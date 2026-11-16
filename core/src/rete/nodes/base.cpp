#include "diagnostics/logging_control.hpp"

#include "runtime/knowledge_base.hpp"
#include "rete/nodes/base.hpp"
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
                                     std::map<std::string, int> const& bindings,
                                     std::map<std::string, std::string> scalar_binding_fields,
                                     std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates_) :
    ReteNode(k),
    join_constraints(joins),
    binding_to_token_idx(bindings),
    scalar_binding_to_field(std::move(scalar_binding_fields)),
    runtime_predicates(std::move(runtime_predicates_)) {
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
                if (!left_hash_key_ && join.op == CompareOp::EQ) {
                    auto const binding = binding_to_token_idx.find(join.right_bound_field->first);
                    if (binding != binding_to_token_idx.end()) {
                        left_hash_key_ = std::pair{r_field, binding->second};
                        right_hash_key_ = join.left_field;
                    }
                }
            }
        }
    }

std::optional<ConstraintValue> BetaConditionNode::get_hash_key(Token const& token) const {
    if (!left_hash_key_) return std::nullopt;
    auto const* fact = token.get_fact_at_depth(left_hash_key_->second);
    if (!fact) return std::nullopt;
    return fact->get_field(left_hash_key_->first);
}

std::optional<ConstraintValue> BetaConditionNode::get_hash_key(Fact const* fact) const {
    if (!left_hash_key_ || !fact) return std::nullopt;
    return fact->get_field(right_hash_key_);
}

void BetaConditionNode::propagate_condition_transition(StatefulSession& session,
                                                        TokenWME const* wme,
                                                        size_t old_match_count,
                                                        size_t new_match_count) {
    bool const passed_before = was_passing(old_match_count);
    bool const passes_now = condition_passes(new_match_count);
    if (passed_before && !passes_now) {
        Token token{wme, PropagationType::RETRACT};
        for (auto* child : children_raw) { child->left_activate(session, token); }
    } else if (!passed_before && passes_now) {
        Token token{wme, PropagationType::ASSERT};
        for (auto* child : children_raw) { child->left_activate(session, token); }
    }
}

void BetaConditionNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().beta_condition[mem_slot];

    if (token.type == PropagationType::RETRACT) {
        auto const item = mem.left.find(token.wme);
        if (item != mem.left.end()) {
            auto const key = mem.left_keys.find(token.wme);
            if (key != mem.left_keys.end()) {
                auto bucket = mem.left_index.find(key->second);
                if (bucket != mem.left_index.end()) {
                    remove_from_vector(bucket->second, token.wme);
                    if (bucket->second.empty()) mem.left_index.erase(bucket);
                }
                mem.left_keys.erase(key);
            } else {
                remove_from_vector(mem.left_unindexed, token.wme);
            }
            mem.left.erase(item);
            for (auto* child : children_raw) { child->left_activate(session, token); }
        }
        return;
    }

    NetworkMemory::BetaConditionMem::LeftMemoryItem new_item;
    new_item.wme = token.wme;
    auto const key = get_hash_key(token);

    auto record_match = [&](Fact* fact) {
        if (check_all_join_conditions(session, token, *fact, join_constraints, binding_to_token_idx,
                                      scalar_binding_to_field, &runtime_predicates)) {
            new_item.matched_fact_ids.insert(fact->id);
        }
    };

    if (left_hash_key_ && key) {
        if (auto const bucket = mem.right_index.find(*key); bucket != mem.right_index.end()) {
            for (auto* fact : bucket->second) record_match(fact);
        }
        for (auto* fact : mem.right_unindexed) record_match(fact);
    } else {
        for (auto const& [fact_id, fact] : mem.right) record_match(fact);
    }

    auto const match_count = new_item.matched_fact_ids.size();
    mem.left[token.wme] = std::move(new_item);
    if (left_hash_key_ && key) {
        mem.left_index[*key].push_back(token.wme);
        mem.left_keys.emplace(token.wme, *key);
    } else if (left_hash_key_) {
        mem.left_unindexed.push_back(token.wme);
    }

    if (condition_passes(match_count)) {
        for (auto* child : children_raw) { child->left_activate(session, token); }
    }
}

void BetaConditionNode::right_activate(StatefulSession& session, Fact* fact, PropagationType p_type) {
    auto& mem = session.net_mem().beta_condition[mem_slot];
    if (!fact) return;

    bool const existed = mem.right.find(fact->id) != mem.right.end();
    if (p_type == PropagationType::RETRACT && !existed) return;

    std::optional<ConstraintValue> old_key;
    if (auto const key = mem.right_keys.find(fact->id); key != mem.right_keys.end()) {
        old_key = key->second;
    }
    auto const new_key = p_type == PropagationType::RETRACT ? std::nullopt : get_hash_key(fact);

    auto append_candidates = [&](std::optional<ConstraintValue> const& key,
                                 std::unordered_set<TokenWME const*>& out) {
        if (!left_hash_key_ || !key) {
            for (auto const& [wme, item] : mem.left) out.insert(wme);
            return;
        }
        if (auto const bucket = mem.left_index.find(*key); bucket != mem.left_index.end()) {
            out.insert(bucket->second.begin(), bucket->second.end());
        }
        out.insert(mem.left_unindexed.begin(), mem.left_unindexed.end());
    };

    std::unordered_set<TokenWME const*> old_candidates;
    std::unordered_set<TokenWME const*> new_candidates;
    if (existed) append_candidates(old_key, old_candidates);
    if (p_type != PropagationType::RETRACT) append_candidates(new_key, new_candidates);
    auto affected = old_candidates;
    affected.insert(new_candidates.begin(), new_candidates.end());

    for (auto const* wme : affected) {
        auto item_it = mem.left.find(wme);
        if (item_it == mem.left.end()) continue;
        auto& item = item_it->second;
        size_t const old_match_count = item.matched_fact_ids.size();
        if (existed) item.matched_fact_ids.erase(fact->id);
        if (p_type != PropagationType::RETRACT && new_candidates.contains(wme)) {
            Token token{item.wme, PropagationType::ASSERT};
            if (check_all_join_conditions(session, token, *fact, join_constraints,
                                          binding_to_token_idx, scalar_binding_to_field, &runtime_predicates)) {
                item.matched_fact_ids.insert(fact->id);
            }
        }
        propagate_condition_transition(session, item.wme, old_match_count,
                                       item.matched_fact_ids.size());
    }

    if (existed) {
        if (old_key) {
            auto bucket = mem.right_index.find(*old_key);
            if (bucket != mem.right_index.end()) {
                remove_from_vector(bucket->second, fact);
                if (bucket->second.empty()) mem.right_index.erase(bucket);
            }
            mem.right_keys.erase(fact->id);
        } else if (left_hash_key_) {
            remove_from_vector(mem.right_unindexed, fact);
        }
    }

    if (p_type == PropagationType::RETRACT) {
        mem.right.erase(fact->id);
    } else {
        mem.right[fact->id] = fact;
        if (left_hash_key_ && new_key) {
            mem.right_index[*new_key].push_back(fact);
            mem.right_keys.emplace(fact->id, *new_key);
        } else if (left_hash_key_) {
            mem.right_unindexed.push_back(fact);
        }
    }
}

void BetaConditionNode::right_activate_batch(StatefulSession& session, std::vector<Fact*>& facts, PropagationType p_type) {
    for (auto* fact : facts) right_activate(session, fact, p_type);
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

// --- BaseJoinNode ---
BaseJoinNode::BaseJoinNode(NodeKind k,
                           std::vector<ParsedConstraint> joins,
                           std::map<std::string, int> bindings,
                           std::map<std::string, std::string> scalar_binding_fields,
                           std::vector<std::optional<rulesforge::RuntimePredicateRef>> runtime_predicates) :
    ReteNode(k),
    join_constraints_(std::move(joins)),
    binding_to_token_idx_(std::move(bindings)),
    scalar_binding_to_field_(std::move(scalar_binding_fields)),
    runtime_predicates_(std::move(runtime_predicates)) {}

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
