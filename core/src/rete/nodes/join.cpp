#include "diagnostics/logging_control.hpp"

#include "runtime/knowledge_base.hpp"
#include "rete/nodes/join.hpp"
#include "runtime/stateful_session.hpp"
#include "expression_descriptor.hpp"
#include "text/scalar_text.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iosfwd>
#include <chrono>

#include <sstream>
#include <span>
#include <stdexcept>
#include <typeinfo>
#include <utility>

using namespace rulesforge;

#include "rete/detail/evaluation.hpp"

using namespace rulesforge::rete_detail;

#include "diagnostics/rete_counters.hpp"

// --- AlphaNode ---
AlphaNode::AlphaNode(ParsedConstraint const& c) :
    AlphaNode(c, std::nullopt) {}

AlphaNode::AlphaNode(ParsedConstraint const& c,
                     std::optional<rulesforge::RuntimePredicateRef> runtime_predicate_ref) :
    ReteNode(NodeKind::Alpha), constraint(c),
    runtime_predicate_ref_(std::move(runtime_predicate_ref)) {
    // Pre-parse path if complex
    if (constraint.left_field.find('.') != std::string::npos || constraint.left_field.find('[') != std::string::npos) {
        constraint.cached_left_field_path = parse_field_path(constraint.left_field);
    }
}

void AlphaNode::left_activate(StatefulSession&, Token const&) {}

void AlphaNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    if (!fact) return;

    auto& mem = session.net_mem().alpha[mem_slot];
    bool const was_passing = mem.passing_facts.count(fact->id) > 0;

    if (p_type == PropagationType::RETRACT) {
        if (!was_passing) return;
        mem.passing_facts.erase(fact->id);
        for (auto* child : children_raw) { child->right_activate(session, fact, PropagationType::RETRACT); }
        return;
    }

    if (p_type == PropagationType::MODIFY) {
        auto const* changed_fields = session.current_modified_fields();
        if (changed_fields && !alpha_constraint_affected(constraint, changed_fields)) {
            if (!was_passing) return;
            for (auto* child : children_raw) { child->right_activate(session, fact, PropagationType::MODIFY); }
            return;
        }
    }

    bool const passes = check_constraint(session, *fact);
    if (!passes) {
        if (!was_passing) return;
        mem.passing_facts.erase(fact->id);
        for (auto* child : children_raw) { child->right_activate(session, fact, PropagationType::RETRACT); }
        return;
    }

    mem.passing_facts.insert(fact->id);
    PropagationType child_type = p_type;
    if (p_type == PropagationType::MODIFY && !was_passing) {
        child_type = PropagationType::ASSERT;
    }
    for (auto* child : children_raw) { child->right_activate(session, fact, child_type); }
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
        auto& mem = session.net_mem().alpha[mem_slot];
        size_t write = 0;
        for (size_t read = 0; read < facts.size(); ++read) {
            if (check_constraint(session, *facts[read])) {
                mem.passing_facts.insert(facts[read]->id);
                facts[write++] = facts[read];
            }
        }
        if (write == 0) return;
        facts.resize(write);
        children_raw[0]->right_activate_batch(session, facts, p_type);
    } else {
        // Multiple children: need a copy
        auto& mem = session.net_mem().alpha[mem_slot];
        std::vector<Fact*> survivors;
        survivors.reserve(facts.size());
        for (auto* fact : facts) {
            if (check_constraint(session, *fact)) {
                mem.passing_facts.insert(fact->id);
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
    if (check_constraint(session, *fact)) {
        session.net_mem().alpha[mem_slot].passing_facts.insert(fact->id);
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
    auto& mem = session.net_mem().alpha[mem_slot];
    for (auto* fact : facts) {
        if (check_constraint(session, *fact)) {
            mem.passing_facts.insert(fact->id);
            survivors.push_back(fact);
        }
    }
    if (survivors.empty()) return;
    for (auto* child : children_raw) { child->right_activate_batch_deferred(session, survivors, p_type); }
}

bool AlphaNode::check_constraint(StatefulSession const& session, Fact const& fact) const {
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
    auto kb = session.get_knowledge_base();

    if (!runtime_predicate_ref_) {
        static const ConstraintValue nil_value{NilValue{}};
        std::optional<ConstraintValue> rhs_expr;
        if (constraint.right_arith_expr) {
            rhs_expr = evaluate_runtime_expression(constraint.right_arith_program, fact, nullptr, nullptr);
            if (!rhs_expr) return false;
        }
        ConstraintValue const& rhs = rhs_expr.has_value()
            ? *rhs_expr
            : (constraint.right_literal.has_value() ? *constraint.right_literal : nil_value);
        return evaluate_compare(constraint.op, lhs, rhs);
    }

    if (constraint.op == CompareOp::In || constraint.op == CompareOp::NotIn) {
        return evaluate_literal_membership(constraint, lhs);
    }
    if (!kb || runtime_predicate_ref_->external_name.empty()) {
        static const ConstraintValue nil_value{NilValue{}};
        std::optional<ConstraintValue> rhs_expr;
        if (constraint.right_arith_expr) {
            rhs_expr = evaluate_runtime_expression(constraint.right_arith_program, fact, nullptr, nullptr);
            if (!rhs_expr) return false;
        }
        ConstraintValue const& rhs = rhs_expr.has_value()
            ? *rhs_expr
            : (constraint.right_literal.has_value() ? *constraint.right_literal : nil_value);
        return evaluate_compare(constraint.op, lhs, rhs);
    }

    switch (runtime_predicate_ref_->kind) {
        case rulesforge::RuntimePredicateKind::NumericLiteral:
        case rulesforge::RuntimePredicateKind::ValueList: {
            auto result = kb->runtime_predicate(*runtime_predicate_ref_, {lhs});
            if (!result) {
                throw_lhs_runtime_predicate_failed(*runtime_predicate_ref_, "alpha unary predicate");
            }
            return *result;
        }
        case rulesforge::RuntimePredicateKind::CollectionContains:
            if (constraint.right_value_list) {
                auto rhs_list = make_typed_list(*constraint.right_value_list);
                auto result = kb->runtime_predicate(*runtime_predicate_ref_, {lhs, rhs_list});
                if (!result) {
                    throw_lhs_runtime_predicate_failed(
                        *runtime_predicate_ref_,
                        "alpha collection value-list predicate");
                }
                return *result;
            }
            {
                static const ConstraintValue nil_value{NilValue{}};
                ConstraintValue const& rhs = constraint.right_literal.has_value() ? *constraint.right_literal : nil_value;
                auto result = kb->runtime_predicate(*runtime_predicate_ref_, {lhs, rhs});
                if (!result) {
                    throw_lhs_runtime_predicate_failed(*runtime_predicate_ref_, "alpha binary predicate");
                }
                return *result;
            }
        default: {
            static const ConstraintValue nil_value{NilValue{}};
            ConstraintValue const& rhs = constraint.right_literal.has_value() ? *constraint.right_literal : nil_value;
            auto result = kb->runtime_predicate(*runtime_predicate_ref_, {lhs, rhs});
            if (!result) {
                throw_lhs_runtime_predicate_failed(*runtime_predicate_ref_, "alpha binary predicate");
            }
            return *result;
        }
    }
}

void AlphaNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"AlphaNode (" << id << ")\\n";
    if (!constraint.left_field.empty()) {
        os << constraint.left_field << " " << compare_op_str(constraint.op) << " "
           << ::to_string(constraint.right_literal.value_or(NilValue{}));
        if (runtime_predicate_ref_) {
            os << "\\nRuntime predicate: " << static_cast<int>(runtime_predicate_ref_->kind)
               << "#" << runtime_predicate_ref_->predicate_id;
        }
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

// --- HashedJoinNode ---
HashedJoinNode::HashedJoinNode(std::vector<ParsedConstraint> joins,
                               std::map<std::string, int> bindings,
                               std::map<std::string, std::string> scalar_binding_fields,
                               std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates,
                               std::pair<std::string, int> left_hash_key, std::string right_hash_key) :
    BaseJoinNode(NodeKind::HashedJoin,
                 std::move(joins),
                 std::move(bindings),
                 std::move(scalar_binding_fields),
                 std::move(runtime_predicates)),
    left_hash_key_(std::move(left_hash_key)),
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
            if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                          scalar_binding_to_field_, &runtime_predicates_)) {
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
            if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                          scalar_binding_to_field_, &runtime_predicates_)) {
                propagate_assert(session, token, fact, mem.left_to_children, mem.right_to_children);
            }
        }
    }
}

void HashedJoinNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    auto& mem = session.net_mem().hashed_join[mem_slot];

    // Index all incoming facts by hash key, store into right memory
    std::unordered_map<ConstraintValue, std::vector<Fact*>, ConstraintValueHasher, ConstraintValueEquals> facts_by_key;
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
                if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                              scalar_binding_to_field_, &runtime_predicates_)) {
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
        print_join_constraints_with_runtime_predicates(os,
                                            join_constraints_,
                                            runtime_predicates_);
    }
    os << "\", shape=box, style=filled, fillcolor=lightblue];";
}

// --- CrossProductJoinNode ---
CrossProductJoinNode::CrossProductJoinNode(std::vector<ParsedConstraint> joins,
                                           std::map<std::string, int> bindings,
                                           std::map<std::string, std::string> scalar_binding_fields,
                                           std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates) :
    BaseJoinNode(NodeKind::CrossProductJoin, std::move(joins), std::move(bindings), std::move(scalar_binding_fields),
                 std::move(runtime_predicates)) {}

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
        if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                      scalar_binding_to_field_, &runtime_predicates_)) {
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
        if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                      scalar_binding_to_field_, &runtime_predicates_)) {
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
            if (check_all_join_conditions(session, token, *fact, join_constraints_, binding_to_token_idx_,
                                          scalar_binding_to_field_, &runtime_predicates_)) {
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
        print_join_constraints_with_runtime_predicates(os,
                                            join_constraints_,
                                            runtime_predicates_);
    }
    os << "\", shape=box, style=filled, fillcolor=lightgrey];";
}

// --- NotNode ---
NotNode::NotNode(std::vector<ParsedConstraint> const& joins,
                 std::map<std::string, int> const& bindings,
                 std::map<std::string, std::string> scalar_binding_fields,
                 std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates_) :
    BetaConditionNode(NodeKind::Not,
                      joins,
                      bindings,
                      std::move(scalar_binding_fields),
                      std::move(runtime_predicates_)) {}

void NotNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"NotNode (" << id << ")";
    if (!join_constraints.empty()) {
        os << "\\nJoins:";
        print_join_constraints_with_runtime_predicates(os,
                                            join_constraints,
                                            runtime_predicates);
    }
    os << "\", shape=octagon, style=filled, fillcolor=salmon];";
}

// --- ExistsNode ---
ExistsNode::ExistsNode(std::vector<ParsedConstraint> const& joins,
                       std::map<std::string, int> const& bindings,
                       std::map<std::string, std::string> scalar_binding_fields,
                       std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates_) :
    BetaConditionNode(NodeKind::Exists,
                      joins,
                      bindings,
                      std::move(scalar_binding_fields),
                      std::move(runtime_predicates_)) {}

void ExistsNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"ExistsNode (" << id << ")";
    if (!join_constraints.empty()) {
        os << "\\nJoins:";
        print_join_constraints_with_runtime_predicates(os,
                                            join_constraints,
                                            runtime_predicates);
    }
    os << "\", shape=octagon, style=filled, fillcolor=khaki];";
}
