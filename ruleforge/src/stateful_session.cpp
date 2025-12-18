#include "fmtlog.h"

#include "rfl_js_manager.hpp"
#include "query_result.hpp"
#include "rete/beta_builder.hpp"
#include "rete/rete_node.hpp"
#include "stateful_session.hpp"
#include "tms.hpp"

#include <iostream>
#include <magic_enum/magic_enum.hpp>
#include <sstream>

// Local helper to prevent code duplication
namespace {
    // Compute no-loop key based on rule pointer and fact IDs (stable across WME recreation)
    size_t compute_noloop_key(ParsedRule const* rule, std::vector<int64_t> const& fact_ids) {
        size_t hash = reinterpret_cast<uintptr_t>(rule);
        for (auto id : fact_ids) {
            hash ^= static_cast<size_t>(id) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
}   // namespace

StatefulSession::StatefulSession(private_key, std::shared_ptr<KnowledgeBase const> kb) : kb_(std::move(kb)) {
    logd("StatefulSession::StatefulSession -> Creating session. KB Rules: {}, KB Queries: {}",
         kb_->get_rules().size(), kb_->get_parser_state().parsed_queries.size());
    scripting_manager_ = std::make_unique<JSScriptingManager>(*this);
    tms_ = std::make_unique<TruthMaintenanceSystem>(*this);
    // PROD-002: Initialize schema validator
    schema_validator_ = std::make_unique<SchemaValidator>(kb_->get_parser_state().parsed_declarations);
    // Allocate dummy_wme_ from pool
    TokenWME* raw = wme_pool_.allocate();
    new (raw) TokenWME{nullptr, nullptr, 0, 0};
    dummy_wme_ = std::shared_ptr<TokenWME const>(raw, [this](TokenWME const* p) {
        wme_pool_.deallocate(const_cast<TokenWME*>(p));
    });
    wme_cache_[dummy_wme_->hash] = dummy_wme_;
}

StatefulSession::~StatefulSession() {
    logd("StatefulSession::~StatefulSession -> Destroying session. Final fact count: {}", all_facts_.size());
}

void StatefulSession::build_network() {
    logd("Building Rete network...");
    logd("StatefulSession::build_network -> Rules: {}, Queries: {}", kb_->get_rules().size(),
         kb_->get_parser_state().parsed_queries.size());
    for (auto const& rule : kb_->get_rules()) {
        if (!rule.enabled) {
            logd("Skipping disabled rule: '{}'", rule.name);
            continue;
        }
        logd("Building network for rule: '{}'", rule.name);
        if (rule.condition_groups.empty() || (rule.condition_groups.size() == 1 && rule.condition_groups[0].empty())) {
            // This is a special case for rules with no conditions. It's an immediate activation.
            logd("  -> Rule '{}' has no conditions, creating immediate activation.", rule.name);
            Token dummy_token{get_dummy_wme(), PropagationType::ASSERT};
            size_t hash = std::hash<TokenWME const*>{}(dummy_token.wme.get()) ^ reinterpret_cast<uintptr_t>(&rule);
            add_activation(Activation{&rule, dummy_token, hash, {}});
            continue;
        }

        for (auto const& condition_group : rule.condition_groups) {
            logd("  -> Building condition group for rule '{}'", rule.name);
            BetaNetworkBuilder builder(*this, condition_group, false, 0);
            auto last_node = builder.build();
            auto terminal_node = create_node<TerminalNode>(rule, builder.get_bindings());
            if (last_node) { last_node->add_child(terminal_node); }
        }
    }

    // Build the network for queries.
    for (auto& query : kb_->get_parser_state().parsed_queries) {
        logd("Building network for query: '{}'", query.name);
        BetaNetworkBuilder builder(*this, query.patterns, true, query.parameter_count);
        auto last_node = builder.build();
        // Give the terminal node the full set of bindings so it can interpret tokens correctly.
        auto query_terminal_node = create_node<QueryTerminalNode>(builder.get_bindings());
        query_nodes_[query.name] = query_terminal_node;
        if (last_node) { last_node->add_child(query_terminal_node); }

        if (query.parameter_count > 0) {
            auto query_input_node = create_node<QueryInputNode>(query_terminal_node);
            parameterized_query_inputs_[query.name] = query_input_node;
            if (builder.first_beta_node_in_chain) {
                // Connect input node to the start of the beta chain.
                query_input_node->add_child(builder.first_beta_node_in_chain);
            } else if (!last_node) {
                // The input node should propagate directly to the terminal node.
                logd("  -> Query '{}' has no body, connecting input directly to terminal.", query.name);
                query_input_node->add_child(query_terminal_node);
            }
        }
    }
}

void StatefulSession::prime_network_state() {
    logd("Priming Rete network state...");
    logd("StatefulSession::prime_network_state -> Nodes: {}", all_nodes_.size());
    Token dummy_token{get_dummy_wme(), PropagationType::ASSERT};

    for (auto const& node : all_nodes_) {
        // Is the current node a beta node that could be a root?
        if (std::dynamic_pointer_cast<BaseJoinNode>(node) || std::dynamic_pointer_cast<NotNode>(node) ||
            std::dynamic_pointer_cast<ExistsNode>(node) || std::dynamic_pointer_cast<AccumulateNode>(node) ||
            std::dynamic_pointer_cast<EvalNode>(node)) {
            bool has_beta_parent = false;
            logd("  -> Checking node ID {} for beta parents. Parent count: {}", node->id,
                 node->get_parents().size());
            for (auto const& weak_parent : node->get_parents()) {
                if (auto parent = weak_parent.lock()) {
                    logd("    -> Checking parent ID {}", parent->id);
                    // If a node is fed by another join, not, exists, etc., it's not a root.
                    if (dynamic_cast<BaseJoinNode*>(parent.get()) || dynamic_cast<NotNode*>(parent.get()) ||
                        dynamic_cast<ExistsNode*>(parent.get()) || dynamic_cast<AccumulateNode*>(parent.get()) ||
                        dynamic_cast<EvalNode*>(parent.get()) || dynamic_cast<UnnestNode*>(parent.get()) ||
                        dynamic_cast<QueryInputNode*>(parent.get())) {
                        logd("      -> Parent ID {} IS a beta node. Marking as having beta parent.", parent->id);
                        has_beta_parent = true;
                        break;
                    }
                }
            }
            // A node is a root of a beta chain if it has no beta-node parents.
            // Its parents can be AlphaNodes, EntryPointNodes, or nothing.
            if (!has_beta_parent) {
                logd("  -> Priming beta root node ID {}", node->id);
                node->left_activate(*this, dummy_token);
            }
        }
    }
    logd("Finished priming Rete network.");
}

std::vector<std::shared_ptr<ReteNode>>
StatefulSession::build_alpha_chain(ConstraintNode const* node, std::vector<std::shared_ptr<ReteNode>> parent_tails) {
    if (!node || node->children.empty()) { return parent_tails; }

    // The logic here assumes the root `node` is an AND of all its children.
    // We chain the alpha nodes one after another.
    std::vector<std::shared_ptr<ReteNode>> current_tails = parent_tails;
    for (auto const& constraint_leaf : node->children) {
        auto alpha_node = create_node<AlphaNode>(constraint_leaf->constraint);
        logd("  AlphaNode ID: {}, Constraint: {}", alpha_node->id,
             constraint_to_string(constraint_leaf->constraint));

        // Connect all current tails to this new alpha node.
        for (auto& parent : current_tails) { parent->add_child(alpha_node); }
        // The new tail of the chain IS the node we just added.
        // All subsequent nodes will be attached to this one.
        current_tails = {alpha_node};
    }

    // Return the final tail(s) of the single chain.
    return current_tails;
}

JSContext* StatefulSession::get_js_context() { return scripting_manager_->get_js_context(); }

void StatefulSession::add_fact(std::shared_ptr<Fact> fact) {
    if (!fact) return;

    // PROD-002: Schema validation
    if (validation_mode_ != ValidationMode::None && schema_validator_) {
        auto errors = schema_validator_->validate(*fact);
        if (!errors.empty()) {
            if (validation_mode_ == ValidationMode::Strict) {
                throw SchemaValidationException(std::move(errors));
            } else {  // ValidationMode::Warn
                for (auto const& e : errors) {
                    logw("Schema validation warning: {}", e.to_string());
                }
            }
        }
    }

    if (fact->id == 0) { fact->id = next_fact_id_++; }
    assign_nested_fact_ids(*fact);

    tracer_.trace_fact_added(fact->id, fact->type);

    // P1-002 FIX: Increment metrics counter
    facts_inserted_total_++;

    logd("Adding fact ID: {}, Type: '{}'", fact->id, fact->type);

    // P1-001 FIX: Track fact insertion if in a transaction
    if (in_rhs_transaction_) {
        transaction_inserted_facts_.push_back(fact->id);
    }

    all_facts_[fact->id] = fact;
    logd("Fact count after add: {}", all_facts_.size());
    auto it = alpha_entry_points_.find(fact->type);
    if (it != alpha_entry_points_.end()) {
        logd("Propagating fact ID {} to entry point for type '{}' (Node ID {})", fact->id, fact->type,
                  it->second->id);
        it->second->right_activate(*this, fact, PropagationType::ASSERT);
    } else {
        logd("No entry point found for fact type '{}'", fact->type);
    }
}

void StatefulSession::add_facts(std::vector<std::shared_ptr<Fact>> const& facts) {
    if (facts.empty()) return;

    logd("Adding {} facts in batch. Fact count before: {}", facts.size(), all_facts_.size());

    // Phase 1: Prepare all facts without network propagation
    phmap::flat_hash_map<std::string, std::vector<std::shared_ptr<Fact>>> facts_by_type;
    for (auto& fact : facts) {
        if (!fact) continue;
        if (fact->id == 0) fact->id = next_fact_id_++;
        assign_nested_fact_ids(*fact);

        tracer_.trace_fact_added(fact->id, fact->type);

        all_facts_[fact->id] = fact;
        facts_by_type[fact->type].push_back(fact);
    }

    logd("Fact count after batch add: {}", all_facts_.size());

    // Phase 2: Batch propagate by type to minimize network overhead
    for (auto const& [type, type_facts] : facts_by_type) {
        auto it = alpha_entry_points_.find(type);
        if (it != alpha_entry_points_.end()) {
            logd("Batch propagating {} facts of type '{}' to entry point (Node ID {})",
                     type_facts.size(), type, it->second->id);
            for (auto& fact : type_facts) {
                it->second->right_activate(*this, fact, PropagationType::ASSERT);
            }
        } else {
            logd("No entry point found for fact type '{}'", type);
        }
    }
}
void StatefulSession::insert_into(std::string const& stream_name, std::shared_ptr<Fact> fact) {
    if (!fact) return;
    if (fact->id == 0) { fact->id = next_fact_id_++; }
    assign_nested_fact_ids(*fact);

    tracer_.trace_fact_added(fact->id, fact->type);
    facts_inserted_total_++;

    logd("Adding fact ID: {}, Type: '{}' to entry-point '{}'", fact->id, fact->type, stream_name);

    if (in_rhs_transaction_) {
        transaction_inserted_facts_.push_back(fact->id);
    }

    all_facts_[fact->id] = fact;

    // Route to named entry point
    auto stream_it = named_entry_points_.find(stream_name);
    if (stream_it != named_entry_points_.end()) {
        auto type_it = stream_it->second.find(fact->type);
        if (type_it != stream_it->second.end()) {
            logd("Propagating fact ID {} to named entry-point '{}' for type '{}' (Node ID {})",
                 fact->id, stream_name, fact->type, type_it->second->id);
            type_it->second->right_activate(*this, fact, PropagationType::ASSERT);
        } else {
            logd("No entry point found for fact type '{}' in stream '{}'", fact->type, stream_name);
        }
    } else {
        logd("No named entry-point '{}' found", stream_name);
    }
}

void StatefulSession::retract_facts(std::vector<std::shared_ptr<Fact>> const& facts) {
    if (facts.empty()) return;

    logd("Retracting {} facts in batch", facts.size());

    // Group by type for efficient processing
    phmap::flat_hash_map<std::string, std::vector<std::shared_ptr<Fact>>> facts_by_type;
    for (auto& fact : facts) {
        if (!fact) continue;
        facts_by_type[fact->type].push_back(fact);
    }

    // Batch retract by type
    for (auto const& [type, type_facts] : facts_by_type) {
        auto it = alpha_entry_points_.find(type);
        if (it != alpha_entry_points_.end()) {
            for (auto& fact : type_facts) {
                it->second->right_activate(*this, fact, PropagationType::RETRACT);
                all_facts_.erase(fact->id);
                tms_->on_fact_retracted(fact.get());
            }
        }
    }
}

void StatefulSession::_internal_add_fact(std::shared_ptr<Fact> fact) {
    if (!fact) return;
    if (fact->id == 0) { fact->id = next_fact_id_++; }
    logd("StatefulSession::_internal_add_fact -> Adding fact ID {}. Fact count before: {}", fact->id,
              all_facts_.size());
    all_facts_[fact->id] = fact;
    logd("StatefulSession::_internal_add_fact -> Fact count after: {}", all_facts_.size());
}

void StatefulSession::_internal_add_facts_batch(std::vector<std::shared_ptr<Fact>> const& facts) {
    for (auto& fact : facts) {
        if (!fact) continue;
        if (fact->id == 0) fact->id = next_fact_id_++;
        all_facts_[fact->id] = fact;
    }
    logd("Batch internal add completed. Total facts: {}", all_facts_.size());
}

void StatefulSession::_internal_remove_fact(int64_t fact_id) {
    logd("StatefulSession::_internal_remove_fact -> Removing fact ID {}. Fact count before: {}", fact_id,
              all_facts_.size());
    all_facts_.erase(fact_id);
    logd("StatefulSession::_internal_remove_fact -> Fact count after: {}", all_facts_.size());
}

void StatefulSession::assign_nested_fact_ids(Fact& fact) {
    if (fact.id == 0) { fact.id = next_fact_id_++; }
    for (auto& [key, val] : fact.fields) {
        if (std::holds_alternative<FactList>(val)) {
            for (auto& nested_fact : std::get<FactList>(val).facts) {
                if (nested_fact) { assign_nested_fact_ids(*nested_fact); }
            }
        }
    }
}

int StatefulSession::fire_all_rules(int max_rules) {
    int total_fired_count = 0;

    // P1 FIX: Reset halt flag at start of each fire_all_rules cycle
    halt_requested_ = false;

    // P1 FIX: Reset lock-on-active blocked rules at start of cycle
    lock_on_active_blocked_.clear();

    // P1 FIX: Block lock-on-active rules for the current focus group
    std::string current_focus = get_focus();
    for (auto const& [hash, activation] : agenda_map_) {
        if (activation.rule->lock_on_active) {
            std::string rule_agenda_group = activation.rule->agenda_group.value_or("MAIN");
            if (rule_agenda_group == current_focus) {
                lock_on_active_blocked_.insert(activation.rule);
            }
        }
    }

    logd("Starting fire_all_rules cycle. Focus: '{}', Agenda size: {}. Fact count: {}. Max rules: {}",
         get_focus(), agenda_queue_.size(), all_facts_.size(), max_rules);
    while (true) {
        auto now = std::chrono::steady_clock::now();
        while (!delayed_activations_.empty() && delayed_activations_.top().fire_time <= now) {
            auto delayed = delayed_activations_.top();
            delayed_activations_.pop();
            logd("Delayed activation ready for rule '{}', adding to agenda", delayed.activation.rule->name);
            agenda_map_[delayed.activation.hash_value] = delayed.activation;
            agenda_queue_.push({delayed.activation.rule->salience, delayed.activation.hash_value});
            // Track activation-group membership
            if (delayed.activation.rule->activation_group) {
                activation_group_map_[*delayed.activation.rule->activation_group].insert(delayed.activation.hash_value);
            }
        }

        // P1 FIX: Check if rfl.halt() was called
        if (halt_requested_) {
            logi("Rule execution halted by rfl.halt() after {} rules fired.", total_fired_count);
            break;
        }

        // Check max firing limit to prevent infinite loops
        if (max_rules >= 0 && total_fired_count >= max_rules) {
            logw("Reached max rule firing limit ({}). Stopping execution. "
                 "This may indicate an infinite loop in rules.", max_rules);
            break;
        }
        std::optional<Activation> activation_to_fire;
        while (!agenda_queue_.empty()) {
            auto [salience, activation_hash] = agenda_queue_.top();
            auto map_it = agenda_map_.find(activation_hash);
            if (map_it == agenda_map_.end()) {
                agenda_queue_.pop();
                logd("Skipping stale activation (hash: {})", activation_hash);
                continue;
            }
            Activation& candidate = map_it->second;
            std::string rule_agenda_group = candidate.rule->agenda_group.value_or("MAIN");
            if (rule_agenda_group == get_focus()) {
                activation_to_fire = candidate;
                agenda_queue_.pop();
                agenda_map_.erase(map_it);
                break;
            } else {
                activation_to_fire = std::nullopt;
                break;
            }
        }

        if (activation_to_fire) {
            total_fired_count++;

            // P1-002 FIX: Increment metrics counter
            rules_fired_total_++;

            Activation& act = *activation_to_fire;

            // Extract fact IDs from the token for tracing
            std::vector<int64_t> involved_facts;
            if (act.token.wme) {
                auto token_facts = act.token.get_facts();
                for (auto const& fact : token_facts) {
                    if (fact) involved_facts.push_back(fact->id);
                }
            }

            logd("Firing rule '{}' (salience: {})", act.rule->name, act.rule->salience);
            for (auto& listener : listeners_) { listener->before_rule_fired(act.rule->name); }

            // Block re-activation for no-loop rules before execution
            if (act.rule->no_loop) {
                size_t noloop_key = compute_noloop_key(act.rule, involved_facts);
                no_loop_blocked_.insert(noloop_key);
            }

            // P1 FIX: activation-group - cancel all other activations in the same group
            if (act.rule->activation_group) {
                std::string const& group_name = *act.rule->activation_group;
                auto group_it = activation_group_map_.find(group_name);
                if (group_it != activation_group_map_.end()) {
                    // Copy the set since we'll be modifying it
                    auto activations_to_cancel = group_it->second;
                    activations_to_cancel.erase(act.hash_value);  // Don't cancel the one we're firing
                    for (size_t cancel_hash : activations_to_cancel) {
                        logd("  -> Cancelling activation in group '{}', hash: {}", group_name, cancel_hash);
                        remove_activation(cancel_hash);
                    }
                }
                // Remove this activation from the group tracking since we just fired it
                activation_group_map_[group_name].erase(act.hash_value);
                if (activation_group_map_[group_name].empty()) {
                    activation_group_map_.erase(group_name);
                }
            }

            try {
                RuleExecutionTimer timer(tracer_, act.rule->name, involved_facts);
                scripting_manager_->execute_rhs(act.rule->rhs_code, act.rule->name, act.token, act.bindings);
            } catch (ReteExecutionException const& e) {
                loge("--- RUNTIME ERROR in rule '{}': {}", e.get_rule_name(), e.what());
            }
            for (auto& listener : listeners_) { listener->after_rule_fired(act.rule->name); }
        } else {
            break;
        }
    }
    no_loop_blocked_.clear();  // Reset no-loop blocking for next fire_all_rules cycle
    lock_on_active_blocked_.clear();  // P1 FIX: Reset lock-on-active blocking
    activation_group_map_.clear();  // P1 FIX: Clear activation group tracking

    // Compact agenda_queue_ if it has too many stale entries
    // Threshold: queue size > 2x map size AND queue has at least 100 stale entries
    if (agenda_queue_.size() > agenda_map_.size() * 2 &&
        agenda_queue_.size() - agenda_map_.size() > 100) {
        std::priority_queue<std::pair<int, size_t>> compacted;
        for (auto const& [hash, activation] : agenda_map_) {
            compacted.push({activation.rule->salience, hash});
        }
        agenda_queue_ = std::move(compacted);
        logd("Compacted agenda queue: {} -> {} entries",
             agenda_queue_.size() + (agenda_queue_.size() - agenda_map_.size()), agenda_map_.size());
    }

    logd("Finished fire_all_rules cycle. Total fired: {}", total_fired_count);
    return total_fired_count;
}

void StatefulSession::retract_fact(std::shared_ptr<Fact> fact) {
    if (!fact) return;
    auto it = all_facts_.find(fact->id);
    if (it == all_facts_.end()) {
        logw("Attempted to retract fact ID {} which is not in working memory.", fact->id);
        return;
    }
    std::shared_ptr<Fact> fact_to_retract = it->second;

    // P1-002 FIX: Increment metrics counter
    facts_retracted_total_++;

    logd("Fact count before retract: {}", all_facts_.size());
    all_facts_.erase(it);
    logd("Fact count after retract: {}", all_facts_.size());

    // Clean up agenda: remove activations that depend on the retracted fact
    std::vector<size_t> activations_to_remove;
    for (auto const& [hash, activation] : agenda_map_) {
        if (activation.token.wme) {
            // Check if this activation's token contains the retracted fact
            std::vector<std::shared_ptr<Fact>> token_facts = activation.token.get_facts();
            for (auto const& token_fact : token_facts) {
                if (token_fact && token_fact->id == fact_to_retract->id) {
                    activations_to_remove.push_back(hash);
                    logd("Removing activation for rule '{}' because it depends on retracted fact ID {}",
                              activation.rule->name, fact_to_retract->id);
                    break;
                }
            }
        }
    }

    // Remove the identified activations
    for (size_t hash : activations_to_remove) {
        agenda_map_.erase(hash);
    }

    // Note: We don't need to clean the priority_queue directly since fire_all_rules()
    // will skip stale entries that are no longer in agenda_map_

    tms_->on_fact_retracted(fact_to_retract.get());
    logd("Retracting fact ID: {}, Type: {}, Propagation: {}", fact_to_retract->id, fact_to_retract->type,
              magic_enum::enum_name(PropagationType::RETRACT));

    auto alpha_it = alpha_entry_points_.find(fact_to_retract->type);
    if (alpha_it != alpha_entry_points_.end()) {
        alpha_it->second->right_activate(*this, fact_to_retract, PropagationType::RETRACT);
    }

    // Clean up WMEs that reference the retracted fact (directly or in parent chain)
    std::vector<size_t> hashes_to_retract;
    for (auto const& [hash, wme_ptr] : wme_cache_) {
        if (!wme_ptr) continue;
        // Check if this WME or any ancestor references the retracted fact
        for (auto const* current = wme_ptr.get(); current; current = current->parent.get()) {
            if (current->fact && current->fact->id == fact_to_retract->id) {
                hashes_to_retract.push_back(hash);
                break;
            }
        }
    }
    for (size_t hash : hashes_to_retract) {
        auto it = wme_cache_.find(hash);
        if (it != wme_cache_.end()) {
            logical_retract(it->second.get());
            wme_cache_.erase(it);
        }
    }
}

void StatefulSession::update_fact(std::shared_ptr<Fact> fact, std::function<void(Fact&)> modifier) {
    if (!fact || all_facts_.find(fact->id) == all_facts_.end()) return;
    logd("Updating fact ID: {}, Type: '{}'", fact->id, fact->type);
    modifier(*fact);
    auto alpha_it = alpha_entry_points_.find(fact->type);
    if (alpha_it != alpha_entry_points_.end()) {
        alpha_it->second->right_activate(*this, fact, PropagationType::MODIFY);
    }
}

void StatefulSession::logical_insert(Token& token, std::shared_ptr<Fact> fact) {
    if (!token.wme) return;
    logd("StatefulSession::logical_insert -> fact_id={} justified by token WME {}", fact->id, token.wme->get_id());
    if (all_facts_.find(fact->id) == all_facts_.end()) {
        logd("  -> Fact ID {} is new, adding to working memory.", fact->id);
        add_fact(fact);
    }
    tms_->add_justification(token.wme, fact);
}

void StatefulSession::logical_retract(TokenWME const* wme) {
    logd("StatefulSession::logical_retract for wme {}", (wme ? wme->get_id() : 0));
    tms_->remove_justifications_by_token(wme);
}

std::optional<std::shared_ptr<Fact>> StatefulSession::get_fact_by_id(int64_t id) {
    auto it = all_facts_.find(id);
    return (it != all_facts_.end()) ? std::optional(it->second) : std::nullopt;
}

QueryResult StatefulSession::execute_query(std::string const& query_name,
                                           std::vector<std::shared_ptr<Fact>> const& args) {

    logd("Executing query -> name='{}', args={}", query_name, args.size());
    auto it = query_nodes_.find(query_name);
    // PROD-003: Return error result instead of throwing
    if (it == query_nodes_.end()) {
        return QueryResult::error("Query '" + query_name + "' not found.");
    }
    auto& terminal_node = it->second;

    auto param_it = parameterized_query_inputs_.find(query_name);
    if (param_it != parameterized_query_inputs_.end()) { param_it->second->execute(*this, args); }

    std::vector<map<std::string, std::shared_ptr<Fact>>> raw_results;
    for (auto const& [wme, token] : terminal_node->get_results()) {
        map<std::string, std::shared_ptr<Fact>> row;
        auto facts = token.get_facts();
        for (auto const& [binding, depth] : terminal_node->get_bindings()) {
            if (!binding.empty() && depth < facts.size()) { row[binding] = facts[depth]; }
        }
        if (!row.empty()) raw_results.push_back(row);
    }
    logd("Query '{}' returned {} results.", query_name, raw_results.size());
    return QueryResult(std::move(raw_results), kb_);
}

void StatefulSession::set_focus(std::string const& group_name) {
    logd("StatefulSession::set_focus -> {}", group_name);
    agenda_group_focus_stack_.push_back(group_name);
}

std::string StatefulSession::get_focus() const {
    return agenda_group_focus_stack_.empty() ? "MAIN" : agenda_group_focus_stack_.back();
}

void StatefulSession::set_global(std::string const& name, JSValue obj) {
    scripting_manager_->set_global(name, obj);
}

map<std::string, JSValue> const& StatefulSession::get_global_values() const {
    static map<std::string, JSValue> empty;
    return empty;
}

size_t StatefulSession::get_fact_count() const { return all_facts_.size(); }

int64_t StatefulSession::get_next_fact_id() { return next_fact_id_++; }

// PROD-002: Check if a fact type has a declaration
bool StatefulSession::has_type_declaration(std::string const& type_name) const {
    return schema_validator_ && schema_validator_->has_declaration(type_name);
}

bool StatefulSession::execute_eval(std::string const& code, Token const& token,
                                   map<std::string, int> const& bindings) {
    return scripting_manager_->execute_eval(code, token, bindings);
}

void StatefulSession::addListener(std::shared_ptr<IEngineListener> listener) {
    if (listener) listeners_.push_back(listener);
}

void StatefulSession::removeListener(std::shared_ptr<IEngineListener> const& listener) {
    std::erase(listeners_, listener);
}

void StatefulSession::add_activation(Activation const& activation) {
    // Extract fact IDs for no-loop check and tracing
    std::vector<int64_t> involved_facts;
    if (activation.token.wme) {
        auto token_facts = activation.token.get_facts();
        for (auto const& fact : token_facts) {
            if (fact) involved_facts.push_back(fact->id);
        }
    }

    // Check if this is a no-loop rule that has already fired with these facts
    if (activation.rule->no_loop) {
        size_t noloop_key = compute_noloop_key(activation.rule, involved_facts);
        if (no_loop_blocked_.count(noloop_key)) {
            logd("Skipping no-loop blocked activation for rule '{}', key: {}",
                 activation.rule->name, noloop_key);
            return;
        }
    }

    // P1 FIX: Check lock-on-active - rule cannot be re-activated while its agenda-group is active
    if (activation.rule->lock_on_active) {
        if (lock_on_active_blocked_.count(activation.rule)) {
            logd("Skipping lock-on-active blocked activation for rule '{}'", activation.rule->name);
            return;
        }
    }

    logd("Activating rule '{}' (salience: {}), hash: {}", activation.rule->name, activation.rule->salience,
              activation.hash_value);

    tracer_.trace_rule_matched(activation.rule->name, involved_facts);
    if (activation.rule->duration > 0) {
        auto fire_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(activation.rule->duration);
        delayed_activations_.push({fire_time, activation});
        logd("  -> Rule '{}' scheduled for delayed firing in {}ms", activation.rule->name, activation.rule->duration);
        return;
    }

    agenda_map_[activation.hash_value] = activation;
    agenda_queue_.push({activation.rule->salience, activation.hash_value});

    // P1 FIX: Track activation-group membership for later cancellation
    if (activation.rule->activation_group) {
        activation_group_map_[*activation.rule->activation_group].insert(activation.hash_value);
    }
}

void StatefulSession::remove_activation(size_t activation_hash) {
    auto it = agenda_map_.find(activation_hash);
    if (it != agenda_map_.end()) {
        logd("Deactivating rule '{}', hash: {}", it->second.rule->name, activation_hash);

        // P1 FIX: Clean up activation-group tracking
        if (it->second.rule->activation_group) {
            auto group_it = activation_group_map_.find(*it->second.rule->activation_group);
            if (group_it != activation_group_map_.end()) {
                group_it->second.erase(activation_hash);
                if (group_it->second.empty()) {
                    activation_group_map_.erase(group_it);
                }
            }
        }

        agenda_map_.erase(it);
    }
}

map<int, std::shared_ptr<ReteNode>> StatefulSession::get_nodes() const {
    map<int, std::shared_ptr<ReteNode>> node_map;
    for (auto const& node : all_nodes_) { node_map[node->id] = node; }
    return node_map;
}

// P1-001 FIX: Transactional semantics implementation
void StatefulSession::begin_rhs_transaction() {
    logd("StatefulSession::begin_rhs_transaction");
    in_rhs_transaction_ = true;
    transaction_inserted_facts_.clear();
}

void StatefulSession::end_rhs_transaction(bool commit) {
    logd("StatefulSession::end_rhs_transaction commit={}", commit);
    if (!in_rhs_transaction_) {
        return;  // No transaction in progress
    }

    if (!commit) {
        // Rollback: retract all facts inserted during this transaction
        logw("Rolling back RHS transaction: retracting {} inserted facts", transaction_inserted_facts_.size());
        for (int64_t fact_id : transaction_inserted_facts_) {
            auto fact_opt = get_fact_by_id(fact_id);
            if (fact_opt) {
                logd("  -> Rolling back fact ID {}", fact_id);
                // Use internal remove to avoid re-triggering network propagation issues
                all_facts_.erase(fact_id);
            }
        }
        is_consistent_ = false;  // Mark session as having had a failed transaction
    }

    transaction_inserted_facts_.clear();
    in_rhs_transaction_ = false;
}

// P1-002 FIX: Get session metrics for monitoring export
SessionMetrics StatefulSession::get_metrics() const {
    SessionMetrics metrics;

    // Counters
    metrics.rules_fired_total = rules_fired_total_;
    metrics.facts_inserted_total = facts_inserted_total_;
    metrics.facts_retracted_total = facts_retracted_total_;

    // Gauges
    metrics.facts_count = static_cast<int64_t>(all_facts_.size());
    metrics.memory_used_bytes = arena_.memory_used();
    metrics.memory_max_bytes = arena_.get_max_size();
    metrics.memory_usage_percent = arena_.usage_percent();
    metrics.activations_count = agenda_map_.size();

    // Per-rule metrics from tracer
    auto rule_stats = tracer_.get_rule_statistics();
    for (auto const& stat : rule_stats) {
        metrics.rule_fire_counts[stat.rule_name] = stat.fire_count;
        metrics.rule_execution_time_us[stat.rule_name] = stat.total_execution_time_us;
    }

    // Timing
    metrics.last_fire_time = std::chrono::steady_clock::now();

    return metrics;
}


