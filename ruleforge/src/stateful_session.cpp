#include "fmtlog.h"

#include "drools_js_manager.hpp"
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
    std::string constraint_to_string_local(ParsedConstraint const& join) {
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
}   // namespace

StatefulSession::StatefulSession(private_key, std::shared_ptr<KnowledgeBase const> kb) : kb_(std::move(kb)) {
    logd("StatefulSession::StatefulSession -> Creating session. KB Rules: {}, KB Queries: {}",
         kb_->get_rules().size(), kb_->get_parser_state().parsed_queries.size());
    scripting_manager_ = std::make_unique<JSScriptingManager>(*this);
    tms_ = std::make_unique<TruthMaintenanceSystem>(*this);
    dummy_wme_ = std::make_shared<TokenWME>(TokenWME{nullptr, nullptr, 0, 0});
    wme_cache_.insert(dummy_wme_);
}

StatefulSession::~StatefulSession() {
    logd("StatefulSession::~StatefulSession -> Destroying session. Final fact count: {}", all_facts_.size());
}

void StatefulSession::build_network() {
    logd("Building Rete network...");
    logd("StatefulSession::build_network -> Rules: {}, Queries: {}", kb_->get_rules().size(),
         kb_->get_parser_state().parsed_queries.size());
    for (auto const& rule : kb_->get_rules()) {
        logd("Building network for rule: '{}'", rule.name);
        if (rule.condition_groups.empty() || (rule.condition_groups.size() == 1 && rule.condition_groups[0].empty())) {
            // This is a special case for rules with no conditions. It's an immediate activation.
            logd("  -> Rule '{}' has no conditions, creating immediate activation.", rule.name);
            auto dummy_token = std::make_shared<Token>(get_dummy_wme(), PropagationType::ASSERT);
            size_t hash = std::hash<TokenWME const*>{}(dummy_token->wme.get()) ^ reinterpret_cast<uintptr_t>(&rule);
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
    auto dummy_token = std::make_shared<Token>(get_dummy_wme(), PropagationType::ASSERT);

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
             constraint_to_string_local(constraint_leaf->constraint));

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
    if (fact->id == 0) { fact->id = next_fact_id_++; }
    assign_nested_fact_ids(*fact);

    tracer_.trace_fact_added(fact->id, fact->type);

    logd("Adding fact ID: {}, Type: '{}'", fact->id, fact->type);

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

int StatefulSession::fire_all_rules() {
    int total_fired_count = 0;
    logd("Starting fire_all_rules cycle. Focus: '{}', Agenda size: {}. Fact count: {}", get_focus(),
              agenda_queue_.size(), all_facts_.size());
    while (true) {
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
            Activation& act = *activation_to_fire;

            // Extract fact IDs from the token for tracing
            std::vector<int64_t> involved_facts;
            if (act.token && act.token->wme) {
                auto token_facts = act.token->get_facts();
                for (auto const& fact : token_facts) {
                    if (fact) involved_facts.push_back(fact->id);
                }
            }

            logd("Firing rule '{}' (salience: {})", act.rule->name, act.rule->salience);
            for (auto& listener : listeners_) { listener->before_rule_fired(act.rule->name); }

            try {
                RuleExecutionTimer timer(tracer_, act.rule->name, involved_facts);
                scripting_manager_->execute_rhs(act.rule->rhs_code, act.rule->name, *act.token, act.bindings);
            } catch (ReteExecutionException const& e) {
                loge("--- RUNTIME ERROR in rule '{}': {}", e.get_rule_name(), e.what());
            }
            for (auto& listener : listeners_) { listener->after_rule_fired(act.rule->name); }
        } else {
            break;
        }
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
    logd("Fact count before retract: {}", all_facts_.size());
    all_facts_.erase(it);
    logd("Fact count after retract: {}", all_facts_.size());

    // Clean up agenda: remove activations that depend on the retracted fact
    std::vector<size_t> activations_to_remove;
    for (auto const& [hash, activation] : agenda_map_) {
        if (activation.token && activation.token->wme) {
            // Check if this activation's token contains the retracted fact
            std::vector<std::shared_ptr<Fact>> token_facts = activation.token->get_facts();
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

    std::vector<TokenWME const*> wmes_to_retract;
    for (auto const& wme_ptr : wme_cache_) {
        if (wme_ptr && wme_ptr->fact && wme_ptr->fact->id == fact_to_retract->id) {
            wmes_to_retract.push_back(wme_ptr.get());
        }
    }
    for (auto const* wme : wmes_to_retract) { logical_retract(wme); }
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
    if (it == query_nodes_.end()) { throw std::runtime_error("Query '" + query_name + "' not found."); }
    auto& terminal_node = it->second;

    auto param_it = parameterized_query_inputs_.find(query_name);
    if (param_it != parameterized_query_inputs_.end()) { param_it->second->execute(*this, args); }

    std::vector<map<std::string, std::shared_ptr<Fact>>> raw_results;
    for (auto const& [wme, token] : terminal_node->get_results()) {
        map<std::string, std::shared_ptr<Fact>> row;
        auto facts = token->get_facts();
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
    logd("Activating rule '{}' (salience: {}), hash: {}", activation.rule->name, activation.rule->salience,
              activation.hash_value);

    // Extract fact IDs for tracing
    std::vector<int64_t> involved_facts;
    if (activation.token && activation.token->wme) {
        auto token_facts = activation.token->get_facts();
        for (auto const& fact : token_facts) {
            if (fact) involved_facts.push_back(fact->id);
        }
    }

    tracer_.trace_rule_matched(activation.rule->name, involved_facts);

    agenda_map_[activation.hash_value] = activation;
    agenda_queue_.push({activation.rule->salience, activation.hash_value});
}

void StatefulSession::remove_activation(size_t activation_hash) {
    if (agenda_map_.count(activation_hash)) {
        logd("Deactivating rule '{}', hash: {}", agenda_map_.at(activation_hash).rule->name, activation_hash);
        agenda_map_.erase(activation_hash);
    }
}

map<int, std::shared_ptr<ReteNode>> StatefulSession::get_nodes() const {
    map<int, std::shared_ptr<ReteNode>> node_map;
    for (auto const& node : all_nodes_) { node_map[node->id] = node; }
    return node_map;
}
