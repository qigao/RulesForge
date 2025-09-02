#include "pubcxx/logger.hpp"

#include "knowledge_base.hpp"
#include "rete/rete_node.hpp"
#include "stateful_session.hpp"

#include <algorithm>
#include <iostream>
#include <magic_enum/magic_enum.hpp>
#include <sstream>
#include <typeinfo>
#include <utility> 

// --- Helper Functions ---
namespace {
    std::string constraint_to_string(ParsedConstraint const& join) {
        std::ostringstream oss;
        if (join.temporal_constraint) {
            auto const& tc = *join.temporal_constraint;
            oss << "temporal " << tc.lhs_field << " " << tc.op << " " << tc.rhs_binding_and_field.first << "."
                << tc.rhs_binding_and_field.second;
            if (tc.op == "within") { oss << " " << tc.window_ms << "ms"; }
            return oss.str();
        }

        if (join.left_binding) {
            oss << *join.left_binding;
        } else {
            oss << "fact";
        }
        oss << "." << join.left_field << " " << join.op << " ";

        if (join.right_bound_field) {
            oss << join.right_bound_field->first << "." << join.right_bound_field->second;
        } else if (join.right_literal) {
            oss << ::to_string(*join.right_literal);
        }
        return oss.str();
    }

    bool compare_values(ConstraintValue const& v1, std::string const& op, ConstraintValue const& v2) {
        LOG_TRACE("      compare_values: {} {} {}", ::to_string(v1), op, ::to_string(v2));

        bool result = false;
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
            LOG_TRACE("        -> Arithmetic comparison result: {}", result);

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
        if (it != vec.end()) { vec.erase(it); }
    }
}   // namespace

// --- ReteNode ---
void ReteNode::add_child(std::shared_ptr<ReteNode> child) {
    if (child) {
        LOG_DEBUG("Linking parent node {} to child node {}", this->id, child->id);
        children.push_back(child);
        child->add_parent(shared_from_this());
    }
}

void ReteNode::add_parent(std::shared_ptr<ReteNode> parent) {
    if (parent) {
        LOG_DEBUG("Linking child node {} to parent node {}", this->id, parent->id);
        parents.push_back(parent);
    }
}

// --- BetaConditionNode ---
BetaConditionNode::BetaConditionNode(std::vector<ParsedConstraint> const& joins,
                                     map<std::string, int> const& bindings) :
    join_constraints(joins), binding_to_token_idx(bindings) {}

void BetaConditionNode::left_activate(StatefulSession& session, std::shared_ptr<Token> token) {
    LOG_DEBUG("Node {}:{} left_activate. Token depth {}, type {}", this->id, typeid(*this).name(), token->wme->depth,
              magic_enum::enum_name(token->type));
    auto const& wme = token->wme;
    if (token->type == PropagationType::RETRACT) {
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
        if (check_all_join_conditions(session, *token, *fact, join_constraints, binding_to_token_idx)) {
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
    LOG_DEBUG("Node {}:{} right_activate. Fact ID {}, type {}", this->id, typeid(*this).name(), fact->id,
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
        auto token = std::make_shared<Token>(item.wme, PropagationType::ASSERT);

        // Check if the arriving fact matches the conditions for the current token.
        bool matches = check_all_join_conditions(session, *token, *fact, join_constraints, binding_to_token_idx);

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
            auto retract_token = std::make_shared<Token>(item.wme, PropagationType::RETRACT);
            for (auto& weak_child : children) {
                if (auto child = weak_child.lock()) child->left_activate(session, retract_token);
            }
        } else if (!was_passing_before && is_passing_now) {
            auto assert_token = std::make_shared<Token>(item.wme, PropagationType::ASSERT);
            for (auto& weak_child : children) {
                if (auto child = weak_child.lock()) child->left_activate(session, assert_token);
            }
        }
    }
}

// --- AlphaNode ---
AlphaNode::AlphaNode(ParsedConstraint const& c) : constraint(c) {}

void AlphaNode::left_activate(StatefulSession&, std::shared_ptr<Token>) {}

void AlphaNode::right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) {
    bool passes = check_constraint(*fact);
    LOG_DEBUG("Node {}:AlphaNode right_activate. Fact ID {}. Constraint check: {}", this->id, fact->id,
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
            LOG_DEBUG("  -> AlphaNode ID {} passing pure binding on field '{}'.", this->id, constraint.left_field);
            return true;
        }
        // Otherwise, it's an existence check that was transformed to `field == 1`.
        // This will be handled by the main comparison logic below.
    }

    auto fact_val_opt = fact.get_field(constraint.left_field);
    if (!fact_val_opt) {
        LOG_DEBUG("  -> AlphaNode ID {} check FAILED: field '{}' not found on fact.", this->id, constraint.left_field);
        return false;
    }

    ConstraintValue const& lhs = *fact_val_opt;
    ConstraintValue const& rhs = constraint.right_literal.value_or(NilValue{});
    bool result = compare_values(lhs, constraint.op, rhs);

    LOG_DEBUG("  -> AlphaNode ID {} checking: LHS: {} (type {}) {} RHS: {} (type {}) -> {}", this->id, to_string(lhs),
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
void EntryPointNode::left_activate(StatefulSession&, std::shared_ptr<Token>) {
    // An EntryPointNode is the start of an alpha chain. It does not receive left activations.
}

void EntryPointNode::right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) {
    LOG_DEBUG("Node {}:EntryPointNode right_activate. Fact ID {}, type {}", this->id, fact->id,
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

void BaseJoinNode::propagate_assert(StatefulSession& session, std::shared_ptr<Token> token,
                                    std::shared_ptr<Fact> fact) {
    auto new_wme = session.get_or_create_wme(token->wme, fact);
    left_to_children_[token->wme.get()].push_back(new_wme);
    right_to_children_[fact->id].push_back(new_wme);
    auto new_token = std::make_shared<Token>(new_wme, PropagationType::ASSERT);
    LOG_DEBUG("Node {}:{} propagating ASSERT. Old token depth {}, new token depth {}", this->id, typeid(*this).name(),
              token->wme->depth, new_wme->depth);
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
        LOG_DEBUG("Node {}:{} propagating RETRACT. Old token depth {}, fact ID {}", this->id, typeid(*this).name(),
                  wme->depth, fact->id);
        auto retract_token = std::make_shared<Token>(child_to_retract, PropagationType::RETRACT);
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
    }
}

// --- HashedJoinNode ---
HashedJoinNode::HashedJoinNode(std::vector<ParsedConstraint> joins, map<std::string, int> bindings,
                               std::pair<std::string, int> left_hash_key, std::string right_hash_key) :
    BaseJoinNode(std::move(joins), std::move(bindings)), left_hash_key_(std::move(left_hash_key)),
    right_hash_key_(std::move(right_hash_key)) {}

std::optional<ConstraintValue> HashedJoinNode::get_key(std::shared_ptr<Token> const& token) const {
    auto fact_at_depth = token->get_fact_at_depth(left_hash_key_.second);
    if (!fact_at_depth) return std::nullopt;
    return fact_at_depth->get_field(left_hash_key_.first);
}

std::optional<ConstraintValue> HashedJoinNode::get_key(std::shared_ptr<Fact> const& fact) const {
    return fact->get_field(right_hash_key_);
}

void HashedJoinNode::left_activate(StatefulSession& session, std::shared_ptr<Token> token) {
    LOG_DEBUG("Node {}:HashedJoinNode left_activate. Token depth {}, type {}", this->id, token->wme->depth,
              magic_enum::enum_name(token->type));
    auto key_opt = get_key(token);
    if (!key_opt) return;
    auto const& key = *key_opt;
    LOG_DEBUG("  -> Left key: {}", ::to_string(key));

    if (token->type == PropagationType::RETRACT) {
        auto mem_it = left_memory_.find(key);
        if (mem_it != left_memory_.end()) {
            remove_from_vector(mem_it->second, token->wme);
            if (mem_it->second.empty()) left_memory_.erase(mem_it);
        }
        auto fact_it = right_memory_.find(key);
        if (fact_it != right_memory_.end()) {
            for (auto const& fact : fact_it->second) { propagate_retract(session, token->wme, fact); }
        }
        return;
    }

    left_memory_[key].push_back(token->wme);
    auto it_right = right_memory_.find(key);
    if (it_right != right_memory_.end()) {
        LOG_DEBUG("  -> Found {} matching facts in right memory.", it_right->second.size());
        for (auto const& fact : it_right->second) {
            if (check_all_join_conditions(session, *token, *fact, join_constraints_, binding_to_token_idx_)) {
                propagate_assert(session, token, fact);
            }
        }
    }
}

void HashedJoinNode::right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) {
    LOG_DEBUG("Node {}:HashedJoinNode right_activate. Fact ID {}, type {}", this->id, fact->id,
              magic_enum::enum_name(p_type));
    auto key_opt = get_key(fact);
    if (!key_opt) return;
    auto const& key = *key_opt;
    LOG_DEBUG("  -> Right key: {}", ::to_string(key));

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

    right_memory_[key].push_back(fact);
    auto it_left = left_memory_.find(key);
    if (it_left != left_memory_.end()) {
        LOG_DEBUG("  -> Found {} matching tokens in left memory.", it_left->second.size());
        for (auto const& wme : it_left->second) {
            auto token = std::make_shared<Token>(wme, PropagationType::ASSERT);
            if (check_all_join_conditions(session, *token, *fact, join_constraints_, binding_to_token_idx_)) {
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

void CrossProductJoinNode::left_activate(StatefulSession& session, std::shared_ptr<Token> token) {
    LOG_DEBUG("Node {}:CrossProductJoinNode left_activate. Token depth {}, type {}", this->id, token->wme->depth,
              magic_enum::enum_name(token->type));
    if (token->type == PropagationType::RETRACT) {
        if (left_memory_.erase(token->wme.get()) > 0) {
            LOG_DEBUG("  -> Retracted token from left memory. Propagating retract to {} children.", children.size());
            for (auto const& [id, fact] : right_memory_) { propagate_retract(session, token->wme, fact); }
        }
        return;
    }

    left_memory_[token->wme.get()] = token->wme;
    for (auto const& [id, fact] : right_memory_) {
        if (check_all_join_conditions(session, *token, *fact, join_constraints_, binding_to_token_idx_)) {
            propagate_assert(session, token, fact);
        }
    }
}

void CrossProductJoinNode::right_activate(StatefulSession& session, std::shared_ptr<Fact> fact,
                                          PropagationType p_type) {
    LOG_DEBUG("Node {}:CrossProductJoinNode right_activate. Fact ID {}, type {}", this->id, fact->id,
              magic_enum::enum_name(p_type));

    if (p_type == PropagationType::RETRACT) {
        if (right_memory_.erase(fact->id) > 0) {
            LOG_DEBUG("  -> Retracted fact from right memory. Propagating retract to {} tokens in left memory.",
                      left_memory_.size());
            for (auto const& [ptr, wme] : left_memory_) { propagate_retract(session, wme, fact); }
        }
        return;
    }

    right_memory_[fact->id] = fact;
    for (auto const& [ptr, wme] : left_memory_) {
        auto token = std::make_shared<Token>(wme, PropagationType::ASSERT);
        if (check_all_join_conditions(session, *token, *fact, join_constraints_, binding_to_token_idx_)) {
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

// --- OrderedJoinNode ---
OrderedJoinNode::OrderedJoinNode(std::vector<ParsedConstraint> joins, map<std::string, int> bindings) :
    BaseJoinNode(std::move(joins), std::move(bindings)) {}

void OrderedJoinNode::left_activate(StatefulSession& session, std::shared_ptr<Token> token) {
    // TBD: For now, acts as a cross-product.
}

void OrderedJoinNode::right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) {
    // TBD
}

void OrderedJoinNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"OrderedJoinNode (" << id << ", TBD)";
    if (!join_constraints_.empty()) {
        os << "\\nJoins:";
        for (auto const& join : join_constraints_) { os << "\\n" << constraint_to_string(join); }
    }
    os << "\", shape=box, style=filled, fillcolor=moccasin];";
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
AccumulateNode::AccumulateNode(IAccumulator const* prototype, ParsedAccumulate&& accumulate_info,
                               std::string res_fact_type, map<std::string, int> bindings,
                               std::vector<ParsedConstraint> joins) :
    accumulator_prototype(prototype), info(std::move(accumulate_info)), result_fact_type(std::move(res_fact_type)),
    binding_to_token_idx(std::move(bindings)), join_constraints(std::move(joins)) {}

void AccumulateNode::left_activate(StatefulSession& session, std::shared_ptr<Token> token) {
    LOG_DEBUG("Node {}:AccumulateNode left_activate. Token depth {}, type {}", this->id, token->wme->depth,
              magic_enum::enum_name(token->type));
    auto const& wme = token->wme;
    if (token->type == PropagationType::RETRACT) {
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
        if (check_all_join_conditions(session, *token, *fact_ptr, join_constraints, binding_to_token_idx)) {
            if (is_collect_set) {
                new_item.contributing_facts_set.insert(fact_ptr);
            } else if (is_collect_list) {
                new_item.contributing_facts_list.push_back(fact_ptr);
            } else {
                if (auto value_opt = fact_ptr->get_field(info.accumulate_field_name)) {
                    new_item.accumulator->accumulate(*value_opt);
                }
            }
        }
    }
    update_and_propagate_result(session, new_item);   // This will add the fact
    left_memory[wme.get()] = std::move(new_item);
}

void AccumulateNode::right_activate(StatefulSession& session, std::shared_ptr<Fact> fact, PropagationType p_type) {
    LOG_DEBUG("Node {}:AccumulateNode right_activate. Fact ID {}, type {}", this->id, fact->id,
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
            auto current_token = std::make_shared<Token>(item.wme, PropagationType::ASSERT);
            item.accumulator->clear();
            item.contributing_facts_list.clear();
            item.contributing_facts_set.clear();
            for (auto const& [id, f_ptr] : right_memory) {
                if (check_all_join_conditions(session, *current_token, *f_ptr, join_constraints,
                                              binding_to_token_idx)) {
                    if (is_collect_set) {
                        item.contributing_facts_set.insert(f_ptr);
                    } else if (is_collect_list) {
                        item.contributing_facts_list.push_back(f_ptr);
                    } else {
                        if (auto value_opt = f_ptr->get_field(info.accumulate_field_name)) {
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
                auto token = std::make_shared<Token>(item.wme, PropagationType::ASSERT);
                if (check_all_join_conditions(session, *token, *fact, join_constraints, binding_to_token_idx)) {
                    if (auto value_opt = fact->get_field(info.accumulate_field_name)) {
                        item.accumulator->accumulate(*value_opt);
                        update_and_propagate_result(session, item);
                    }
                }
            }
        } else {
            if (right_memory.count(fact->id) == 0) return;
            for (auto& [wme_ptr, item] : left_memory) {
                auto token = std::make_shared<Token>(item.wme, PropagationType::ASSERT);
                if (check_all_join_conditions(session, *token, *fact, join_constraints, binding_to_token_idx)) {
                    if (auto value_opt = fact->get_field(info.accumulate_field_name)) {
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
            f.fields["result"] = item.accumulator->get_result();
        }
    };
    if (is_new_fact) {
        modifier(*item.result_fact);
        session.add_fact(item.result_fact);
        auto result_wme = session.get_or_create_wme(item.wme, item.result_fact);
        auto assert_token = std::make_shared<Token>(result_wme, PropagationType::ASSERT);
        LOG_DEBUG("  -> Accumulate created new result fact ID {}, propagating.", item.result_fact->id);
        for (auto& weak_child : children) {
            if (auto c = weak_child.lock()) c->left_activate(session, assert_token);
        }
    } else {
        LOG_DEBUG("  -> Accumulate updating existing result fact ID {}.", item.result_fact->id);
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

void UnnestNode::left_activate(StatefulSession& session, std::shared_ptr<Token> token) {
    LOG_DEBUG("Node {}:UnnestNode left_activate. Token depth {}, type {}", this->id, token->wme->depth,
              magic_enum::enum_name(token->type));
    auto const& wme = token->wme;
    if (token->type == PropagationType::RETRACT) {
        auto it = parent_to_children_map.find(wme.get());
        if (it != parent_to_children_map.end()) {
            for (auto const& child_wme : it->second.second) {
                auto child_token = std::make_shared<Token>(child_wme, PropagationType::RETRACT);
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
    auto source_fact = token->get_fact_at_depth(token_idx);
    if (!source_fact) return;
    auto collection_opt = source_fact->get_field(info.source_field);
    if (collection_opt && std::holds_alternative<FactList>(*collection_opt)) {
        auto const& list = std::get<FactList>(*collection_opt).facts;
        if (list.empty()) return;
        LOG_DEBUG("  -> Unnesting {} items from {}.{}", list.size(), info.source_binding, info.source_field);
        std::vector<std::shared_ptr<TokenWME const>> new_child_wmes;
        new_child_wmes.reserve(list.size());
        for (auto const& item_fact : list) {
            auto new_wme = session.get_or_create_wme(wme, item_fact);
            new_child_wmes.push_back(new_wme);
            auto new_token = std::make_shared<Token>(new_wme, PropagationType::ASSERT);
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

void EvalNode::left_activate(StatefulSession& session, std::shared_ptr<Token> token) {
    auto const& wme = token->wme;
    LOG_DEBUG("Node {}:EvalNode left_activate. Token depth {}, type {}", this->id, wme->depth,
              magic_enum::enum_name(token->type));
    if (token->type == PropagationType::RETRACT) {
        if (memory.erase(wme.get()) > 0) {
            LOG_DEBUG("  -> Retracted token from memory. Propagating retract to children.");
            for (auto& weak_child : children) {
                if (auto c = weak_child.lock()) c->left_activate(session, token);
            }
        }
        return;
    }
    bool result = session.execute_eval(expression, *token, binding_to_token_idx);
    LOG_DEBUG("  -> Eval expression '{}' result: {}", expression, result);
    if (result) {
        memory[wme.get()] = wme;
        LOG_DEBUG("  -> Eval passed. Propagating assert to children.");
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

void TerminalNode::left_activate(StatefulSession& session, std::shared_ptr<Token> token) {
    LOG_DEBUG("Node {}:TerminalNode left_activate for rule '{}'. Token type: {}", this->id, rule_name,
              magic_enum::enum_name(token->type));
    auto wme_ptr = token->wme.get();

    auto const* rule = session.get_knowledge_base()->find_rule_by_name(rule_name);
    if (!rule) return;   // Should not happen in a valid network

    if (token->type == PropagationType::RETRACT) {
        if (memory_.erase(wme_ptr) > 0) {
            size_t hash = std::hash<TokenWME const*>{}(wme_ptr) ^ reinterpret_cast<uintptr_t>(rule);
            session.remove_activation(hash);
            for (auto& listener : session.get_listeners()) {
                listener->on_activation_retracted(rule->name, token->get_facts());
            }
        }
        session.logical_retract(wme_ptr);
    } else if (token->type == PropagationType::ASSERT) {
        if (memory_.find(wme_ptr) == memory_.end()) {
            memory_.insert(wme_ptr);
            size_t hash = std::hash<TokenWME const*>{}(wme_ptr) ^ reinterpret_cast<uintptr_t>(rule);
            Activation activation{rule, token, hash, this->binding_to_token_idx};
            session.add_activation(activation);
            // Notify listeners about creation
            for (auto& listener : session.get_listeners()) {
                listener->on_activation_created(rule->name, token->get_facts());
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

void QueryTerminalNode::left_activate(StatefulSession& session, std::shared_ptr<Token> token) {
    LOG_DEBUG("Node {}:QueryTerminalNode left_activate. Token type: {}", this->id, magic_enum::enum_name(token->type));
    auto const& wme = token->wme;
    if (token->type == PropagationType::ASSERT) {
        results[wme.get()] = token;
        LOG_DEBUG("  -> Added token to query results. Total results: {}", results.size());
    } else {
        results.erase(wme.get());
        LOG_DEBUG("  -> Removed token from query results. Total results: {}", results.size());
    }
}

void QueryTerminalNode::clear_results() {
    LOG_DEBUG("Node {}:QueryTerminalNode clearing results.", this->id);
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
    LOG_DEBUG("Node {}:QueryInputNode execute with {} args.", this->id, args.size());
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
    auto initial_token = std::make_shared<Token>(current_wme, PropagationType::ASSERT);
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
