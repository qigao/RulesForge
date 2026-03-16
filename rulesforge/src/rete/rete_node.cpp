#include "core/logging_control.hpp"

#include "engine/knowledge_base.hpp"
#include "rete/rete_node.hpp"
#include "expression_evaluator.hpp"
#include "engine/stateful_session.hpp"
#include "turbo_parser.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iosfwd>
#include <list>

#include <regex>
#include <sstream>
#include <typeinfo>
#include <utility>

#include <jsoncons/json.hpp>
#include <jsoncons_ext/jmespath/jmespath.hpp>

using namespace rulesforge;

// --- Helper Functions ---
namespace {
    std::atomic<uint64_t> g_alpha_checks{0};
    std::atomic<uint64_t> g_join_checks{0};
    std::atomic<uint64_t> g_compare_calls{0};
    std::atomic<uint64_t> g_compiled_expr_evals{0};
    std::atomic<uint64_t> g_field_lookups{0};

    bool changed_fields_contains(rulesforge::ModifiedFieldsHint const* changed_fields,
                                 std::string_view field) {
        if (!changed_fields) return true;
        return changed_fields->contains(field);
    }

    bool alpha_constraint_affected(ParsedConstraint const& c,
                                   rulesforge::ModifiedFieldsHint const* changed_fields) {
        if (!changed_fields) return true;
        if (c.compiled_expr) return true;  // conservative: expression dependencies are not tracked
        if (c.left_field == "this" || c.left_field.empty()) return true;

        if (!c.cached_left_field_path.empty() && !c.cached_left_field_path[0].name.empty()) {
            return changed_fields_contains(changed_fields, c.cached_left_field_path[0].name);
        }

        size_t end = c.left_field.find_first_of(".[");
        std::string_view root = (end == std::string::npos)
            ? std::string_view(c.left_field)
            : std::string_view(c.left_field.data(), end);
        if (root.empty()) return true;
        return changed_fields_contains(changed_fields, root);
    }

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

    // Helper to extract numeric value from a ConstraintValue
    double constraint_value_to_double(std::optional<ConstraintValue> const& val_opt) {
        if (!val_opt) return 0.0;
        if (std::holds_alternative<double>(*val_opt)) {
            return std::get<double>(*val_opt);
        } else if (std::holds_alternative<int64_t>(*val_opt)) {
            return static_cast<double>(std::get<int64_t>(*val_opt));
        }
        return 0.0;
    }

    double constraint_value_to_double_flexible(std::optional<ConstraintValue> const& val_opt) {
        if (!val_opt) return 0.0;
        if (std::holds_alternative<double>(*val_opt)) {
            return std::get<double>(*val_opt);
        }
        if (std::holds_alternative<int64_t>(*val_opt)) {
            return static_cast<double>(std::get<int64_t>(*val_opt));
        }
        if (std::holds_alternative<std::string>(*val_opt)) {
            try {
                return std::stod(std::get<std::string>(*val_opt));
            } catch (...) {
                return 0.0;
            }
        }
        return 0.0;
    }

    // Create a variable resolver for join conditions (token + current fact + bindings)
    VariableResolver make_join_resolver(Token const& token,
                                        Fact const& current_fact,
                                        std::map<std::string, int> const& bindings) {
        return [&token, &current_fact, &bindings](std::string const& var_name) -> double {
            if (var_name.empty()) return 0.0;

            if (var_name[0] == '$') {
                // It's a binding reference like "$avail" or "$order.total"
                size_t dot_pos = var_name.find('.');
                std::string binding_name = (dot_pos != std::string::npos)
                    ? var_name.substr(0, dot_pos)
                    : var_name;
                std::string field_name = (dot_pos != std::string::npos)
                    ? var_name.substr(dot_pos + 1)
                    : "this";

                // Check if it's a binding to a fact in the token
                auto it = bindings.find(binding_name);
                if (it != bindings.end()) {
                    auto bound_fact = token.get_fact_at_depth(it->second);
                    if (bound_fact) {
                        if (field_name == "this") {
                            return 0.0;  // Can't convert fact to number
                        }
                        return constraint_value_to_double(bound_fact->get_field(field_name));
                    }
                }
                // Check current fact's fields
                auto val_opt = current_fact.get_field(field_name != "this" ? field_name : binding_name.substr(1));
                return constraint_value_to_double(val_opt);
            } else {
                // Plain field name on current fact
                return constraint_value_to_double(current_fact.get_field(var_name));
            }
        };
    }

    // Create a variable resolver for accumulate expressions (fact + inline bindings)
    VariableResolver make_accumulate_resolver(Fact const& fact,
                                              std::map<std::string, std::string> const& inline_bindings) {
        return [&fact, &inline_bindings](std::string const& var_name) -> double {
            if (var_name.empty()) return 0.0;

            std::string field_name = var_name;
            if (var_name[0] == '$') {
                // Look up in inline bindings map
                auto it = inline_bindings.find(var_name);
                if (it != inline_bindings.end()) {
                    field_name = it->second;
                } else {
                    // Try without the $ prefix as field name
                    field_name = var_name.substr(1);
                }
            }

            return constraint_value_to_double(fact.get_field(field_name));
        };
    }

    bool compare_values(ConstraintValue const& v1, CompareOp op, ConstraintValue const& v2) {
        g_compare_calls.fetch_add(1, std::memory_order_relaxed);

        bool result = false;

        // Fast path: arithmetic comparison (most common case)
        bool is_v1_arith = std::holds_alternative<int64_t>(v1) || std::holds_alternative<double>(v1);
        bool is_v2_arith = std::holds_alternative<int64_t>(v2) || std::holds_alternative<double>(v2);
        if (is_v1_arith && is_v2_arith) {
            double d1 =
                std::holds_alternative<int64_t>(v1) ? static_cast<double>(std::get<int64_t>(v1)) : std::get<double>(v1);
            double d2 =
                std::holds_alternative<int64_t>(v2) ? static_cast<double>(std::get<int64_t>(v2)) : std::get<double>(v2);
            switch (op) {
                case CompareOp::EQ: result = d1 == d2; break;
                case CompareOp::NE: result = d1 != d2; break;
                case CompareOp::GT: result = d1 > d2; break;
                case CompareOp::LT: result = d1 < d2; break;
                case CompareOp::GE: result = d1 >= d2; break;
                case CompareOp::LE: result = d1 <= d2; break;
                default: break;
            }
            return result;
        }

        // Fast path: string comparison (second most common)
        if (std::holds_alternative<std::string>(v1) && std::holds_alternative<std::string>(v2)) {
            std::string const& s1 = std::get<std::string>(v1);
            std::string const& s2 = std::get<std::string>(v2);
            switch (op) {
                case CompareOp::EQ: return s1 == s2;
                case CompareOp::NE: return s1 != s2;
                case CompareOp::GT: return s1 > s2;
                case CompareOp::LT: return s1 < s2;
                case CompareOp::GE: return s1 >= s2;
                case CompareOp::LE: return s1 <= s2;
                default: break;
            }
        }

        // Nil comparison
        if (std::holds_alternative<NilValue>(v1) || std::holds_alternative<NilValue>(v2)) {
            bool v1_is_nil = std::holds_alternative<NilValue>(v1);
            bool v2_is_nil = std::holds_alternative<NilValue>(v2);
            if (op == CompareOp::EQ)
                result = v1_is_nil == v2_is_nil;
            else if (op == CompareOp::NE)
                result = v1_is_nil != v2_is_nil;
            return result;
        }

        // Handle 'contains' and 'not contains' operators
        if (op == CompareOp::Contains || op == CompareOp::NotContains) {
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
            // Case 3: TypedList contains value
            else if (std::holds_alternative<std::shared_ptr<TypedList>>(v1)) {
                auto const& tl_ptr = std::get<std::shared_ptr<TypedList>>(v1);
                if (tl_ptr) {
                    for (auto const& val : tl_ptr->values) {
                        if (val == v2) {
                            contains_result = true;
                            break;
                        }
                    }
                }
            }
            // Case 4: ValueSet contains value
            else if (std::holds_alternative<std::shared_ptr<ValueSet>>(v1)) {
                auto const& vs_ptr = std::get<std::shared_ptr<ValueSet>>(v1);
                if (vs_ptr) {
                    contains_result = vs_ptr->values.find(v2) != vs_ptr->values.end();
                }
            }

            return (op == CompareOp::Contains) ? contains_result : !contains_result;
        }

        // Handle 'containsKey' and 'not containsKey' operators for Map
        if (op == CompareOp::ContainsKey || op == CompareOp::NotContainsKey) {
            bool contains_key_result = false;
            if (std::holds_alternative<std::shared_ptr<ValueMap>>(v1)) {
                auto const& vm_ptr = std::get<std::shared_ptr<ValueMap>>(v1);
                if (vm_ptr) {
                    contains_key_result = vm_ptr->entries.find(v2) != vm_ptr->entries.end();
                }
            }
            return (op == CompareOp::ContainsKey) ? contains_key_result : !contains_key_result;
        }

        // Handle 'matches' and 'not matches' operators (regex)
        if (op == CompareOp::Matches || op == CompareOp::NotMatches) {
            if (std::holds_alternative<std::string>(v1) && std::holds_alternative<std::string>(v2)) {
                std::string const& text = std::get<std::string>(v1);
                std::string const& pattern = std::get<std::string>(v2);
                try {
                    std::regex re = get_cached_regex(pattern);
                    bool matches = std::regex_search(text, re);
                    return (op == CompareOp::Matches) ? matches : !matches;
                } catch (std::regex_error const& e) {
                    loge("Invalid regex pattern '{}': {}", pattern, e.what());
                    return false;
                }
            }
            return false;
        }
        if (op == CompareOp::StartsWith) {
            if (std::holds_alternative<std::string>(v1) && std::holds_alternative<std::string>(v2)) {
                std::string const& text = std::get<std::string>(v1);
                std::string const& prefix = std::get<std::string>(v2);
                return text.length() >= prefix.length() &&
                       text.compare(0, prefix.length(), prefix) == 0;
            }
            return false;
        }
        if (op == CompareOp::EndsWith) {
            if (std::holds_alternative<std::string>(v1) && std::holds_alternative<std::string>(v2)) {
                std::string const& text = std::get<std::string>(v1);
                std::string const& suffix = std::get<std::string>(v2);
                return text.length() >= suffix.length() &&
                       text.compare(text.length() - suffix.length(), suffix.length(), suffix) == 0;
            }
            return false;
        }
        if (op == CompareOp::LengthIs) {
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
        if (op == CompareOp::MemberOf || op == CompareOp::NotMemberOf) {
            bool member_result = false;
            if (std::holds_alternative<FactList>(v2)) {
                member_result = fact_list_contains(std::get<FactList>(v2), v1);
            } else if (std::holds_alternative<std::shared_ptr<TypedList>>(v2)) {
                auto const& tl_ptr = std::get<std::shared_ptr<TypedList>>(v2);
                if (tl_ptr) {
                    for (auto const& val : tl_ptr->values) {
                        if (val == v1) {
                            member_result = true;
                            break;
                        }
                    }
                }
            } else if (std::holds_alternative<std::shared_ptr<ValueSet>>(v2)) {
                auto const& vs_ptr = std::get<std::shared_ptr<ValueSet>>(v2);
                if (vs_ptr) {
                    member_result = vs_ptr->values.find(v1) != vs_ptr->values.end();
                }
            }
            return (op == CompareOp::MemberOf) ? member_result : !member_result;
        }

        return false;
    }

    bool check_all_join_conditions(StatefulSession& session, Token const& token, Fact const& fact,
                                   std::vector<ParsedConstraint> const& joins,
                                   std::map<std::string, int> const& bindings) {
        g_join_checks.fetch_add(1, std::memory_order_relaxed);
        if (joins.empty()) { return true; }

        for (auto const& join : joins) {
            if (join.temporal_constraint) {
                auto const& tc = *join.temporal_constraint;
                auto lhs_val_opt = fact.get_field(tc.lhs_field);
                g_field_lookups.fetch_add(1, std::memory_order_relaxed);
                auto it = bindings.find(tc.rhs_binding_and_field.first);
                if (it == bindings.end()) return false;

                auto bound_fact = token.get_fact_at_depth(it->second);   // Get fact directly from token
                if (!bound_fact) return false;
                auto rhs_val_opt = bound_fact->get_field(tc.rhs_binding_and_field.second);
                g_field_lookups.fetch_add(1, std::memory_order_relaxed);

                if (!lhs_val_opt || !rhs_val_opt) return false;
                auto lhs_ts = std::get_if<int64_t>(&*lhs_val_opt);
                auto rhs_ts = std::get_if<int64_t>(&*rhs_val_opt);
                if (!lhs_ts || !rhs_ts) return false;
                bool pass = false;
                switch (tc.op) {
                    case TemporalOp::After:
                        pass = (*lhs_ts > *rhs_ts);
                        break;
                    case TemporalOp::Before:
                        pass = (*lhs_ts < *rhs_ts);
                        break;
                    case TemporalOp::Within:
                        pass = (std::abs(*lhs_ts - *rhs_ts) <= tc.window_ms);
                        break;
                    case TemporalOp::Coincides:
                        // For point events, coincides means equal timestamps
                        pass = (*lhs_ts == *rhs_ts);
                        break;
                    case TemporalOp::During:
                        // For point events, during means lhs is strictly between rhs bounds
                        // If we only have start timestamp, treat it as lhs > rhs.start
                        pass = (*lhs_ts > *rhs_ts);
                        break;
                    default:
                        break;
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
                if (!bound_fact) {
                    // Current-pattern binding can legally point past current token depth.
                    // In that case, resolve it against the currently tested fact.
                    if (it->second >= token.get_depth()) {
                        bound_fact = const_cast<Fact*>(&fact);
                    } else {
                        return false;
                    }
                }

                // Use cached path if available
                if (!join.cached_left_field_path.empty()) {
                    lhs_val_opt = bound_fact->get_field(join.cached_left_field_path);
                } else {
                    lhs_val_opt = bound_fact->get_field(join.left_field);
                }
                g_field_lookups.fetch_add(1, std::memory_order_relaxed);

            } else {
                if (!join.cached_left_field_path.empty()) {
                    lhs_val_opt = fact.get_field(join.cached_left_field_path);
                } else {
                    lhs_val_opt = fact.get_field(join.left_field);
                }
                g_field_lookups.fetch_add(1, std::memory_order_relaxed);
            }

            if (join.right_bound_field) {
                auto it = bindings.find(join.right_bound_field->first);
                if (it == bindings.end()) return false;

                auto bound_fact = token.get_fact_at_depth(it->second);
                if (!bound_fact) {
                    // Same fallback as LHS: allow current-pattern binding on RHS.
                    if (it->second >= token.get_depth()) {
                        bound_fact = const_cast<Fact*>(&fact);
                    } else {
                        return false;
                    }
                }

                // Use cached RHS path if available
                if (!join.cached_right_field_path.empty()) {
                    rhs_val_opt = bound_fact->get_field(join.cached_right_field_path);
                } else {
                    rhs_val_opt = bound_fact->get_field(join.right_bound_field->second);
                }
                g_field_lookups.fetch_add(1, std::memory_order_relaxed);

            } else if (join.compiled_expr) {
                // Evaluate compiled expression using token bindings
                auto resolver = make_join_resolver(token, fact, bindings);
                g_compiled_expr_evals.fetch_add(1, std::memory_order_relaxed);
                double result = join.compiled_expr->evaluate(resolver);
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

namespace rulesforge::rete_prof {
void reset_stats() {
    g_alpha_checks.store(0, std::memory_order_relaxed);
    g_join_checks.store(0, std::memory_order_relaxed);
    g_compare_calls.store(0, std::memory_order_relaxed);
    g_compiled_expr_evals.store(0, std::memory_order_relaxed);
    g_field_lookups.store(0, std::memory_order_relaxed);
}

Stats get_stats() {
    Stats s;
    s.alpha_checks = g_alpha_checks.load(std::memory_order_relaxed);
    s.join_checks = g_join_checks.load(std::memory_order_relaxed);
    s.compare_calls = g_compare_calls.load(std::memory_order_relaxed);
    s.compiled_expr_evals = g_compiled_expr_evals.load(std::memory_order_relaxed);
    s.field_lookups = g_field_lookups.load(std::memory_order_relaxed);
    return s;
}
}  // namespace rulesforge::rete_prof

// --- ReteNode ---
void ReteNode::add_child(std::shared_ptr<ReteNode> const& child) {
    if (child) {
        children.push_back(child);
        children_raw.push_back(child.get());
        child->parents.push_back(shared_from_this());
    }
}

void ReteNode::add_parent(std::shared_ptr<ReteNode> const& parent) {
    if (parent) {
        parents.push_back(parent);
    }
}

// --- BetaConditionNode ---
BetaConditionNode::BetaConditionNode(NodeKind k, std::vector<ParsedConstraint> const& joins,
                                     std::map<std::string, int> const& bindings) :
    ReteNode(k), join_constraints(joins), binding_to_token_idx(bindings) {
        // Pre-parse paths for join constraints
        for (auto& join : join_constraints) {
            if (join.left_field.find('.') != std::string::npos || join.left_field.find('[') != std::string::npos) {
                join.cached_left_field_path = parse_field_path(join.left_field);
            }
            if (join.right_bound_field) {
                auto const& r_field = join.right_bound_field->second;
                if (r_field.find('.') != std::string::npos || r_field.find('[') != std::string::npos) {
                    join.cached_right_field_path = parse_field_path(r_field);
                }
            }
        }
    }

void BetaConditionNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().beta_condition[mem_slot];

    if (token.type == PropagationType::RETRACT) {
        if (mem.left.erase(token.wme) > 0) {
            for (auto* child : children_raw) { child->left_activate(session, token); }
        }
        return;
    }

    size_t match_count = 0;
    for (auto const& [fact_id, fact] : mem.right) {
        if (check_all_join_conditions(session, token, *fact, join_constraints, binding_to_token_idx)) {
            match_count++;
        }
    }

    NetworkMemory::BetaConditionMem::LeftMemoryItem new_item;
    new_item.wme = token.wme;
    new_item.match_count = match_count;
    mem.left[token.wme] = new_item;

    if (condition_passes(match_count)) {
        for (auto* child : children_raw) { child->left_activate(session, token); }
    }
}

void BetaConditionNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().beta_condition[mem_slot];

    // If the fact is being retracted, we must first check if it was in our memory.
    if (p_type == PropagationType::RETRACT) {
        if (mem.right.erase(fact->id) == 0) {
            return;   // Fact was not in our memory, so no state change is possible.
        }
    } else {   // ASSERT or MODIFY
        mem.right[fact->id] = fact;
    }

    // Iterate over all tokens in the left memory to see which ones are affected by this fact.
    for (auto& [wme_ptr, item] : mem.left) {
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
            for (auto* c : children_raw) { c->left_activate(session, retract_token); }
        } else if (!was_passing_before && is_passing_now) {
            Token assert_token{item.wme, PropagationType::ASSERT};
            for (auto* c : children_raw) { c->left_activate(session, assert_token); }
        }
    }
}

void BetaConditionNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().beta_condition[mem_slot];

    // Store all facts into right memory first
    for (auto& fact : facts) {
        if (p_type == PropagationType::RETRACT) {
            mem.right.erase(fact->id);
        } else {
            mem.right[fact->id] = fact;
        }
    }

    // Single pass over left memory, checking all new facts per token
    for (auto& [wme_ptr, item] : mem.left) {
        Token token{item.wme, PropagationType::ASSERT};
        bool was_passing_before = was_passing(item.match_count);

        for (auto& fact : facts) {
            bool matches = check_all_join_conditions(session, token, *fact, join_constraints, binding_to_token_idx);
            if (!matches) continue;

            if (p_type == PropagationType::ASSERT) {
                item.match_count++;
            } else {
                item.match_count--;
            }
        }

        bool is_passing_now = condition_passes(item.match_count);

        // Propagate only if the state has changed (e.g., from passing to not passing).
        if (was_passing_before && !is_passing_now) {
            Token retract_token{item.wme, PropagationType::RETRACT};
            for (auto* c : children_raw) { c->left_activate(session, retract_token); }
        } else if (!was_passing_before && is_passing_now) {
            Token assert_token{item.wme, PropagationType::ASSERT};
            for (auto* c : children_raw) { c->left_activate(session, assert_token); }
        }
    }
}

void BetaConditionNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().beta_condition[mem_slot];
    mem.pending_facts.push_back(fact);
    mem.dirty = true;
}

void BetaConditionNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().beta_condition[mem_slot];
    mem.pending_facts.insert(mem.pending_facts.end(), facts.begin(), facts.end());
    mem.dirty = true;
}

bool BetaConditionNode::flush_pending(StatefulSession& session) {
    auto& mem = session.net_mem().beta_condition[mem_slot];
    if (!mem.dirty) return false;
    mem.dirty = false;
    auto pending = std::move(mem.pending_facts);
    mem.pending_facts.clear();
    right_activate_batch(session, pending, PropagationType::ASSERT);
    return true;
}

// --- AlphaNode ---
AlphaNode::AlphaNode(ParsedConstraint const& c) : ReteNode(NodeKind::Alpha), constraint(c) {
    // Pre-parse path if complex
    if (constraint.left_field.find('.') != std::string::npos || constraint.left_field.find('[') != std::string::npos) {
        constraint.cached_left_field_path = parse_field_path(constraint.left_field);
    }
}

void AlphaNode::left_activate(StatefulSession&, Token const&) {}

void AlphaNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    // Fast-path for MODIFY: if this alpha constraint is unaffected by changed fields,
    // skip re-evaluation and pass MODIFY through directly.
    if (p_type == PropagationType::MODIFY) {
        auto const* changed_fields = session.current_modified_fields();
        if (changed_fields && !alpha_constraint_affected(constraint, changed_fields)) {
            for (auto* child : children_raw) { child->right_activate(session, fact, PropagationType::MODIFY); }
            return;
        }
    }

    bool passes = check_constraint(*fact);
    if (passes) {
        for (auto* child : children_raw) { child->right_activate(session, fact, p_type); }
    }
}

void AlphaNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    if (p_type != PropagationType::ASSERT) {
        for (auto* fact : facts) {
            right_activate(session, fact, p_type);
        }
        return;
    }
    if (children_raw.size() == 1) {
        // Fast path: single child — filter in-place, zero allocation
        size_t write = 0;
        for (size_t read = 0; read < facts.size(); ++read) {
            if (check_constraint(*facts[read])) {
                facts[write++] = facts[read];
            }
        }
        if (write == 0) return;
        facts.resize(write);
        children_raw[0]->right_activate_batch(session, facts, p_type);
    } else {
        // Multiple children: need a copy
        std::vector<Fact*> survivors;
        survivors.reserve(facts.size());
        for (auto* fact : facts) {
            if (check_constraint(*fact)) {
                survivors.push_back(fact);
            }
        }
        if (survivors.empty()) return;
        for (auto* child : children_raw) { child->right_activate_batch(session, survivors, p_type); }
    }
}

void AlphaNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    if (p_type != PropagationType::ASSERT) {
        right_activate(session, fact, p_type);
        return;
    }
    if (check_constraint(*fact)) {
        for (auto* child : children_raw) { child->right_activate_deferred(session, fact, p_type); }
    }
}

void AlphaNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    if (p_type != PropagationType::ASSERT) {
        for (auto* fact : facts) {
            right_activate(session, fact, p_type);
        }
        return;
    }

    std::vector<Fact*> survivors;
    survivors.reserve(facts.size());
    for (auto* fact : facts) {
        if (check_constraint(*fact)) {
            survivors.push_back(fact);
        }
    }
    if (survivors.empty()) return;
    for (auto* child : children_raw) { child->right_activate_batch_deferred(session, survivors, p_type); }
}

bool AlphaNode::check_constraint(Fact const& fact) const {
    g_alpha_checks.fetch_add(1, std::memory_order_relaxed);
    // A constraint with an empty operator is a pure binding (like `$id: id`)
    // or an existence check (`name`).
    if (constraint.op == CompareOp::None) {
        // If there's no right literal, it's a pure binding. It should always pass
        // the alpha check, as the binding itself is handled elsewhere.
        if (!constraint.right_literal.has_value()) {
            return true; // Fast return (removed logd for perf)
        }
        // Otherwise, it's an existence check that was transformed to `field == 1`.
        // This will be handled by the main comparison logic below.
    }

    ConstraintValue const* lhs_ptr = nullptr;
    std::optional<ConstraintValue> temp_lhs; // Keep alive if returned by value from complex get_field

    if (constraint.cached_left_field_path.empty()) {
        // Fast path for simple fields
        if (constraint.left_field == "this") {
            temp_lhs = static_cast<int64_t>(fact.id);
            lhs_ptr = &*temp_lhs;
        } else {
            // Direct map lookup - zero copy, no string scanning
            auto it = fact.fields.find(constraint.left_field);
            g_field_lookups.fetch_add(1, std::memory_order_relaxed);
            if (it != fact.fields.end()) {
                lhs_ptr = &it->second;
            }
        }
    } else {
        // Complex path
        temp_lhs = fact.get_field(constraint.cached_left_field_path);
        g_field_lookups.fetch_add(1, std::memory_order_relaxed);
        if (temp_lhs) lhs_ptr = &*temp_lhs;
    }

    if (!lhs_ptr) {
        // Field not found - fail
        return false;
    }

    ConstraintValue const& lhs = *lhs_ptr;

    // Fast path for literal EQ/NE on common scalar types.
    if (constraint.right_literal.has_value() &&
        (constraint.op == CompareOp::EQ || constraint.op == CompareOp::NE)) {
        ConstraintValue const& rhs_lit = *constraint.right_literal;
        bool eq = false;
        if (std::holds_alternative<std::string>(lhs) && std::holds_alternative<std::string>(rhs_lit)) {
            eq = (std::get<std::string>(lhs) == std::get<std::string>(rhs_lit));
        } else if (std::holds_alternative<int64_t>(lhs) && std::holds_alternative<int64_t>(rhs_lit)) {
            eq = (std::get<int64_t>(lhs) == std::get<int64_t>(rhs_lit));
        } else if (std::holds_alternative<double>(lhs) && std::holds_alternative<double>(rhs_lit)) {
            eq = (std::get<double>(lhs) == std::get<double>(rhs_lit));
        } else if ((std::holds_alternative<int64_t>(lhs) && std::holds_alternative<double>(rhs_lit)) ||
                   (std::holds_alternative<double>(lhs) && std::holds_alternative<int64_t>(rhs_lit))) {
            double dl = std::holds_alternative<int64_t>(lhs)
                ? static_cast<double>(std::get<int64_t>(lhs))
                : std::get<double>(lhs);
            double dr = std::holds_alternative<int64_t>(rhs_lit)
                ? static_cast<double>(std::get<int64_t>(rhs_lit))
                : std::get<double>(rhs_lit);
            eq = (dl == dr);
        } else if (std::holds_alternative<NilValue>(lhs) && std::holds_alternative<NilValue>(rhs_lit)) {
            eq = true;
        } else {
            // Fallback to generic comparator for mixed/complex types.
            eq = compare_values(lhs, CompareOp::EQ, rhs_lit);
        }
        return (constraint.op == CompareOp::EQ) ? eq : !eq;
    }

    // Handle 'in' and 'not in' operators with value list
    if ((constraint.op == CompareOp::In || constraint.op == CompareOp::NotIn) && constraint.right_value_list.has_value()) {
        bool found = false;
        // Optimization: if value list is large, we should probably have pre-hashed it.
        // But for now, just linear scan is O(N).
        for (auto const& list_val : *constraint.right_value_list) {
            if (compare_values(lhs, CompareOp::EQ, list_val)) {
                found = true;
                break;
            }
        }
        return (constraint.op == CompareOp::In) ? found : !found;
    }

    // Handle arithmetic expressions on the RHS (e.g., `price > base * 1.2`)
    if (constraint.compiled_expr) {
        static std::map<std::string, std::string> empty_bindings;
        auto resolver = make_accumulate_resolver(fact, empty_bindings);
        g_compiled_expr_evals.fetch_add(1, std::memory_order_relaxed);
        double result = constraint.compiled_expr->evaluate(resolver);
        ConstraintValue rhs = result;
        return compare_values(lhs, constraint.op, rhs);
    }

    // Fast path: compare directly against pre-stored literal (zero copy)
    static const ConstraintValue nil_value{NilValue{}};
    ConstraintValue const& rhs = constraint.right_literal.has_value() ? *constraint.right_literal : nil_value;

    return compare_values(lhs, constraint.op, rhs);
}

void AlphaNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"AlphaNode (" << id << ")\\n";
    if (!constraint.left_field.empty()) {
        os << constraint.left_field << " " << compare_op_str(constraint.op) << " "
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

void EntryPointNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    for (auto* child : children_raw) { child->right_activate(session, fact, p_type); }
}

void EntryPointNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    for (auto* child : children_raw) { child->right_activate_batch(session, facts, p_type); }
}

void EntryPointNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    for (auto* child : children_raw) { child->right_activate_deferred(session, fact, p_type); }
}

void EntryPointNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    for (auto* child : children_raw) { child->right_activate_batch_deferred(session, facts, p_type); }
}

void EntryPointNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"Entry Point (" << id << ")\", shape=house, style=filled, fillcolor=yellow];";
}

// --- BaseJoinNode ---
BaseJoinNode::BaseJoinNode(NodeKind k, std::vector<ParsedConstraint> joins, std::map<std::string, int> bindings) :
    ReteNode(k), join_constraints_(std::move(joins)), binding_to_token_idx_(std::move(bindings)) {}

void BaseJoinNode::propagate_assert(StatefulSession& session, Token const& token, Fact* fact,
                                    ChildMap& left_to_children,
                                    RightChildMap& /*right_to_children*/) {
    auto new_wme = session.get_or_create_wme(token.wme, fact);
    left_to_children[token.wme].push_back(new_wme);
    // PHREAK: skip right_to_children — retract finds WMEs via left_to_children scan
    Token new_token{new_wme, PropagationType::ASSERT};
    for (auto* c : children_raw) { c->left_activate(session, new_token); }
}

void BaseJoinNode::propagate_retract(StatefulSession& session, TokenWME const* wme, Fact* fact,
                                     ChildMap& left_to_children,
                                     RightChildMap& /*right_to_children*/) {
    auto it_left = left_to_children.find(wme);
    if (it_left == left_to_children.end()) return;

    TokenWME const* child_to_retract = nullptr;
    for (auto const& child_wme : it_left->second) {
        if (child_wme->fact->id == fact->id) {
            child_to_retract = child_wme;
            break;
        }
    }

    if (child_to_retract) {
        Token retract_token{child_to_retract, PropagationType::RETRACT};
        for (auto* c : children_raw) { c->left_activate(session, retract_token); }
        remove_from_vector(it_left->second, child_to_retract);
        if (it_left->second.empty()) left_to_children.erase(it_left);

        // Invalidate WME cache so that subsequent ASSERT can create a fresh WME
        session.invalidate_wme_cache(child_to_retract->hash);
    }
}

// --- HashedJoinNode ---
HashedJoinNode::HashedJoinNode(std::vector<ParsedConstraint> joins,std::map<std::string, int> bindings,
                               std::pair<std::string, int> left_hash_key, std::string right_hash_key) :
    BaseJoinNode(NodeKind::HashedJoin, std::move(joins), std::move(bindings)), left_hash_key_(std::move(left_hash_key)),
    right_hash_key_(std::move(right_hash_key)) {}

std::optional<ConstraintValue> HashedJoinNode::get_key(Token const& token) const {
    auto fact_at_depth = token.get_fact_at_depth(left_hash_key_.second);
    if (!fact_at_depth) return std::nullopt;
    return fact_at_depth->get_field(left_hash_key_.first);
}

std::optional<ConstraintValue> HashedJoinNode::get_key(Fact const* fact) const {
    return fact->get_field(right_hash_key_);
}

void HashedJoinNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().hashed_join[mem_slot];
    auto key_opt = get_key(token);
    if (!key_opt) return;
    auto const& key = *key_opt;

    if (token.type == PropagationType::RETRACT) {
        auto mem_it = mem.left.find(key);
        if (mem_it != mem.left.end()) {
            remove_from_vector(mem_it->second, token.wme);
            if (mem_it->second.empty()) mem.left.erase(mem_it);
        }
        auto fact_it = mem.right.find(key);
        if (fact_it != mem.right.end()) {
            for (auto const& fact : fact_it->second) { propagate_retract(session, token.wme, fact, mem.left_to_children, mem.right_to_children); }
        }
        return;
    }

    mem.left[key].push_back(token.wme);
    auto it_right = mem.right.find(key);
    if (it_right != mem.right.end()) {
        for (auto const& fact : it_right->second) {
            if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_)) {
                propagate_assert(session, token, fact, mem.left_to_children, mem.right_to_children);
            }
        }
    }
}

void HashedJoinNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().hashed_join[mem_slot];
    auto key_opt = get_key(fact);
    if (!key_opt) return;
    auto const& key = *key_opt;

    if (p_type == PropagationType::RETRACT) {
        auto mem_it = mem.right.find(key);
        if (mem_it != mem.right.end()) {
            remove_from_vector(mem_it->second, fact);
            if (mem_it->second.empty()) mem.right.erase(mem_it);
        }
        auto token_it = mem.left.find(key);
        if (token_it != mem.left.end()) {
            for (auto const& wme : token_it->second) { propagate_retract(session, wme, fact, mem.left_to_children, mem.right_to_children); }
        }
        return;
    }

    // For MODIFY: first retract old matches, then assert new ones
    if (p_type == PropagationType::MODIFY) {
        auto mem_it = mem.right.find(key);
        if (mem_it != mem.right.end()) {
            // Check if fact is in the memory for this key
            auto fact_it = std::find(mem_it->second.begin(), mem_it->second.end(), fact);
            if (fact_it != mem_it->second.end()) {
                auto token_it = mem.left.find(key);
                if (token_it != mem.left.end()) {
                    for (auto const& wme : token_it->second) { propagate_retract(session, wme, fact, mem.left_to_children, mem.right_to_children); }
                }
                // Remove from memory - will be re-added below
                remove_from_vector(mem_it->second, fact);
                if (mem_it->second.empty()) mem.right.erase(mem_it);
            }
        }
    }

    mem.right[key].push_back(fact);
    auto it_left = mem.left.find(key);
    if (it_left != mem.left.end()) {
        for (auto const& wme : it_left->second) {
            Token token{wme, PropagationType::ASSERT};
            if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_)) {
                propagate_assert(session, token, fact, mem.left_to_children, mem.right_to_children);
            }
        }
    }
}

void HashedJoinNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().hashed_join[mem_slot];

    // Index all incoming facts by hash key, store into right memory
    std::unordered_map<ConstraintValue, std::vector<Fact*>, ConstraintValueHasher> facts_by_key;
    for (auto& fact : facts) {
        auto key_opt = get_key(fact);
        if (!key_opt) continue;
        mem.right[*key_opt].push_back(fact);
        facts_by_key[*key_opt].push_back(fact);
    }

    // Single pass over left memory per key bucket
    for (auto& [key, keyed_facts] : facts_by_key) {
        auto it_left = mem.left.find(key);
        if (it_left == mem.left.end()) continue;
        for (auto const& wme : it_left->second) {
            Token token{wme, PropagationType::ASSERT};
            for (auto& fact : keyed_facts) {
                if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_)) {
                    // Inline: uncached WME + skip right_to_children
                    auto new_wme = session.create_wme_uncached(token.wme, fact);
                    mem.left_to_children[token.wme].push_back(new_wme);
                    Token new_token{new_wme, PropagationType::ASSERT};
                    for (auto* c : children_raw) { c->left_activate(session, new_token); }
                }
            }
        }
    }
}

void HashedJoinNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().hashed_join[mem_slot];
    mem.pending_facts.push_back(fact);
    mem.dirty = true;
}

void HashedJoinNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().hashed_join[mem_slot];
    mem.pending_facts.insert(mem.pending_facts.end(), facts.begin(), facts.end());
    mem.dirty = true;
}

bool HashedJoinNode::flush_pending(StatefulSession& session) {
    auto& mem = session.net_mem().hashed_join[mem_slot];
    if (!mem.dirty) return false;
    mem.dirty = false;
    auto pending = std::move(mem.pending_facts);
    mem.pending_facts.clear();
    right_activate_batch(session, pending, PropagationType::ASSERT);
    return true;
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
CrossProductJoinNode::CrossProductJoinNode(std::vector<ParsedConstraint> joins,std::map<std::string, int> bindings) :
    BaseJoinNode(NodeKind::CrossProductJoin, std::move(joins), std::move(bindings)) {}

void CrossProductJoinNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];
    if (token.type == PropagationType::RETRACT) {
        if (mem.left.erase(token.wme) > 0) {
            for (auto const& [fact_id, fact] : mem.right) { propagate_retract(session, token.wme, fact, mem.left_to_children, mem.right_to_children); }
        }
        return;
    }

    mem.left[token.wme] = token.wme;
    for (auto const& [fact_id, fact] : mem.right) {
        if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_)) {
            propagate_assert(session, token, fact, mem.left_to_children, mem.right_to_children);
        }
    }
}

void CrossProductJoinNode::right_activate(StatefulSession& session, Fact* fact,
                                          PropagationType p_type) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];

    if (p_type == PropagationType::RETRACT) {
        if (mem.right.erase(fact->id) > 0) {
            for (auto const& [ptr, wme] : mem.left) { propagate_retract(session, wme, fact, mem.left_to_children, mem.right_to_children); }
        }
        return;
    }

    // For MODIFY: first retract old matches, then assert new ones
    if (p_type == PropagationType::MODIFY && mem.right.count(fact->id) > 0) {
        for (auto const& [ptr, wme] : mem.left) { propagate_retract(session, wme, fact, mem.left_to_children, mem.right_to_children); }
    }

    mem.right[fact->id] = fact;
    for (auto const& [ptr, wme] : mem.left) {
        Token token{wme, PropagationType::ASSERT};
        if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_)) {
            propagate_assert(session, token, fact, mem.left_to_children, mem.right_to_children);
        }
    }
}

void CrossProductJoinNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];

    // Store all facts into right memory first
    mem.right.reserve(mem.right.size() + facts.size());
    for (auto& fact : facts) {
        mem.right[fact->id] = fact;
    }

    // PHREAK: set-oriented propagation — batch WME creation + batch left_activate
    for (auto const& [ptr, wme] : mem.left) {
        Token token{wme, PropagationType::ASSERT};

        // Pre-reserve left_to_children vector for this token
        auto& child_vec = mem.left_to_children[token.wme];
        child_vec.reserve(child_vec.size() + facts.size());

        // Collect all matching tokens for batch propagation
        std::vector<Token> batch_tokens;
        batch_tokens.reserve(facts.size());

        for (auto& fact : facts) {
            if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_)) {
                auto new_wme = session.create_wme_uncached(token.wme, fact);
                child_vec.push_back(new_wme);
                batch_tokens.push_back(Token{new_wme, PropagationType::ASSERT});
            }
        }

        // Batch propagate to children
        if (!batch_tokens.empty()) {
            for (auto* c : children_raw) { c->left_activate_batch(session, batch_tokens); }
        }
    }
}

void CrossProductJoinNode::right_activate_deferred(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];
    mem.pending_facts.push_back(fact);
    mem.dirty = true;
}

void CrossProductJoinNode::right_activate_batch_deferred(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];
    mem.pending_facts.insert(mem.pending_facts.end(), facts.begin(), facts.end());
    mem.dirty = true;
}

bool CrossProductJoinNode::flush_pending(StatefulSession& session) {
    auto& mem = session.net_mem().cross_product_join[mem_slot];
    if (!mem.dirty) return false;
    mem.dirty = false;
    auto pending = std::move(mem.pending_facts);
    mem.pending_facts.clear();
    right_activate_batch(session, pending, PropagationType::ASSERT);
    return true;
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
NotNode::NotNode(std::vector<ParsedConstraint> const& joins,std::map<std::string, int> const& bindings) :
    BetaConditionNode(NodeKind::Not, joins, bindings) {}

void NotNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"NotNode (" << id << ")";
    if (!join_constraints.empty()) {
        os << "\\nJoins:";
        for (auto const& join : join_constraints) { os << "\\n" << constraint_to_string(join); }
    }
    os << "\", shape=octagon, style=filled, fillcolor=salmon];";
}

// --- ExistsNode ---
ExistsNode::ExistsNode(std::vector<ParsedConstraint> const& joins,std::map<std::string, int> const& bindings) :
    BetaConditionNode(NodeKind::Exists, joins, bindings) {}

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
        // If we have a compiled expression, evaluate it
        if (info.compiled_expr) {
            auto resolver = make_accumulate_resolver(fact, info.inline_binding_to_field);
            double result = info.compiled_expr->evaluate(resolver);
            return ConstraintValue{result};
        }
        // Otherwise use simple field lookup
        if (!info.accumulate_field_name.empty()) {
            return fact.get_field(info.accumulate_field_name);
        }
        return std::nullopt;
    }
}

#include "rete_node_accumulate.inc"

#include "rete_node_unnest.inc"

#include "rete_node_eval.inc"

#include "rete_node_terminal.inc"

#include "rete_node_query_terminal.inc"

#include "rete_node_query_input.inc"
