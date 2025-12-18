#include "fmtlog.h"

#include "knowledge_base.hpp"
#include "rete/rete_node.hpp"
#include "stateful_session.hpp"

#include <algorithm>
#include <iostream>
#include <list>
#include <magic_enum/magic_enum.hpp>
#include <regex>
#include <sstream>
#include <typeinfo>
#include <utility>

// --- Helper Functions ---
namespace {

    // P1 FIX: Helper for contains check on FactList
    bool fact_list_contains(FactList const& list, ConstraintValue const& value) {
        for (auto const& fact : list.facts) {
            if (!fact) continue;
            // Check if any field matches the value
            for (auto const& [_, field_val] : fact->fields) {
                if (field_val == value) return true;
            }
            // Also check the type
            if (std::holds_alternative<std::string>(value) &&
                fact->type == std::get<std::string>(value)) {
                return true;
            }
        }
        return false;
    }

    // PROD-006: Enhanced regex cache with LRU eviction
    // Thread-safe for read operations, limited to MAX_CACHE_SIZE entries
    class RegexCache {
    public:
        static constexpr size_t MAX_CACHE_SIZE = 1000;

        static RegexCache& instance() {
            static RegexCache cache;
            return cache;
        }

        std::regex const& get(std::string const& pattern) {
            auto it = cache_.find(pattern);
            if (it != cache_.end()) {
                // Move to front of LRU list
                lru_list_.splice(lru_list_.begin(), lru_list_, it->second.second);
                return it->second.first;
            }

            // Compile new regex
            std::regex compiled(pattern);

            // Evict oldest if at capacity
            if (cache_.size() >= MAX_CACHE_SIZE) {
                cache_.erase(lru_list_.back());
                lru_list_.pop_back();
            }

            // Insert at front
            lru_list_.push_front(pattern);
            cache_[pattern] = {std::move(compiled), lru_list_.begin()};
            return cache_[pattern].first;
        }

        size_t size() const { return cache_.size(); }

    private:
        RegexCache() = default;
        std::list<std::string> lru_list_;
        std::unordered_map<std::string, std::pair<std::regex, std::list<std::string>::iterator>> cache_;
    };

    std::regex const& get_cached_regex(std::string const& pattern) {
        return RegexCache::instance().get(pattern);
    }

    // Forward declaration for recursive evaluation
    double evaluate_arith_expr(ArithExprValue const& expr,
                               Token const& token,
                               Fact const& current_fact,
                               map<std::string, int> const& bindings);

    double evaluate_arith_expr(ArithExprValue const& expr,
                               Token const& token,
                               Fact const& current_fact,
                               map<std::string, int> const& bindings) {
        return std::visit(
            [&](auto const& arg) -> double {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, double>) {
                    return arg;
                } else if constexpr (std::is_same_v<T, std::string>) {
                    // It's a variable reference like "$avail" or field name
                    std::string const& ref = arg;
                    if (ref.empty()) return 0.0;

                    if (ref[0] == '$') {
                        // It's a binding reference
                        size_t dot_pos = ref.find('.');
                        std::string binding_name = (dot_pos != std::string::npos)
                            ? ref.substr(0, dot_pos)
                            : ref;
                        std::string field_name = (dot_pos != std::string::npos)
                            ? ref.substr(dot_pos + 1)
                            : "this";

                        // First check if it's a binding to a fact in the token
                        auto it = bindings.find(binding_name);
                        if (it != bindings.end()) {
                            auto bound_fact = token.get_fact_at_depth(it->second);
                            if (bound_fact) {
                                if (field_name == "this") {
                                    // Return 0 for "this" reference (can't convert fact to number)
                                    return 0.0;
                                }
                                auto val_opt = bound_fact->get_field(field_name);
                                if (val_opt) {
                                    if (std::holds_alternative<double>(*val_opt)) {
                                        return std::get<double>(*val_opt);
                                    } else if (std::holds_alternative<int64_t>(*val_opt)) {
                                        return static_cast<double>(std::get<int64_t>(*val_opt));
                                    }
                                }
                            }
                        }
                        // Check current fact's inline bindings - the binding might refer to a field
                        // on the current fact (common in accumulate source patterns)
                        auto val_opt = current_fact.get_field(field_name != "this" ? field_name : binding_name.substr(1));
                        if (val_opt) {
                            if (std::holds_alternative<double>(*val_opt)) {
                                return std::get<double>(*val_opt);
                            } else if (std::holds_alternative<int64_t>(*val_opt)) {
                                return static_cast<double>(std::get<int64_t>(*val_opt));
                            }
                        }
                    } else {
                        // It's a plain field name on current fact
                        auto val_opt = current_fact.get_field(ref);
                        if (val_opt) {
                            if (std::holds_alternative<double>(*val_opt)) {
                                return std::get<double>(*val_opt);
                            } else if (std::holds_alternative<int64_t>(*val_opt)) {
                                return static_cast<double>(std::get<int64_t>(*val_opt));
                            }
                        }
                    }
                    return 0.0;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<ArithExprNode>>) {
                    if (!arg) return 0.0;
                    double left_val = evaluate_arith_expr(arg->left, token, current_fact, bindings);
                    double right_val = evaluate_arith_expr(arg->right, token, current_fact, bindings);
                    switch (arg->op) {
                        case ArithOp::ADD: return left_val + right_val;
                        case ArithOp::SUB: return left_val - right_val;
                        case ArithOp::MUL: return left_val * right_val;
                        case ArithOp::DIV: return (right_val != 0.0) ? left_val / right_val : 0.0;
                    }
                    return 0.0;
                }
                return 0.0;
            },
            expr);
    }

    // Overload for accumulate context where we have inline bindings map
    double evaluate_arith_expr_for_accumulate(ArithExprValue const& expr,
                                              Fact const& fact,
                                              map<std::string, std::string> const& inline_bindings) {
        return std::visit(
            [&](auto const& arg) -> double {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, double>) {
                    return arg;
                } else if constexpr (std::is_same_v<T, std::string>) {
                    std::string const& ref = arg;
                    if (ref.empty()) return 0.0;

                    std::string field_name = ref;
                    if (ref[0] == '$') {
                        // Look up in inline bindings map
                        auto it = inline_bindings.find(ref);
                        if (it != inline_bindings.end()) {
                            field_name = it->second;
                        } else {
                            // Try without the $ prefix as field name
                            field_name = ref.substr(1);
                        }
                    }

                    auto val_opt = fact.get_field(field_name);
                    if (val_opt) {
                        if (std::holds_alternative<double>(*val_opt)) {
                            return std::get<double>(*val_opt);
                        } else if (std::holds_alternative<int64_t>(*val_opt)) {
                            return static_cast<double>(std::get<int64_t>(*val_opt));
                        }
                    }
                    return 0.0;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<ArithExprNode>>) {
                    if (!arg) return 0.0;
                    double left_val = evaluate_arith_expr_for_accumulate(arg->left, fact, inline_bindings);
                    double right_val = evaluate_arith_expr_for_accumulate(arg->right, fact, inline_bindings);
                    switch (arg->op) {
                        case ArithOp::ADD: return left_val + right_val;
                        case ArithOp::SUB: return left_val - right_val;
                        case ArithOp::MUL: return left_val * right_val;
                        case ArithOp::DIV: return (right_val != 0.0) ? left_val / right_val : 0.0;
                    }
                    return 0.0;
                }
                return 0.0;
            },
            expr);
    }

    bool compare_values(ConstraintValue const& v1, std::string const& op, ConstraintValue const& v2) {
        logd("      compare_values: {} {} {}", ::to_string(v1), op, ::to_string(v2));

        bool result = false;

        // P1 FIX: Handle 'contains' and 'not contains' operators
        if (op == "contains" || op == "not contains") {
            bool contains_result = false;

            // Case 1: String contains substring
            if (std::holds_alternative<std::string>(v1) && std::holds_alternative<std::string>(v2)) {
                std::string const& haystack = std::get<std::string>(v1);
                std::string const& needle = std::get<std::string>(v2);
                contains_result = haystack.find(needle) != std::string::npos;
            }
            // Case 2: FactList contains value
            else if (std::holds_alternative<FactList>(v1)) {
                contains_result = fact_list_contains(std::get<FactList>(v1), v2);
            }

            return (op == "contains") ? contains_result : !contains_result;
        }

        // P1 FIX: Handle 'matches' and 'not matches' operators (regex)
        if (op == "matches" || op == "not matches") {
            if (std::holds_alternative<std::string>(v1) && std::holds_alternative<std::string>(v2)) {
                std::string const& text = std::get<std::string>(v1);
                std::string const& pattern = std::get<std::string>(v2);
                try {
                    std::regex re = get_cached_regex(pattern);
                    bool matches = std::regex_search(text, re);
                    return (op == "matches") ? matches : !matches;
                } catch (std::regex_error const& e) {
                    loge("Invalid regex pattern '{}': {}", pattern, e.what());
                    return false;
                }
            }
            return false;
        }
        if (op == "startsWith") {
            if (std::holds_alternative<std::string>(v1) && std::holds_alternative<std::string>(v2)) {
                std::string const& text = std::get<std::string>(v1);
                std::string const& prefix = std::get<std::string>(v2);
                return text.length() >= prefix.length() &&
                       text.compare(0, prefix.length(), prefix) == 0;
            }
            return false;
        }
        if (op == "endsWith") {
            if (std::holds_alternative<std::string>(v1) && std::holds_alternative<std::string>(v2)) {
                std::string const& text = std::get<std::string>(v1);
                std::string const& suffix = std::get<std::string>(v2);
                return text.length() >= suffix.length() &&
                       text.compare(text.length() - suffix.length(), suffix.length(), suffix) == 0;
            }
            return false;
        }
        if (op == "lengthIs") {
            if (std::holds_alternative<std::string>(v1)) {
                std::string const& text = std::get<std::string>(v1);
                int64_t expected_length = 0;
                if (std::holds_alternative<int64_t>(v2)) {
                    expected_length = std::get<int64_t>(v2);
                } else if (std::holds_alternative<double>(v2)) {
                    expected_length = static_cast<int64_t>(std::get<double>(v2));
                }
                return static_cast<int64_t>(text.length()) == expected_length;
            }
            return false;
        }
        // memberOf checks if v1 is a member of v2 (collection) - reverse of contains
        if (op == "memberOf" || op == "not memberOf") {
            bool member_result = false;
            if (std::holds_alternative<FactList>(v2)) {
                member_result = fact_list_contains(std::get<FactList>(v2), v1);
            }
            return (op == "memberOf") ? member_result : !member_result;
        }

        if (std::holds_alternative<NilValue>(v1) || std::holds_alternative<NilValue>(v2)) {
            bool v1_is_nil = std::holds_alternative<NilValue>(v1);
            bool v2_is_nil = std::holds_alternative<NilValue>(v2);
            if (op == "==")
                result = v1_is_nil == v2_is_nil;
            else if (op == "!=")
                result = v1_is_nil != v2_is_nil;
            return result;
        }

        if (std::holds_alternative<std::string>(v1) && std::holds_alternative<std::string>(v2)) {
            std::string const& s1 = std::get<std::string>(v1);
            std::string const& s2 = std::get<std::string>(v2);
            if (op == "==")
                result = s1 == s2;
            else if (op == "!=")
                result = s1 != s2;
            else if (op == ">")
                result = s1 > s2;
            else if (op == "<")
                result = s1 < s2;
            else if (op == ">=")
                result = s1 >= s2;
            else if (op == "<=")
                result = s1 <= s2;
            return result;
        }

        bool is_v1_arith = std::holds_alternative<int64_t>(v1) || std::holds_alternative<double>(v1);
        bool is_v2_arith = std::holds_alternative<int64_t>(v2) || std::holds_alternative<double>(v2);
        if (is_v1_arith && is_v2_arith) {
            double d1 =
                std::holds_alternative<int64_t>(v1) ? static_cast<double>(std::get<int64_t>(v1)) : std::get<double>(v1);
            double d2 =
                std::holds_alternative<int64_t>(v2) ? static_cast<double>(std::get<int64_t>(v2)) : std::get<double>(v2);
            if (op == "==")
                result = d1 == d2;
            else if (op == "!=")
                result = d1 != d2;
            else if (op == ">")
                result = d1 > d2;
            else if (op == "<")
                result = d1 < d2;
            else if (op == ">=")
                result = d1 >= d2;
            else if (op == "<=")
                result = d1 <= d2;
            logd("        -> Arithmetic comparison result: {}", result);

            return result;
        }
        return false;
    }

    bool check_all_join_conditions(StatefulSession& session, Token const& token, Fact const& fact,
                                   std::vector<ParsedConstraint> const& joins,
                                   map<std::string, int> const& bindings) {
        if (joins.empty()) { return true; }

        for (auto const& join : joins) {
            if (join.temporal_constraint) {
                auto const& tc = *join.temporal_constraint;
                auto lhs_val_opt = fact.get_field(tc.lhs_field);
                auto it = bindings.find(tc.rhs_binding_and_field.first);
                if (it == bindings.end()) return false;

                auto bound_fact = token.get_fact_at_depth(it->second);   // Get fact directly from token
                if (!bound_fact) return false;
                auto rhs_val_opt = bound_fact->get_field(tc.rhs_binding_and_field.second);

                if (!lhs_val_opt || !rhs_val_opt) return false;
                auto lhs_ts = std::get_if<int64_t>(&*lhs_val_opt);
                auto rhs_ts = std::get_if<int64_t>(&*rhs_val_opt);
                if (!lhs_ts || !rhs_ts) return false;
                bool pass = false;
                if (tc.op == "after") {
                    pass = (*lhs_ts > *rhs_ts);
                } else if (tc.op == "before") {
                    pass = (*lhs_ts < *rhs_ts);
                } else if (tc.op == "within") {
                    pass = (std::abs(*lhs_ts - *rhs_ts) <= tc.window_ms);
                } else if (tc.op == "coincides") {
                    // For point events, coincides means equal timestamps
                    // Optional: Could add tolerance parameter in future
                    pass = (*lhs_ts == *rhs_ts);
                } else if (tc.op == "during") {
                    // For point events, during means lhs is strictly between rhs bounds
                    // If we only have start timestamp, treat it as lhs > rhs.start
                    // For full interval support, would need end_timestamp field
                    pass = (*lhs_ts > *rhs_ts);
                }
                if (!pass) return false;
                continue;
            }
            std::optional<ConstraintValue> lhs_val_opt;
            std::optional<ConstraintValue> rhs_val_opt;

            if (join.left_binding) {
                auto it = bindings.find(*join.left_binding);
                if (it == bindings.end()) return false;

                auto bound_fact = token.get_fact_at_depth(it->second);
                if (!bound_fact) return false;
                lhs_val_opt = bound_fact->get_field(join.left_field);

            } else {
                lhs_val_opt = fact.get_field(join.left_field);
            }

            if (join.right_bound_field) {
                auto it = bindings.find(join.right_bound_field->first);
                if (it == bindings.end()) return false;

                auto bound_fact = token.get_fact_at_depth(it->second);
                if (!bound_fact) return false;
                rhs_val_opt = bound_fact->get_field(join.right_bound_field->second);

            } else if (join.right_arith_ast.has_value()) {
                // Evaluate arithmetic expression using token bindings
                double result = evaluate_arith_expr(*join.right_arith_ast, token, fact, bindings);
                rhs_val_opt = result;
            } else if (join.right_literal) {
                rhs_val_opt = join.right_literal;
            } else {
                continue;   // Should not happen for a join constraint
            }

            ConstraintValue lhs = lhs_val_opt.value_or(NilValue{});
            ConstraintValue rhs = rhs_val_opt.value_or(NilValue{});
            if (!compare_values(lhs, join.op, rhs)) { return false; }
        }

        return true;
    }

    template <typename T>
    void remove_from_vector(std::vector<T>& vec, T const& item) {
        auto it = std::find(vec.begin(), vec.end(), item);
        if (it != vec.end()) {
            *it = std::move(vec.back());  // O(1) swap-and-pop instead of O(n) shift
            vec.pop_back();
        }
    }
}   // namespace

// --- ReteNode ---
void ReteNode::add_child(std::shared_ptr<ReteNode> child) {
    if (child) {
        logd("Linking parent node {} to child node {}", this->id, child->id);
        children.push_back(child);
        child->add_parent(shared_from_this());
    }
}

void ReteNode::add_parent(std::shared_ptr<ReteNode> parent) {
    if (parent) {
        logd("Linking child node {} to parent node {}", this->id, parent->id);
        parents.push_back(parent);
    }
}

// --- BetaConditionNode ---
BetaConditionNode::BetaConditionNode(std::vector<ParsedConstraint> const& joins,
                                     map<std::string, int> const& bindings) :
    join_constraints(joins), binding_to_token_idx(bindings) {}

void BetaConditionNode::left_activate(StatefulSession& session, Token const& token) {
    logd("Node {}:{} left_activate. Token depth {}, type {}", this->id, typeid(*this).name(), token.wme->depth,
              magic_enum::enum_name(token.type));
    auto const& wme = token.wme;
    if (token.type == PropagationType::RETRACT) {
        auto it = left_memory.find(wme.get());
        if (it != left_memory.end()) {
            if (was_passing(it->second.match_count)) {
                for (auto& weak_child : children) {
                    if (auto child = weak_child.lock()) child->left_activate(session, token);
                }
            }
            left_memory.erase(it);
        }
        return;
    }

    LeftMemoryItem new_item{wme, 0};
    for (auto const& [fact_id, fact] : right_memory) {
        if (check_all_join_conditions(session, token, *fact, join_constraints, binding_to_token_idx)) {
            new_item.match_count++;
        }
    }

    if (condition_passes(new_item.match_count)) {
        for (auto& weak_child : children) {
            if (auto child = weak_child.lock()) child->left_activate(session, token);
        }
    }
    left_memory[wme.get()] = new_item;
}

void BetaConditionNode::right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) {
    logd("Node {}:{} right_activate. Fact ID {}, type {}", this->id, typeid(*this).name(), fact->id,
              magic_enum::enum_name(p_type));

    // If the fact is being retracted, we must first check if it was in our memory.
    if (p_type == PropagationType::RETRACT) {
        if (right_memory.erase(fact->id) == 0) {
            return;   // Fact was not in our memory, so no state change is possible.
        }
    } else {   // ASSERT or MODIFY
        right_memory[fact->id] = fact;
    }

    // Iterate over all tokens in the left memory to see which ones are affected by this fact.
    for (auto& [wme_ptr, item] : left_memory) {
        Token token{item.wme, PropagationType::ASSERT};

        // Check if the arriving fact matches the conditions for the current token.
        bool matches = check_all_join_conditions(session, token, *fact, join_constraints, binding_to_token_idx);

        // If it doesn't match, this fact doesn't affect this token's match count. Continue.
        if (!matches) { continue; }

        // The fact matches. Now update the count and check for a state change.
        bool was_passing_before = was_passing(item.match_count);

        if (p_type == PropagationType::ASSERT) {
            item.match_count++;
        } else {   // RETRACT
            item.match_count--;
        }

        bool is_passing_now = condition_passes(item.match_count);

        // Propagate only if the state has changed (e.g., from passing to not passing).
        if (was_passing_before && !is_passing_now) {
            Token retract_token{item.wme, PropagationType::RETRACT};
            for (auto& weak_child : children) {
                if (auto child = weak_child.lock()) child->left_activate(session, retract_token);
            }
        } else if (!was_passing_before && is_passing_now) {
            Token assert_token{item.wme, PropagationType::ASSERT};
            for (auto& weak_child : children) {
                if (auto child = weak_child.lock()) child->left_activate(session, assert_token);
            }
        }
    }
}

// --- AlphaNode ---
AlphaNode::AlphaNode(ParsedConstraint const& c) : constraint(c) {}

void AlphaNode::left_activate(StatefulSession&, Token const&) {}

void AlphaNode::right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) {
    bool passes = check_constraint(*fact);
    logd("Node {}:AlphaNode right_activate. Fact ID {}. Constraint check: {}", this->id, fact->id,
              passes ? "PASS" : "FAIL");
    if (passes) {
        for (auto& weak_child : children) {
            if (auto child = weak_child.lock()) { child->right_activate(session, fact, p_type); }
        }
    }
}

bool AlphaNode::check_constraint(Fact const& fact) const {
    // A constraint with an empty operator is a pure binding (like `$id: id`)
    // or an existence check (`name`).
    if (constraint.op.empty()) {
        // If there's no right literal, it's a pure binding. It should always pass
        // the alpha check, as the binding itself is handled elsewhere.
        if (!constraint.right_literal.has_value()) {
            logd("  -> AlphaNode ID {} passing pure binding on field '{}'.", this->id, constraint.left_field);
            return true;
        }
        // Otherwise, it's an existence check that was transformed to `field == 1`.
        // This will be handled by the main comparison logic below.
    }

    auto fact_val_opt = fact.get_field(constraint.left_field);
    if (!fact_val_opt) {
        logd("  -> AlphaNode ID {} check FAILED: field '{}' not found on fact.", this->id, constraint.left_field);
        return false;
    }

    ConstraintValue const& lhs = *fact_val_opt;

    // Handle 'in' and 'not in' operators with value list
    if ((constraint.op == "in" || constraint.op == "not in") && constraint.right_value_list.has_value()) {
        bool found = false;
        for (auto const& list_val : *constraint.right_value_list) {
            if (compare_values(lhs, "==", list_val)) {
                found = true;
                break;
            }
        }
        bool result = (constraint.op == "in") ? found : !found;
        logd("  -> AlphaNode ID {} checking: {} {} [list of {} values] -> {}",
             this->id, to_string(lhs), constraint.op, constraint.right_value_list->size(),
             result ? "PASS" : "FAIL");
        return result;
    }

    // Handle arithmetic expressions on the RHS (e.g., `price > base * 1.2`)
    // AlphaNode can only evaluate expressions referencing current fact fields
    ConstraintValue rhs;
    if (constraint.right_arith_ast.has_value()) {
        static map<std::string, std::string> empty_bindings;
        double result = evaluate_arith_expr_for_accumulate(*constraint.right_arith_ast, fact, empty_bindings);
        rhs = result;
        logd("  -> AlphaNode ID {} evaluated arithmetic expression to {}", this->id, result);
    } else {
        rhs = constraint.right_literal.value_or(NilValue{});
    }
    bool result = compare_values(lhs, constraint.op, rhs);

    logd("  -> AlphaNode ID {} checking: LHS: {} (type {}) {} RHS: {} (type {}) -> {}", this->id, to_string(lhs),
              lhs.index(), constraint.op, to_string(rhs), rhs.index(), result ? "PASS" : "FAIL");

    return result;
}

void AlphaNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"AlphaNode (" << id << ")\\n";
    if (!constraint.left_field.empty()) {
        os << constraint.left_field << " " << constraint.op << " "
           << ::to_string(constraint.right_literal.value_or(NilValue{}));
    } else {
        os << "(No Constraint)";
    }
    os << "\", shape=ellipse, style=filled, fillcolor=orange];";
}

// --- EntryPointNode ---
void EntryPointNode::left_activate(StatefulSession&, Token const&) {
    // An EntryPointNode is the start of an alpha chain. It does not receive left activations.
}

void EntryPointNode::right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) {
    logd("Node {}:EntryPointNode right_activate. Fact ID {}, type {}", this->id, fact->id,
              magic_enum::enum_name(p_type));
    for (auto& weak_child : children) {
        if (auto child = weak_child.lock()) { child->right_activate(session, fact, p_type); }
    }
}

void EntryPointNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"Entry Point (" << id << ")\", shape=house, style=filled, fillcolor=yellow];";
}

// --- BaseJoinNode ---
BaseJoinNode::BaseJoinNode(std::vector<ParsedConstraint> joins, map<std::string, int> bindings) :
    ReteNode(), join_constraints_(std::move(joins)), binding_to_token_idx_(std::move(bindings)) {}

void BaseJoinNode::propagate_assert(StatefulSession& session, Token const& token,
                                    std::shared_ptr<Fact> fact) {
    auto new_wme = session.get_or_create_wme(token.wme, fact);
    left_to_children_[token.wme.get()].push_back(new_wme);
    right_to_children_[fact->id].push_back(new_wme);
    Token new_token{new_wme, PropagationType::ASSERT};
    logd("Node {}:{} propagating ASSERT. Old token depth {}, new token depth {}", this->id, typeid(*this).name(),
              token.wme->depth, new_wme->depth);
    for (auto& weak_child : children) {
        if (auto c = weak_child.lock()) c->left_activate(session, new_token);
    }
}

void BaseJoinNode::propagate_retract(StatefulSession& session, std::shared_ptr<TokenWME const> wme,
                                     std::shared_ptr<Fact> fact) {
    auto it_left = left_to_children_.find(wme.get());
    if (it_left == left_to_children_.end()) return;

    std::shared_ptr<TokenWME const> child_to_retract = nullptr;
    for (auto const& child_wme : it_left->second) {
        if (child_wme->fact->id == fact->id) {
            child_to_retract = child_wme;
            break;
        }
    }

    if (child_to_retract) {
        logd("Node {}:{} propagating RETRACT. Old token depth {}, fact ID {}", this->id, typeid(*this).name(),
                  wme->depth, fact->id);
        Token retract_token{child_to_retract, PropagationType::RETRACT};
        for (auto& weak_child : children) {
            if (auto c = weak_child.lock()) c->left_activate(session, retract_token);
        }
        remove_from_vector(it_left->second, child_to_retract);
        if (it_left->second.empty()) left_to_children_.erase(it_left);

        auto it_right = right_to_children_.find(fact->id);
        if (it_right != right_to_children_.end()) {
            remove_from_vector(it_right->second, child_to_retract);
            if (it_right->second.empty()) right_to_children_.erase(it_right);
        }

        // Invalidate WME cache so that subsequent ASSERT can create a fresh WME
        session.invalidate_wme_cache(child_to_retract->hash);
    }
}

// --- HashedJoinNode ---
HashedJoinNode::HashedJoinNode(std::vector<ParsedConstraint> joins, map<std::string, int> bindings,
                               std::pair<std::string, int> left_hash_key, std::string right_hash_key) :
    BaseJoinNode(std::move(joins), std::move(bindings)), left_hash_key_(std::move(left_hash_key)),
    right_hash_key_(std::move(right_hash_key)) {}

std::optional<ConstraintValue> HashedJoinNode::get_key(Token const& token) const {
    auto fact_at_depth = token.get_fact_at_depth(left_hash_key_.second);
    if (!fact_at_depth) return std::nullopt;
    return fact_at_depth->get_field(left_hash_key_.first);
}

std::optional<ConstraintValue> HashedJoinNode::get_key(std::shared_ptr<Fact> const& fact) const {
    return fact->get_field(right_hash_key_);
}

void HashedJoinNode::left_activate(StatefulSession& session, Token const& token) {
    logd("Node {}:HashedJoinNode left_activate. Token depth {}, type {}", this->id, token.wme->depth,
              magic_enum::enum_name(token.type));
    auto key_opt = get_key(token);
    if (!key_opt) return;
    auto const& key = *key_opt;
    logd("  -> Left key: {}", ::to_string(key));

    if (token.type == PropagationType::RETRACT) {
        auto mem_it = left_memory_.find(key);
        if (mem_it != left_memory_.end()) {
            remove_from_vector(mem_it->second, token.wme);
            if (mem_it->second.empty()) left_memory_.erase(mem_it);
        }
        auto fact_it = right_memory_.find(key);
        if (fact_it != right_memory_.end()) {
            for (auto const& fact : fact_it->second) { propagate_retract(session, token.wme, fact); }
        }
        return;
    }

    left_memory_[key].push_back(token.wme);
    auto it_right = right_memory_.find(key);
    if (it_right != right_memory_.end()) {
        logd("  -> Found {} matching facts in right memory.", it_right->second.size());
        for (auto const& fact : it_right->second) {
            if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_)) {
                propagate_assert(session, token, fact);
            }
        }
    }
}

void HashedJoinNode::right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) {
    logd("Node {}:HashedJoinNode right_activate. Fact ID {}, type {}", this->id, fact->id,
              magic_enum::enum_name(p_type));
    auto key_opt = get_key(fact);
    if (!key_opt) return;
    auto const& key = *key_opt;
    logd("  -> Right key: {}", ::to_string(key));

    if (p_type == PropagationType::RETRACT) {
        auto mem_it = right_memory_.find(key);
        if (mem_it != right_memory_.end()) {
            remove_from_vector(mem_it->second, fact);
            if (mem_it->second.empty()) right_memory_.erase(mem_it);
        }
        auto token_it = left_memory_.find(key);
        if (token_it != left_memory_.end()) {
            for (auto const& wme : token_it->second) { propagate_retract(session, wme, fact); }
        }
        return;
    }

    // For MODIFY: first retract old matches, then assert new ones
    if (p_type == PropagationType::MODIFY) {
        auto mem_it = right_memory_.find(key);
        if (mem_it != right_memory_.end()) {
            // Check if fact is in the memory for this key
            auto fact_it = std::find(mem_it->second.begin(), mem_it->second.end(), fact);
            if (fact_it != mem_it->second.end()) {
                logd("  -> MODIFY: Retracting old matches before re-asserting.");
                auto token_it = left_memory_.find(key);
                if (token_it != left_memory_.end()) {
                    for (auto const& wme : token_it->second) { propagate_retract(session, wme, fact); }
                }
                // Remove from memory - will be re-added below
                remove_from_vector(mem_it->second, fact);
                if (mem_it->second.empty()) right_memory_.erase(mem_it);
            }
        }
    }

    right_memory_[key].push_back(fact);
    auto it_left = left_memory_.find(key);
    if (it_left != left_memory_.end()) {
        logd("  -> Found {} matching tokens in left memory.", it_left->second.size());
        for (auto const& wme : it_left->second) {
            Token token{wme, PropagationType::ASSERT};
            if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_)) {
                propagate_assert(session, token, fact);
            }
        }
    }
}

void HashedJoinNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"HashedJoinNode (" << id << ")\\nIndex: " << right_hash_key_ << " == $"
       << left_hash_key_.second << "." << left_hash_key_.first;
    if (!join_constraints_.empty()) {
        os << "\\nJoins:";
        for (auto const& join : join_constraints_) { os << "\\n" << constraint_to_string(join); }
    }
    os << "\", shape=box, style=filled, fillcolor=lightblue];";
}

// --- CrossProductJoinNode ---
CrossProductJoinNode::CrossProductJoinNode(std::vector<ParsedConstraint> joins, map<std::string, int> bindings) :
    BaseJoinNode(std::move(joins), std::move(bindings)) {}

void CrossProductJoinNode::left_activate(StatefulSession& session, Token const& token) {
    logd("Node {}:CrossProductJoinNode left_activate. Token depth {}, type {}", this->id, token.wme->depth,
              magic_enum::enum_name(token.type));
    if (token.type == PropagationType::RETRACT) {
        if (left_memory_.erase(token.wme.get()) > 0) {
            logd("  -> Retracted token from left memory. Propagating retract to {} children.", children.size());
            for (auto const& [id, fact] : right_memory_) { propagate_retract(session, token.wme, fact); }
        }
        return;
    }

    left_memory_[token.wme.get()] = token.wme;
    for (auto const& [id, fact] : right_memory_) {
        if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_)) {
            propagate_assert(session, token, fact);
        }
    }
}

void CrossProductJoinNode::right_activate(StatefulSession& session, std::shared_ptr<Fact> fact,
                                          PropagationType p_type) {
    logd("Node {}:CrossProductJoinNode right_activate. Fact ID {}, type {}", this->id, fact->id,
              magic_enum::enum_name(p_type));

    if (p_type == PropagationType::RETRACT) {
        if (right_memory_.erase(fact->id) > 0) {
            logd("  -> Retracted fact from right memory. Propagating retract to {} tokens in left memory.",
                      left_memory_.size());
            for (auto const& [ptr, wme] : left_memory_) { propagate_retract(session, wme, fact); }
        }
        return;
    }

    // For MODIFY: first retract old matches, then assert new ones
    if (p_type == PropagationType::MODIFY && right_memory_.count(fact->id) > 0) {
        logd("  -> MODIFY: Retracting old matches before re-asserting.");
        for (auto const& [ptr, wme] : left_memory_) { propagate_retract(session, wme, fact); }
    }

    right_memory_[fact->id] = fact;
    for (auto const& [ptr, wme] : left_memory_) {
        Token token{wme, PropagationType::ASSERT};
        if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_)) {
            propagate_assert(session, token, fact);
        }
    }
}

void CrossProductJoinNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"CrossProductJoinNode (" << id << ")";
    if (!join_constraints_.empty()) {
        os << "\\nJoins:";
        for (auto const& join : join_constraints_) { os << "\\n" << constraint_to_string(join); }
    }
    os << "\", shape=box, style=filled, fillcolor=lightgrey];";
}

// --- NotNode ---
NotNode::NotNode(std::vector<ParsedConstraint> const& joins, map<std::string, int> const& bindings) :
    BetaConditionNode(joins, bindings) {}

void NotNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"NotNode (" << id << ")";
    if (!join_constraints.empty()) {
        os << "\\nJoins:";
        for (auto const& join : join_constraints) { os << "\\n" << constraint_to_string(join); }
    }
    os << "\", shape=octagon, style=filled, fillcolor=salmon];";
}

// --- ExistsNode ---
ExistsNode::ExistsNode(std::vector<ParsedConstraint> const& joins, map<std::string, int> const& bindings) :
    BetaConditionNode(joins, bindings) {}

void ExistsNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"ExistsNode (" << id << ")";
    if (!join_constraints.empty()) {
        os << "\\nJoins:";
        for (auto const& join : join_constraints) { os << "\\n" << constraint_to_string(join); }
    }
    os << "\", shape=octagon, style=filled, fillcolor=khaki];";
}

// --- AccumulateNode ---
namespace {
    std::optional<ConstraintValue> get_accumulate_value(
        ParsedAccumulate const& info,
        Fact const& fact)
    {
        // If we have an arithmetic expression AST, evaluate it
        if (info.accumulate_expr_ast.has_value()) {
            double result = evaluate_arith_expr_for_accumulate(
                *info.accumulate_expr_ast, fact, info.inline_binding_to_field);
            return ConstraintValue{result};
        }
        // Otherwise use simple field lookup
        if (!info.accumulate_field_name.empty()) {
            return fact.get_field(info.accumulate_field_name);
        }
        return std::nullopt;
    }
}

AccumulateNode::AccumulateNode(IAccumulator const* prototype, ParsedAccumulate&& accumulate_info,
                               std::string res_fact_type, map<std::string, int> bindings,
                               std::vector<ParsedConstraint> joins) :
    accumulator_prototype(prototype), info(std::move(accumulate_info)), result_fact_type(std::move(res_fact_type)),
    binding_to_token_idx(std::move(bindings)), join_constraints(std::move(joins)) {}

void AccumulateNode::left_activate(StatefulSession& session, Token const& token) {
    logd("Node {}:AccumulateNode left_activate. Token depth {}, type {}", this->id, token.wme->depth,
              magic_enum::enum_name(token.type));
    auto const& wme = token.wme;
    if (token.type == PropagationType::RETRACT) {
        auto it = left_memory.find(wme.get());
        if (it != left_memory.end()) {
            session.retract_fact(it->second.result_fact);
            left_memory.erase(it);
        }
        return;
    }
    if (left_memory.count(wme.get())) return;
    LeftMemoryItem new_item;
    new_item.wme = wme;
    new_item.accumulator = accumulator_prototype->clone();
    new_item.result_fact = std::make_shared<Fact>();
    new_item.result_fact->type = result_fact_type;
    bool is_collect_list = (info.function == "collect" || info.function == "collectList");
    bool is_collect_set = (info.function == "collectSet");
    for (auto const& [id, fact_ptr] : right_memory) {
        if (check_all_join_conditions(session, token, *fact_ptr, join_constraints, binding_to_token_idx)) {
            if (is_collect_set) {
                new_item.contributing_facts_set.insert(fact_ptr);
            } else if (is_collect_list) {
                new_item.contributing_facts_list.push_back(fact_ptr);
            } else {
                if (auto value_opt = get_accumulate_value(info, *fact_ptr)) {
                    new_item.accumulator->accumulate(*value_opt);
                }
            }
        }
    }
    update_and_propagate_result(session, new_item);   // This will add the fact
    left_memory[wme.get()] = std::move(new_item);
}

void AccumulateNode::right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) {
    logd("Node {}:AccumulateNode right_activate. Fact ID {}, type {}", this->id, fact->id,
              magic_enum::enum_name(p_type));
    if (p_type == PropagationType::MODIFY || !accumulator_prototype->supports_reverse()) {
        if (p_type == PropagationType::ASSERT || p_type == PropagationType::MODIFY) {
            right_memory[fact->id] = fact;
        } else {
            if (right_memory.erase(fact->id) == 0) return;
        }
        bool is_collect_list = (info.function == "collect" || info.function == "collectList");
        bool is_collect_set = (info.function == "collectSet");
        for (auto& [wme_ptr, item] : left_memory) {
            Token current_token{item.wme, PropagationType::ASSERT};
            item.accumulator->clear();
            item.contributing_facts_list.clear();
            item.contributing_facts_set.clear();
            for (auto const& [id, f_ptr] : right_memory) {
                if (check_all_join_conditions(session, current_token, *f_ptr, join_constraints,
                                              binding_to_token_idx)) {
                    if (is_collect_set) {
                        item.contributing_facts_set.insert(f_ptr);
                    } else if (is_collect_list) {
                        item.contributing_facts_list.push_back(f_ptr);
                    } else {
                        if (auto value_opt = get_accumulate_value(info, *f_ptr)) {
                            item.accumulator->accumulate(*value_opt);
                        }
                    }
                }
            }
            update_and_propagate_result(session, item);
        }
    } else {
        if (p_type == PropagationType::ASSERT) {
            right_memory[fact->id] = fact;
            for (auto& [wme_ptr, item] : left_memory) {
                Token token{item.wme, PropagationType::ASSERT};
                if (check_all_join_conditions(session, token, *fact, join_constraints, binding_to_token_idx)) {
                    if (auto value_opt = get_accumulate_value(info, *fact)) {
                        item.accumulator->accumulate(*value_opt);
                        update_and_propagate_result(session, item);
                    }
                }
            }
        } else {
            if (right_memory.count(fact->id) == 0) return;
            for (auto& [wme_ptr, item] : left_memory) {
                Token token{item.wme, PropagationType::ASSERT};
                if (check_all_join_conditions(session, token, *fact, join_constraints, binding_to_token_idx)) {
                    if (auto value_opt = get_accumulate_value(info, *fact)) {
                        item.accumulator->reverse(*value_opt);
                        update_and_propagate_result(session, item);
                    }
                }
            }
            right_memory.erase(fact->id);
        }
    }
}

void AccumulateNode::update_and_propagate_result(StatefulSession& session, LeftMemoryItem& item) {
    bool is_new_fact = (item.result_fact->id == 0);
    auto modifier = [&](Fact& f) {
        bool is_collect = (info.function == "collect" || info.function == "collectList");
        bool is_collect_set = (info.function == "collectSet");
        if (is_collect) {
            FactList fl;
            fl.facts = item.contributing_facts_list;
            f.fields["result"] = fl;
        } else if (is_collect_set) {
            FactList fl;
            fl.facts.assign(item.contributing_facts_set.begin(), item.contributing_facts_set.end());
            f.fields["result"] = fl;
        } else {
            ConstraintValue result_val = item.accumulator->get_result();
            f.fields["result"] = result_val;

            // For Number type, provide Java-like accessor methods (intValue, doubleValue, etc.)
            // This fulfills the contract promised by the Number type schema
            double numeric_result = 0.0;
            if (std::holds_alternative<int64_t>(result_val)) {
                numeric_result = static_cast<double>(std::get<int64_t>(result_val));
            } else if (std::holds_alternative<double>(result_val)) {
                numeric_result = std::get<double>(result_val);
            }
            f.fields["intValue"] = static_cast<int64_t>(numeric_result);
            f.fields["longValue"] = static_cast<int64_t>(numeric_result);
            f.fields["doubleValue"] = numeric_result;
            f.fields["floatValue"] = numeric_result;
            f.fields["value"] = numeric_result;
        }
    };
    if (is_new_fact) {
        modifier(*item.result_fact);
        session.add_fact(item.result_fact);
        auto result_wme = session.get_or_create_wme(item.wme, item.result_fact);
        Token assert_token{result_wme, PropagationType::ASSERT};
        logd("  -> Accumulate created new result fact ID {}, propagating.", item.result_fact->id);
        for (auto& weak_child : children) {
            if (auto c = weak_child.lock()) c->left_activate(session, assert_token);
        }
    } else {
        logd("  -> Accumulate updating existing result fact ID {}.", item.result_fact->id);
        session.update_fact(item.result_fact, modifier);
    }
}

void AccumulateNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"AccumulateNode (" << id << ")\\nFunction: " << info.function;
    if (!info.accumulate_field_name.empty()) { os << "\\nField: " << info.accumulate_field_name; }
    os << "\\nResult Type: " << result_fact_type;
    if (!join_constraints.empty()) {
        os << "\\nJoins:";
        for (auto const& join : join_constraints) { os << "\\n" << constraint_to_string(join); }
    }
    os << "\", shape=cylinder, style=filled, fillcolor=plum];";
}

// --- UnnestNode ---
UnnestNode::UnnestNode(ParsedUnnest const& unnest_info, map<std::string, int> const& bindings) :
    info(unnest_info), binding_to_token_idx(bindings) {}

void UnnestNode::left_activate(StatefulSession& session, Token const& token) {
    logd("Node {}:UnnestNode left_activate. Token depth {}, type {}", this->id, token.wme->depth,
              magic_enum::enum_name(token.type));
    auto const& wme = token.wme;
    if (token.type == PropagationType::RETRACT) {
        auto it = parent_to_children_map.find(wme.get());
        if (it != parent_to_children_map.end()) {
            for (auto const& child_wme : it->second.second) {
                Token child_token{child_wme, PropagationType::RETRACT};
                for (auto& weak_child : children) {
                    if (auto c = weak_child.lock()) c->left_activate(session, child_token);
                }
            }
            parent_to_children_map.erase(it);
        }
        return;
    }
    auto it_binding = binding_to_token_idx.find(info.source_binding);
    if (it_binding == binding_to_token_idx.end()) return;
    int token_idx = it_binding->second;
    auto source_fact = token.get_fact_at_depth(token_idx);
    if (!source_fact) return;
    auto collection_opt = source_fact->get_field(info.source_field);
    if (collection_opt && std::holds_alternative<FactList>(*collection_opt)) {
        auto const& list = std::get<FactList>(*collection_opt).facts;
        if (list.empty()) return;
        logd("  -> Unnesting {} items from {}.{}", list.size(), info.source_binding, info.source_field);
        std::vector<std::shared_ptr<TokenWME const>> new_child_wmes;
        new_child_wmes.reserve(list.size());
        for (auto const& item_fact : list) {
            auto new_wme = session.get_or_create_wme(wme, item_fact);
            new_child_wmes.push_back(new_wme);
            Token new_token{new_wme, PropagationType::ASSERT};
            for (auto& weak_child : children) {
                if (auto c = weak_child.lock()) c->left_activate(session, new_token);
            }
        }
        parent_to_children_map[wme.get()] = {wme, std::move(new_child_wmes)};
    }
}

void UnnestNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"UnnestNode (" << id << ")\\nFrom: " << info.source_binding << "."
       << info.source_field << "\", shape=trapezium, style=filled, fillcolor=darksalmon];";
}

// --- EvalNode ---
EvalNode::EvalNode(std::string expr, map<std::string, int> bindings) :
    expression(std::move(expr)), binding_to_token_idx(std::move(bindings)) {}

void EvalNode::left_activate(StatefulSession& session, Token const& token) {
    auto const& wme = token.wme;
    logd("Node {}:EvalNode left_activate. Token depth {}, type {}", this->id, wme->depth,
              magic_enum::enum_name(token.type));
    if (token.type == PropagationType::RETRACT) {
        if (memory.erase(wme.get()) > 0) {
            logd("  -> Retracted token from memory. Propagating retract to children.");
            for (auto& weak_child : children) {
                if (auto c = weak_child.lock()) c->left_activate(session, token);
            }
        }
        return;
    }
    bool result = session.execute_eval(expression, token, binding_to_token_idx);
    logd("  -> Eval expression '{}' result: {}", expression, result);
    if (result) {
        memory[wme.get()] = wme;
        logd("  -> Eval passed. Propagating assert to children.");
        for (auto& weak_child : children) {
            if (auto c = weak_child.lock()) c->left_activate(session, token);
        }
    }
}

void EvalNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"EvalNode (" << id << ")\\n" << expression;
    os << "\\nBindings: ";
    bool first = true;
    for (auto const& [name, idx] : binding_to_token_idx) {
        if (!first) os << ", ";
        os << name << "->" << idx;
        first = false;
    }
    os << "\", shape=underline, style=filled, fillcolor=orchid];";
}

// --- TerminalNode ---
TerminalNode::TerminalNode(ParsedRule const& r, map<std::string, int> b) :
    rule_name(r.name), binding_to_token_idx(std::move(b)) {}

void TerminalNode::left_activate(StatefulSession& session, Token const& token) {
    logd("Node {}:TerminalNode left_activate for rule '{}'. Token type: {}", this->id, rule_name,
              magic_enum::enum_name(token.type));
    auto wme_ptr = token.wme.get();

    auto const* rule = session.get_knowledge_base()->find_rule_by_name(rule_name);
    if (!rule) return;   // Should not happen in a valid network

    if (token.type == PropagationType::RETRACT) {
        if (memory_.erase(wme_ptr) > 0) {
            size_t hash = std::hash<TokenWME const*>{}(wme_ptr) ^ reinterpret_cast<uintptr_t>(rule);
            session.remove_activation(hash);
            for (auto& listener : session.get_listeners()) {
                listener->on_activation_retracted(rule->name, token.get_facts());
            }
        }
        session.logical_retract(wme_ptr);
    } else if (token.type == PropagationType::ASSERT) {
        if (memory_.find(wme_ptr) == memory_.end()) {
            memory_.insert(wme_ptr);
            size_t hash = std::hash<TokenWME const*>{}(wme_ptr) ^ reinterpret_cast<uintptr_t>(rule);
            Activation activation{rule, token, hash, this->binding_to_token_idx};
            session.add_activation(activation);
            // Notify listeners about creation
            for (auto& listener : session.get_listeners()) {
                listener->on_activation_created(rule->name, token.get_facts());
            }
            if (rule->auto_focus && rule->agenda_group) {
                logd("  -> auto-focus: Setting focus to agenda-group '{}'", *rule->agenda_group);
                session.set_focus(*rule->agenda_group);
            }
        }
    }
}

void TerminalNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"TerminalNode (" << id << ")\\nRule: " << rule_name
       << "\", shape=box, style=filled, fillcolor=lightgreen, peripheries=2];";
}

// --- QueryTerminalNode ---
QueryTerminalNode::QueryTerminalNode(map<std::string, int> bindings) : binding_to_token_idx(std::move(bindings)) {}

void QueryTerminalNode::left_activate(StatefulSession& session, Token const& token) {
    logd("Node {}:QueryTerminalNode left_activate. Token type: {}", this->id, magic_enum::enum_name(token.type));
    auto const& wme = token.wme;
    if (token.type == PropagationType::ASSERT) {
        results[wme.get()] = token;
        logd("  -> Added token to query results. Total results: {}", results.size());
    } else {
        results.erase(wme.get());
        logd("  -> Removed token from query results. Total results: {}", results.size());
    }
}

void QueryTerminalNode::clear_results() {
    logd("Node {}:QueryTerminalNode clearing results.", this->id);
    results.clear();
}

void QueryTerminalNode::set_bindings(map<std::string, int> const& bindings) { binding_to_token_idx = bindings; }

void QueryTerminalNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"Query Terminal (" << id << ")\\n";
    os << "Bindings: ";
    bool first = true;
    for (auto const& [name, idx] : binding_to_token_idx) {
        if (!first) os << ", ";
        os << name << "->" << idx;
        first = false;
    }
    os << "\", shape=Mdiamond, style=filled, fillcolor=lemonchiffon, peripheries=2];";
}

// --- QueryInputNode ---
QueryInputNode::QueryInputNode(std::shared_ptr<QueryTerminalNode> terminal) : terminal_node(terminal) {}

void QueryInputNode::execute(StatefulSession& session, std::vector<std::shared_ptr<Fact>> const& args) {
    logd("Node {}:QueryInputNode execute with {} args.", this->id, args.size());
    if (auto terminal = terminal_node.lock()) {
        terminal->clear_results();
    } else {
        return;
    }

    for (auto const& arg_fact : args) {
        if (arg_fact) { session._internal_add_fact(arg_fact); }
    }

    std::shared_ptr<TokenWME const> current_wme = session.get_dummy_wme();
    for (auto const& arg_fact : args) { current_wme = session.get_or_create_wme(current_wme, arg_fact); }
    Token initial_token{current_wme, PropagationType::ASSERT};
    for (auto& weak_child : children) {
        if (auto c = weak_child.lock()) { c->left_activate(session, initial_token); }
    }

    for (auto const& arg_fact : args) {
        if (arg_fact) { session._internal_remove_fact(arg_fact->id); }
    }
}

void QueryInputNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"Query Input (" << id << ")\", shape=invhouse, style=filled, fillcolor=yellow];";
}


