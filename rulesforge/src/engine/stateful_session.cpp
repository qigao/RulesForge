#include "core/logging_control.hpp"

#include "expression_evaluator.hpp"
#include "core/exceptions.hpp"
#include "engine/query_engine.hpp"
#include "engine/rhs_executor.hpp"
#include "engine/query_result.hpp"
#include "rete/compiled_network.hpp"
#include "rete/rete_node.hpp"
#include "engine/stateful_session.hpp"
#include "engine/tms.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <jsoncons/json.hpp>
#include <jsoncons_ext/jmespath/jmespath.hpp>

using namespace rulesforge;

// Local helper to prevent code duplication
namespace {
    bool ends_with(std::string const& s, std::string const& suffix) {
        if (s.size() < suffix.size()) return false;
        return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    std::string normalize_type_name(std::string type_name) {
        type_name.erase(std::remove_if(type_name.begin(), type_name.end(),
                                       [](unsigned char c) { return std::isspace(c) != 0; }),
                        type_name.end());
        std::transform(type_name.begin(), type_name.end(), type_name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return type_name;
    }

    ConstraintValue default_global_value(std::string const& declared_type) {
        std::string const normalized = normalize_type_name(declared_type);
        if (normalized == "list" || ends_with(normalized, ".list")) return make_typed_list();
        if (normalized == "set" || ends_with(normalized, ".set")) return make_value_set();
        if (normalized == "map" || ends_with(normalized, ".map")) return make_value_map();
        return NilValue{};
    }

    std::string read_text_file_or_throw(std::string const& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Failed to open file: " + path);
        }
        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    }

    std::vector<std::string> extract_json_documents_or_throw(std::string const& json_content,
                                                             std::string const& jmespath_expr) {
        if (jmespath_expr.empty()) {
            return {json_content};
        }

        auto document = jsoncons::json::parse(json_content);
        auto selected = jsoncons::jmespath::search(document, jmespath_expr);

        std::vector<std::string> extracted;
        if (selected.is_null()) {
            return extracted;
        }

        if (selected.is_array()) {
            extracted.reserve(selected.size());
            for (auto const& item : selected.array_range()) {
                extracted.push_back(item.to_string());
            }
            return extracted;
        }

        extracted.push_back(selected.to_string());
        return extracted;
    }

    std::string resolve_codec_type_name_or_throw(StatefulSession const& session,
                                                 char const* source_kind) {
        auto const& declarations = session.get_knowledge_base()->get_parser_state().parsed_declarations;
        if (declarations.empty()) {
            throw std::runtime_error(std::string("Cannot infer type for ") + source_kind
                                     + " data source: no declarations loaded");
        }

        for (auto const& decl : declarations) {
            if (decl.type_name == "Row") {
                return decl.type_name;
            }
        }

        if (declarations.size() == 1) {
            return declarations.front().type_name;
        }

        throw std::runtime_error(std::string("Cannot infer type for ") + source_kind
                                 + " data source: multiple declarations loaded");
    }

    // Zero-allocation version: walk token chain directly
    size_t compute_noloop_key_from_token(ParsedRule const* rule, TokenWME const* wme) {
        size_t hash = reinterpret_cast<uintptr_t>(rule);
        auto* curr = wme;
        while (curr && curr->depth > 0) {
            if (curr->fact) {
                hash ^= static_cast<size_t>(curr->fact->id) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            curr = curr->parent;
        }
        return hash;
    }

}   // namespace

StatefulSession::StatefulSession(private_key, std::shared_ptr<KnowledgeBase const> kb) :
    kb_(std::move(kb)),
    token_pool_(1024), // Initial capacity: 1024 tokens
    fact_arena_(64 * 1024 * 1024)   // 64MB
{
    // Initialize native RHS executor
    rhs_executor_ = std::make_unique<RhsExecutor>(*this);
    query_engine_ = std::make_unique<QueryEngine>(kb_);

    tms_ = std::make_unique<TruthMaintenanceSystem>(*this);
    schema_validator_ = std::make_unique<SchemaValidator>(kb_->get_parser_state().parsed_declarations);

    // Root WME is managed by TokenPool
    dummy_wme_ = token_pool_.get_root();
    phreak_experimental_ = kb_->is_phreak_experimental();

    for (auto const& g : kb_->get_parser_state().parsed_globals) {
        globals_.emplace(g.name, default_global_value(g.type));
    }
}

StatefulSession::~StatefulSession() {
}

void StatefulSession::allocate_network_memory(MemSlotCounts const& counts) {
    net_mem_.allocate(counts);
}

void StatefulSession::prime_network_state() {
    // Handle rules with no conditions - immediate activation
    for (auto const& rule : kb_->get_rules()) {
        if (!rule.enabled) continue;
        if (rule.condition_groups.empty() || (rule.condition_groups.size() == 1 && rule.condition_groups[0].empty())) {
            Token dummy_token{get_dummy_wme(), PropagationType::ASSERT};
            size_t hash = std::hash<TokenWME const*>{}(dummy_token.wme) ^ reinterpret_cast<uintptr_t>(&rule);
            add_activation(Activation{&rule, dummy_token, hash, nullptr});
        }
    }

    // Prime beta root nodes with dummy token
    Token dummy_token{get_dummy_wme(), PropagationType::ASSERT};
    auto const& net = kb_->network();

    // Optimization: Use pre-calculated beta root nodes instead of scanning all nodes
    for (auto* node : net.beta_root_nodes) {
        node->left_activate(*this, dummy_token);
    }
}

void StatefulSession::ensure_consistent_for_mutation(char const* operation) const {
    if (is_consistent_) {
        return;
    }
    throw SessionInconsistentException(
        std::string("Session is inconsistent due to a previous failed RHS transaction. "
                    "Call reset() before ") +
        operation + ".");
}

void StatefulSession::validate_fact_for_insert(Fact const& fact) const {
    if (validation_mode_ == ValidationMode::None || !schema_validator_) {
        return;
    }

    auto errors = schema_validator_->validate(fact);
    if (errors.empty()) {
        return;
    }

    if (validation_mode_ == ValidationMode::Strict) {
        throw SchemaValidationException(std::move(errors));
    }

    for (auto const& e : errors) {
        logw("Schema validation warning: {}", e.to_string());
    }
}

void StatefulSession::add_fact(Fact* fact) {
    if (!fact) return;
    ensure_consistent_for_mutation("adding facts");
    fact->type = canonicalize_fact_type_name(fact->type);
    validate_fact_for_insert(*fact);

    if (fact->id == 0) { fact->id = working_memory_.reserve_next_id(); }
    working_memory_.assign_nested_ids(*fact);

    tracer_.trace_fact_added(fact->id, fact->type);

    facts_inserted_total_++;


    if (in_rhs_transaction_) {
        transaction_inserted_facts_.push_back(fact->id);
    }

    working_memory_.insert(fact);
    auto it = kb_->network().alpha_entry_points.find(fact->type);
    if (it != kb_->network().alpha_entry_points.end()) {
        if (phreak_experimental_) mark_phreak_dirty_for_type(fact->type);
        it->second->right_activate(*this, fact, PropagationType::ASSERT);
    } else {
    }
}

void StatefulSession::add_fact(std::shared_ptr<Fact> const& fact) {
    if (!fact) return;
    add_fact(fact.get());
    retain_shared_fact(fact);
}

void StatefulSession::add_facts(std::vector<Fact*> const& facts) {
    if (facts.empty()) return;
    ensure_consistent_for_mutation("adding facts");

    std::vector<Fact*> validated_facts;
    validated_facts.reserve(facts.size());
    for (auto* fact : facts) {
        if (!fact) continue;
        fact->type = canonicalize_fact_type_name(fact->type);
        validate_fact_for_insert(*fact);
        validated_facts.push_back(fact);
    }
    if (validated_facts.empty()) return;

    // Pre-size hash map to avoid rehashing during bulk insert
    working_memory_.reserve_for_additional(validated_facts.size());

    // Use a map to group facts by type for batch propagation
    std::map<std::string, std::vector<Fact*>> facts_by_type;

    for (auto* fact : validated_facts) {
        if (fact->id == 0) { fact->id = working_memory_.reserve_next_id(); }

        // Only recurse into nested facts if any FactList fields exist (rare)
        for (auto& [key, val] : fact->fields) {
            if (std::holds_alternative<FactList>(val)) {
                working_memory_.assign_nested_ids(*fact);
                break;
            }
        }

        tracer_.trace_fact_added(fact->id, fact->type);
        facts_inserted_total_++;

        if (in_rhs_transaction_) {
            transaction_inserted_facts_.push_back(fact->id);
        }

        working_memory_.insert(fact);
        facts_by_type[fact->type].push_back(fact);
    }

    auto const& net = kb_->network();
    for (auto& [type, type_facts] : facts_by_type) {
        auto it = net.alpha_entry_points.find(type);
        if (it != net.alpha_entry_points.end()) {
            if (phreak_experimental_) mark_phreak_dirty_for_type(type);
            it->second->right_activate_batch(*this, type_facts, PropagationType::ASSERT);
        } else {
        }
    }
}

void StatefulSession::add_facts(std::vector<std::shared_ptr<Fact>> const& facts) {
    if (facts.empty()) return;

    std::vector<Fact*> raw;
    raw.reserve(facts.size());
    for (auto const& fact : facts) {
        if (fact) raw.push_back(fact.get());
    }

    add_facts(raw);

    for (auto const& fact : facts) {
        retain_shared_fact(fact);
    }
}
void StatefulSession::insert_into(std::string const& stream_name, Fact* fact) {
    if (!fact) return;
    ensure_consistent_for_mutation("adding facts");
    fact->type = canonicalize_fact_type_name(fact->type);
    validate_fact_for_insert(*fact);
    if (fact->id == 0) { fact->id = working_memory_.reserve_next_id(); }
    working_memory_.assign_nested_ids(*fact);

    tracer_.trace_fact_added(fact->id, fact->type);
    facts_inserted_total_++;


    if (in_rhs_transaction_) {
        transaction_inserted_facts_.push_back(fact->id);
    }

    working_memory_.insert(fact);

    // Route to named entry point
    auto stream_it = kb_->network().named_entry_points.find(stream_name);
    if (stream_it != kb_->network().named_entry_points.end()) {
        auto type_it = stream_it->second.find(fact->type);
        if (type_it != stream_it->second.end()) {
            if (phreak_experimental_) mark_phreak_dirty_for_type(fact->type);
            type_it->second->right_activate(*this, fact, PropagationType::ASSERT);
        } else {
        }
    } else {
    }
}

void StatefulSession::retract_facts(std::vector<Fact*> const& facts) {
    if (facts.empty()) return;
    ensure_consistent_for_mutation("retracting facts");

    std::map<std::string, std::vector<Fact*>> facts_by_type;
    for (auto* fact : facts) {
        if (!fact) continue;
        auto* existing = working_memory_.get(fact->id);
        if (existing) {
            tracer_.trace_fact_retracted(fact->id, fact->type);
            facts_retracted_total_++;
            if (in_rhs_transaction_) {
                transaction_retracted_facts_.push_back(existing);
            }
            facts_by_type[fact->type].push_back(fact);
            working_memory_.remove(fact->id);
        }
    }

    auto const& net = kb_->network();
    for (auto& [type, type_facts] : facts_by_type) {
        auto it = net.alpha_entry_points.find(type);
        if (it != net.alpha_entry_points.end()) {
            if (phreak_experimental_) mark_phreak_dirty_for_type(type);
            it->second->right_activate_batch(*this, type_facts, PropagationType::RETRACT);
        }
    }

    for (auto* fact : facts) {
        if (fact) release_retained_fact(fact->id);
    }
}

void StatefulSession::_internal_add_fact(Fact* fact) {
    if (!fact) return;
    if (fact->id == 0) { fact->id = working_memory_.reserve_next_id(); }
    working_memory_.insert(fact);
}

void StatefulSession::_internal_add_facts_batch(std::vector<Fact*> const& facts) {
    working_memory_.insert_batch(facts);
}

void StatefulSession::_internal_remove_fact(int64_t fact_id) {

    working_memory_.remove(fact_id);
    release_retained_fact(fact_id);
}

int StatefulSession::fire_all_rules(int max_rules) {
    int total_fired_count = 0;
    constexpr size_t kAgendaBatchSize = 64;

    if (!is_consistent_) {
        loge("Session is inconsistent due to a previous failed RHS transaction. Call reset() before firing again.");
        return 0;
    }

    // Flush any pending facts from deferred batch insertion
    flush_pending_nodes();

    // P1 FIX: Reset halt flag at start of each fire_all_rules cycle
    halt_requested_ = false;

    // P1 FIX: Reset and seed lock-on-active state for current focus
    agenda_.seed_lock_on_active_for_focus();

/*     logd("Starting fire_all_rules cycle. Focus: '{}', Agenda size: {}. Fact count: {}. Max rules: {}",
         get_focus(), agenda_.size(), working_memory_.count(), max_rules); */
    while (true) {
        agenda_.flush_ready_delayed();

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
        size_t budget = kAgendaBatchSize;
        if (max_rules >= 0) {
            int remaining = max_rules - total_fired_count;
            if (remaining <= 0) break;
            budget = static_cast<size_t>(
                remaining < static_cast<int>(kAgendaBatchSize) ? remaining : static_cast<int>(kAgendaBatchSize));
        }

        uint64_t pop_select_us = 0;
        uint64_t pop_detach_us = 0;
        auto batch = agenda_.pop_next_batch_activations(budget, &pop_select_us, &pop_detach_us);
        runtime_counters_.agenda_pop_select_time_us += pop_select_us;
        runtime_counters_.agenda_pop_detach_time_us += pop_detach_us;
        runtime_counters_.agenda_pop_time_us += (pop_select_us + pop_detach_us);
        runtime_counters_.agenda_pop_calls += batch.size();

        if (batch.empty()) {
            break;
        }

        for (size_t i = 0; i < batch.size(); ++i) {
            if (halt_requested_ || (max_rules >= 0 && total_fired_count >= max_rules)) {
                agenda_.requeue_batch_activations(batch, i);
                if (halt_requested_) {
                    logi("Rule execution halted by rfl.halt() after {} rules fired.", total_fired_count);
                }
                break;
            }

            // Activation might have been detached in this batch before a previous
            // RHS action retracted one of its facts. Skip stale activations.
            bool activation_valid = true;
            if (batch[i].activation.token.wme) {
                auto token_facts = batch[i].activation.token.get_facts();
                for (auto* f : token_facts) {
                    if (!f) continue;
                    // Transient network facts (e.g., from JMESPath/accumulate) are not
                    // inserted into working memory and typically keep id==0.
                    if (f->id <= 0) continue;
                    if (!working_memory_.get(f->id)) {
                        activation_valid = false;
                        break;
                    }
                }
            }
            if (!activation_valid) {
                continue;
            }

            total_fired_count++;

            // P1-002 FIX: Increment metrics counter
            rules_fired_total_++;

            auto act_t0 = std::chrono::steady_clock::now();
            fire_activation(batch[i].activation);
            auto act_t1 = std::chrono::steady_clock::now();
            runtime_counters_.fire_activation_time_us +=
                std::chrono::duration_cast<std::chrono::microseconds>(act_t1 - act_t0).count();
        }

        if (halt_requested_ || (max_rules >= 0 && total_fired_count >= max_rules)) {
            break;
        }
    }
    agenda_.clear_noloop();  // Reset no-loop blocking for next fire_all_rules cycle
    agenda_.clear_lock_on_active();  // P1 FIX: Reset lock-on-active blocking
    agenda_.clear_activation_groups();  // P1 FIX: Clear activation group tracking

    return total_fired_count;
}

void StatefulSession::fire_activation(Activation& activation) {
    runtime_counters_.fire_activation_calls++;
    auto total_t0 = std::chrono::steady_clock::now();
    uint64_t rhs_us = 0;
    if (!listeners_.empty()) {
        for (auto& listener : listeners_) {
            listener->before_rule_fired(activation.rule->name);
        }
    }

    if (activation.rule->no_loop) {
        size_t noloop_key = compute_noloop_key_from_token(activation.rule, activation.token.wme);
        agenda_.block_noloop(noloop_key);
    }

    if (activation.rule->activation_group) {
        agenda_.cancel_activation_group(*activation.rule->activation_group, activation.hash_value);
    }

    try {
        bool const tracing_enabled = tracer_.is_enabled();
        if (tracing_enabled) {
            std::vector<int64_t> involved_facts;
            if (activation.token.wme) {
                auto token_facts = activation.token.get_facts();
                for (auto const& fact : token_facts) {
                    if (fact) involved_facts.push_back(fact->id);
                }
            }
            RuleExecutionTimer timer(tracer_, activation.rule->name, involved_facts);
            if (!activation.rule->compiled_actions.empty()) {
                current_activation_ = &activation;
                try {
                    auto rhs_t0 = std::chrono::steady_clock::now();
                    rhs_executor_->execute(activation.rule->compiled_actions, activation.token,
                                           *activation.bindings, activation.rule->name);
                    auto rhs_t1 = std::chrono::steady_clock::now();
                    rhs_us += static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(rhs_t1 - rhs_t0).count());
                } catch (...) {
                    current_activation_ = nullptr;
                    throw;
                }
                current_activation_ = nullptr;
            }
        } else {
            if (!activation.rule->compiled_actions.empty()) {
                current_activation_ = &activation;
                try {
                    auto rhs_t0 = std::chrono::steady_clock::now();
                    rhs_executor_->execute(activation.rule->compiled_actions, activation.token,
                                           *activation.bindings, activation.rule->name);
                    auto rhs_t1 = std::chrono::steady_clock::now();
                    rhs_us += static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(rhs_t1 - rhs_t0).count());
                } catch (...) {
                    current_activation_ = nullptr;
                    throw;
                }
                current_activation_ = nullptr;
            }
        }
    } catch (ReteExecutionException const& e) {
        loge("--- RUNTIME ERROR in rule '{}': {}", e.get_rule_name(), e.what());
    }

    if (!listeners_.empty()) {
        for (auto& listener : listeners_) {
            listener->after_rule_fired(activation.rule->name);
        }
    }
    auto total_t1 = std::chrono::steady_clock::now();
    uint64_t total_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(total_t1 - total_t0).count());
    runtime_counters_.rhs_execute_time_us += rhs_us;
    runtime_counters_.fire_bookkeeping_time_us += (total_us > rhs_us ? (total_us - rhs_us) : 0);
}

void StatefulSession::retract_fact(Fact* fact) {
    if (!fact) return;
    ensure_consistent_for_mutation("retracting facts");
    auto* fact_to_retract = working_memory_.get(fact->id);
    if (!fact_to_retract) {
        logw("Attempted to retract fact ID {} which is not in working memory.", fact->id);
        return;
    }

    facts_retracted_total_++;

    if (in_rhs_transaction_) {
        // During RHS transaction: defer all cleanup until commit
        // This preserves the activation's token/WME chain for subsequent actions
        transaction_retracted_facts_.push_back(fact_to_retract);
        return;
    }

    working_memory_.remove(fact->id);

    // Clean up agenda: remove activations that depend on the retracted fact
    // Uses fact_index directly to avoid vector allocation per activation
    agenda_.remove_activations_with_fact_id(fact_to_retract->id);

    // Note: Agenda compaction handles stale queue entries lazily.

    tms_->on_fact_retracted(fact_to_retract);

    auto alpha_it = kb_->network().alpha_entry_points.find(fact_to_retract->type);
    if (alpha_it != kb_->network().alpha_entry_points.end()) {
        if (phreak_experimental_) mark_phreak_dirty_for_type(fact_to_retract->type);
        alpha_it->second->right_activate(*this, fact_to_retract, PropagationType::RETRACT);
    }

    // Clean up any remaining WMEs that reference the retracted fact.
    // Note: RETE propagation above already invalidates WMEs it encounters,
    // so this loop only catches stragglers (e.g. orphaned cache entries).
    for (auto it = wme_cache_.begin(); it != wme_cache_.end(); ) {
        if (!it->second) { ++it; continue; }
        bool references_fact = false;
        auto curr = it->second;
        while (curr && curr->depth > 0) {
            if (curr->fact && curr->fact->id == fact_to_retract->id) {
                references_fact = true;
                break;
            }
            curr = curr->parent;
        }
        if (references_fact) {
            logical_retract(it->second);
            it = wme_cache_.erase(it);
        } else {
            ++it;
        }
    }

    release_retained_fact(fact_to_retract->id);
}

void StatefulSession::update_fact(Fact* fact, std::function<void(Fact&)> modifier) {
    if (!fact || !working_memory_.contains(fact->id)) return;
    ensure_consistent_for_mutation("updating facts");
    modifier(*fact);
    auto alpha_it = kb_->network().alpha_entry_points.find(fact->type);
    if (alpha_it != kb_->network().alpha_entry_points.end()) {
        if (phreak_experimental_) mark_phreak_dirty_for_type(fact->type);
        alpha_it->second->right_activate(*this, fact, PropagationType::MODIFY);
    }
}

void StatefulSession::track_rhs_update_snapshot(Fact const& fact) {
    if (!in_rhs_transaction_ || fact.id <= 0) {
        return;
    }
    if (transaction_updated_fact_snapshots_.find(fact.id) != transaction_updated_fact_snapshots_.end()) {
        return;  // First snapshot wins for rollback.
    }
    transaction_updated_fact_order_.push_back(fact.id);
    transaction_updated_fact_snapshots_.emplace(fact.id, fact.fields);
}

void StatefulSession::propagate_modify(Fact* fact,
                                       rulesforge::ModifiedFieldsHint const* changed_fields) {
    if (!fact || !working_memory_.contains(fact->id)) return;
    runtime_counters_.propagate_modify_calls++;
    auto t0 = std::chrono::steady_clock::now();
    current_modified_fields_ = changed_fields;
    auto alpha_it = kb_->network().alpha_entry_points.find(fact->type);
    if (alpha_it != kb_->network().alpha_entry_points.end()) {
        if (phreak_experimental_) mark_phreak_dirty_for_type(fact->type);
        alpha_it->second->right_activate(*this, fact, PropagationType::MODIFY);
    }
    current_modified_fields_ = nullptr;
    auto t1 = std::chrono::steady_clock::now();
    runtime_counters_.propagate_modify_time_us +=
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
}

Fact* StatefulSession::logical_insert(Fact const& fact) {
    ensure_consistent_for_mutation("inserting logical facts");
    Fact* new_fact = fact_arena_.create_fact(fact);
    new_fact->id = working_memory_.reserve_next_id();
    working_memory_.assign_nested_ids(*new_fact);

    // Track for TMS
    if (current_activation_) {
        tms_->add_logical_dependency(current_activation_->hash_value, new_fact->id);
    }

    add_fact(new_fact);
    return new_fact;
}

void StatefulSession::logical_insert(Token& token, Fact* fact) {
    if (!token.wme) return;
    ensure_consistent_for_mutation("inserting logical facts");
    if (!working_memory_.contains(fact->id)) {
        add_fact(fact);
    }
    tms_->add_justification(token.wme, fact);
}

void StatefulSession::logical_retract(TokenWME const* wme) {
    tms_->remove_justifications_by_token(wme);
}

Fact* StatefulSession::get_fact_by_id(int64_t id) {
    return working_memory_.get(id);
}

QueryResult StatefulSession::execute_query(std::string const& query_name,
                                           std::vector<Fact*> const& args) {
    return query_engine_->execute(query_name, args, *this);
}

void StatefulSession::set_focus(std::string const& group_name) {
    ensure_consistent_for_mutation("setting focus");
    agenda_.set_focus(group_name);
}

std::string StatefulSession::get_focus() const {
    return agenda_.get_focus();
}

size_t StatefulSession::get_fact_count() const { return working_memory_.count(); }

int64_t StatefulSession::get_next_fact_id() {
    return working_memory_.next_id();
}

void StatefulSession::set_global(std::string const& name, ConstraintValue value) {
    ensure_consistent_for_mutation("setting globals");
    globals_[name] = std::move(value);
}

std::optional<ConstraintValue> StatefulSession::get_global(std::string const& name) const {
    auto it = globals_.find(name);
    if (it == globals_.end()) return std::nullopt;
    return it->second;
}

bool StatefulSession::has_type_declaration(std::string const& type_name) const {
    return schema_validator_ && schema_validator_->has_declaration(type_name);
}

std::string StatefulSession::canonicalize_fact_type_name(std::string const& type_name) const {
    if (!schema_validator_) {
        return type_name;
    }
    return schema_validator_->canonicalize_type_name(type_name);
}

Fact* StatefulSession::create_fact(std::string const& type) {
    Fact* fact = fact_arena_.create_fact();
    fact->type = canonicalize_fact_type_name(type);
    return fact;
}

bool StatefulSession::execute_eval(std::string const& code, Token const& token,
                                   std::map<std::string, int> const& bindings) {
    if (code.empty()) return true;

    // Compile and evaluate the expression through ExpressionEvaluator
    std::string compile_error;
    auto expr = rulesforge::ExpressionEvaluator::compile(code, &compile_error);
    if (!expr) {
        loge("Failed to compile eval expression '{}': {}", code, compile_error);
        return false;
    }

    return execute_eval(*expr, token, bindings);
}

bool StatefulSession::execute_eval(rulesforge::ExpressionEvaluator const& expr,
                                   Token const& token,
                                   std::map<std::string, int> const& bindings) {

    // Create variable resolver
    auto resolver = [this, &token, &bindings](std::string const& var_name) -> ConstraintValue {
        // Handle $var.field format
        size_t dot_pos = var_name.find('.');
        std::string base_var = (dot_pos != std::string::npos) ? var_name.substr(0, dot_pos) : var_name;
        std::string field_name = (dot_pos != std::string::npos) ? var_name.substr(dot_pos + 1) : "this";

        auto it = bindings.find(base_var);
        if (it == bindings.end()) {
            logw("execute_eval: Variable '{}' not found in bindings", base_var);
            return 0.0;
        }

        auto fact = token.get_fact_at_depth(it->second);
        if (!fact) {
            logw("execute_eval: No fact at depth {} for '{}'", it->second, base_var);
            return 0.0;
        }

        if (field_name == "this" || field_name == "id") {
            return static_cast<double>(fact->id);
        }

        auto field_val = fact->get_field(field_name);
        if (!field_val) {
            logw("execute_eval: Field '{}' not found on '{}'", field_name, base_var);
            return 0.0;
        }

        return *field_val;
    };

    ConstraintValue cv_result = expr.evaluate(resolver);
    double result = 0.0;
    if (std::holds_alternative<double>(cv_result)) {
        result = std::get<double>(cv_result);
    } else if (std::holds_alternative<int64_t>(cv_result)) {
        result = static_cast<double>(std::get<int64_t>(cv_result));
    }
    return result != 0.0;
}

void StatefulSession::addListener(std::shared_ptr<IEngineListener> listener) {
    ensure_consistent_for_mutation("adding listeners");
    if (listener) listeners_.push_back(listener);
}

void StatefulSession::removeListener(std::shared_ptr<IEngineListener> const& listener) {
    ensure_consistent_for_mutation("removing listeners");
    std::erase(listeners_, listener);
}

void StatefulSession::add_activation(Activation const& activation) {
    // Check if this is a no-loop rule that has already fired with these facts (zero-allocation)
    if (activation.rule->no_loop) {
        size_t noloop_key = compute_noloop_key_from_token(activation.rule, activation.token.wme);
        if (agenda_.is_noloop_blocked(noloop_key)) {
            return;
        }
    }

    if (activation.rule->lock_on_active) {
        if (agenda_.is_lock_on_active_blocked(activation.rule)) {
            return;
        }
    }

    // Only build involved_facts for tracer if enabled
    if (tracer_.is_enabled()) {
        std::vector<int64_t> involved_facts;
        if (activation.token.wme) {
            auto token_facts = activation.token.get_facts();
            for (auto const& fact : token_facts) {
                if (fact) involved_facts.push_back(fact->id);
            }
        }
        tracer_.trace_rule_matched(activation.rule->name, involved_facts);
    }
    agenda_.add_with_duration(activation);
    runtime_counters_.agenda_add_calls++;
}

void StatefulSession::add_activations_batch(std::vector<Activation>& activations) {
    if (activations.empty()) return;

    std::vector<Activation> accepted;
    accepted.reserve(activations.size());
    for (auto& activation : activations) {
        // Skip no-loop/lock-on-active checks only if rule uses them
        if (activation.rule->no_loop) {
            size_t noloop_key = compute_noloop_key_from_token(activation.rule, activation.token.wme);
            if (agenda_.is_noloop_blocked(noloop_key)) continue;
        }
        if (activation.rule->lock_on_active) {
            if (agenda_.is_lock_on_active_blocked(activation.rule)) continue;
        }
        accepted.push_back(activation);
    }
    agenda_.add_batch(accepted);
    runtime_counters_.agenda_add_calls += accepted.size();
}

void StatefulSession::reset_runtime_counters() {
    runtime_counters_ = RuntimeCounters{};
}

StatefulSession::RuntimeCounters StatefulSession::runtime_counters() const {
    return runtime_counters_;
}

void StatefulSession::remove_activation(size_t activation_hash) {
    agenda_.remove(activation_hash);
}

std::map<int, std::shared_ptr<ReteNode>> StatefulSession::get_nodes() const {
    std::map<int, std::shared_ptr<ReteNode>> node_map;
    for (auto const& node : kb_->network().all_nodes) { node_map[node->id] = node; }
    return node_map;
}

void StatefulSession::begin_rhs_transaction() {
    in_rhs_transaction_ = true;
    transaction_inserted_facts_.clear();
    transaction_retracted_facts_.clear();
    transaction_updated_fact_order_.clear();
    transaction_updated_fact_snapshots_.clear();
}

void StatefulSession::end_rhs_transaction(bool commit) {
    if (!in_rhs_transaction_) {
        return;  // No transaction in progress
    }

    auto inserted_ids = std::move(transaction_inserted_facts_);
    auto retracted_facts = std::move(transaction_retracted_facts_);
    auto updated_order = std::move(transaction_updated_fact_order_);
    auto updated_snapshots = std::move(transaction_updated_fact_snapshots_);
    transaction_inserted_facts_.clear();
    transaction_retracted_facts_.clear();
    transaction_updated_fact_order_.clear();
    transaction_updated_fact_snapshots_.clear();
    in_rhs_transaction_ = false;

    if (commit) {
        is_consistent_ = true;

        // Process deferred retractions now that the RHS has completed
        // This ensures the activation's token remains valid during RHS execution
        for (Fact* fact : retracted_facts) {
            if (!fact) continue;

            working_memory_.remove(fact->id);
            agenda_.remove_activations_with_fact_id(fact->id);
            tms_->on_fact_retracted(fact);

            auto alpha_it = kb_->network().alpha_entry_points.find(fact->type);
            if (alpha_it != kb_->network().alpha_entry_points.end()) {
                if (phreak_experimental_) mark_phreak_dirty_for_type(fact->type);
                alpha_it->second->right_activate(*this, fact, PropagationType::RETRACT);
            }

            // Clean up WMEs that reference this fact
            for (auto it = wme_cache_.begin(); it != wme_cache_.end(); ) {
                if (!it->second) { ++it; continue; }
                bool references_fact = false;
                auto curr = it->second;
                while (curr && curr->depth > 0) {
                    if (curr->fact && curr->fact->id == fact->id) {
                        references_fact = true;
                        break;
                    }
                    curr = curr->parent;
                }
                if (references_fact) {
                    logical_retract(it->second);
                    it = wme_cache_.erase(it);
                } else {
                    ++it;
                }
            }

            release_retained_fact(fact->id);
        }

        return;
    }

    bool rollback_ok = true;
    logw("Rolling back RHS transaction: inserted={}, retracted={}",
         inserted_ids.size(), retracted_facts.size());

    // Restore update snapshots first
    for (auto it = updated_order.rbegin(); it != updated_order.rend(); ++it) {
        auto snapshot_it = updated_snapshots.find(*it);
        if (snapshot_it == updated_snapshots.end()) {
            continue;
        }
        Fact* fact = get_fact_by_id(*it);
        if (!fact) {
            continue;
        }
        // Validate current state - if update corrupted data, rollback fails
        try {
            validate_fact_for_insert(*fact);
        } catch (...) {
            rollback_ok = false;
            continue;
        }
        fact->fields = snapshot_it->second;
        try {
            propagate_modify(fact, nullptr);
        } catch (...) {
            rollback_ok = false;
        }
    }

    // Roll back inserted facts
    for (auto it = inserted_ids.rbegin(); it != inserted_ids.rend(); ++it) {
        Fact* inserted = get_fact_by_id(*it);
        if (!inserted) {
            continue;
        }
        try {
            retract_fact(inserted);
        } catch (...) {
            rollback_ok = false;
        }
    }

    // Restore retracted facts (skip if still in working memory - deferred retraction)
    for (auto it = retracted_facts.rbegin(); it != retracted_facts.rend(); ++it) {
        Fact* retracted = *it;
        if (!retracted) {
            continue;
        }
        if (working_memory_.contains(retracted->id)) {
            continue;
        }
        try {
            add_fact(retracted);
        } catch (...) {
            rollback_ok = false;
        }
    }

    is_consistent_ = rollback_ok;
    if (!rollback_ok) {
        halt_requested_ = true;
        loge("RHS transaction rollback failed; session marked inconsistent.");
        working_memory_.clear();
    }
}

SessionMetrics StatefulSession::get_metrics() const {
    SessionMetrics metrics;

    // Counters
    metrics.rules_fired_total = rules_fired_total_;
    metrics.facts_inserted_total = facts_inserted_total_;
    metrics.facts_retracted_total = facts_retracted_total_;

    // Gauges
    metrics.facts_count = static_cast<int64_t>(working_memory_.count());
    metrics.memory_used_bytes = arena_.memory_used();
    metrics.memory_max_bytes = arena_.get_max_size();
    metrics.memory_usage_percent = arena_.usage_percent();
    metrics.activations_count = agenda_.size();

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

bool StatefulSession::is_node_pending_dirty(ReteNode const& node) const {
    if (node.mem_slot < 0) return false;
    switch (node.kind) {
        case NodeKind::HashedJoin:
            return static_cast<size_t>(node.mem_slot) < net_mem_.hashed_join.size()
                && net_mem_.hashed_join[node.mem_slot].dirty;
        case NodeKind::CrossProductJoin:
            return static_cast<size_t>(node.mem_slot) < net_mem_.cross_product_join.size()
                && net_mem_.cross_product_join[node.mem_slot].dirty;
        case NodeKind::Not:
        case NodeKind::Exists:
            return static_cast<size_t>(node.mem_slot) < net_mem_.beta_condition.size()
                && net_mem_.beta_condition[node.mem_slot].dirty;
        case NodeKind::Accumulate:
            return static_cast<size_t>(node.mem_slot) < net_mem_.accumulate.size()
                && net_mem_.accumulate[node.mem_slot].dirty;
        default:
            return false;
    }
}

void StatefulSession::mark_phreak_dirty_for_node(int node_id) {
    auto const& net = kb_->network();
    auto it = net.node_to_segment_id.find(node_id);
    if (it == net.node_to_segment_id.end()) return;

    int seg_id = it->second;
    if (seg_id < 0 || static_cast<size_t>(seg_id) >= net_mem_.segment.size()) return;

    auto& seg = net_mem_.segment[seg_id];
    if (seg.dirty_mask == 0) {
        runtime_counters_.phreak_dirty_segment_marks++;
    }
    seg.dirty_mask = 1;
    seg.dirty_epoch = ++phreak_epoch_;

    if (seg_id < 0 || static_cast<size_t>(seg_id) >= net.segment_to_paths.size()) return;
    for (int path_id : net.segment_to_paths[seg_id]) {
        if (path_id < 0 || static_cast<size_t>(path_id) >= net_mem_.path.size()) continue;
        auto& path = net_mem_.path[path_id];
        if (!path.dirty) {
            runtime_counters_.phreak_dirty_path_marks++;
        }
        path.dirty = true;
        path.eval_epoch = phreak_epoch_;
        uint64_t bit = static_cast<uint64_t>(1) << static_cast<uint64_t>(seg_id & 63);
        path.dirty_segments_mask |= bit;
    }
}

void StatefulSession::mark_phreak_dirty_for_type(std::string const& fact_type) {
    auto const& net = kb_->network();
    auto it_segs = net.type_to_segment_ids.find(fact_type);
    if (it_segs != net.type_to_segment_ids.end()) {
        for (int seg_id : it_segs->second) {
            if (seg_id < 0 || static_cast<size_t>(seg_id) >= net_mem_.segment.size()) continue;
            auto& seg = net_mem_.segment[seg_id];
            if (seg.dirty_mask == 0) {
                runtime_counters_.phreak_dirty_segment_marks++;
            }
            seg.dirty_mask = 1;
            seg.dirty_epoch = ++phreak_epoch_;
        }
    }
    auto it_paths = net.type_to_path_ids.find(fact_type);
    if (it_paths != net.type_to_path_ids.end()) {
        for (int path_id : it_paths->second) {
            if (path_id < 0 || static_cast<size_t>(path_id) >= net_mem_.path.size()) continue;
            auto& path = net_mem_.path[path_id];
            if (!path.dirty) {
                runtime_counters_.phreak_dirty_path_marks++;
            }
            path.dirty = true;
            path.eval_epoch = phreak_epoch_;
            // Keep mask generation cheap; path-level mask is diagnostic-only for now.
            path.dirty_segments_mask = 1;
        }
    }
}

void StatefulSession::clear_phreak_dirty_state() {
    for (auto& seg : net_mem_.segment) {
        seg.dirty_mask = 0;
    }
    for (auto& path : net_mem_.path) {
        path.dirty = false;
        path.dirty_segments_mask = 0;
    }
}

void StatefulSession::flush_pending_nodes() {
    auto const& net = kb_->network();
    if (!phreak_experimental_) {
        bool any_flushed = true;
        while (any_flushed) {
            any_flushed = false;
            for (auto const& node : net.all_nodes) {
                if (node->flush_pending(*this)) {
                    any_flushed = true;
                }
            }
        }
        return;
    }

    bool any_flushed = true;
    while (any_flushed) {
        runtime_counters_.phreak_flush_iterations++;
        any_flushed = false;

        // Collect dirty segments via dirty paths first (PHREAK-style path-driven flush).
        std::vector<uint8_t> dirty_segments(net.segment_descriptors.size(), 0);
        bool has_dirty_paths = false;
        for (size_t path_id = 0; path_id < net_mem_.path.size(); ++path_id) {
            auto const& path_mem = net_mem_.path[path_id];
            if (!path_mem.dirty) continue;
            if (path_id >= net.path_descriptors.size()) continue;
            has_dirty_paths = true;
            auto const& path_desc = net.path_descriptors[path_id];
            for (int seg_id : path_desc.segment_ids) {
                if (seg_id < 0 || static_cast<size_t>(seg_id) >= dirty_segments.size()) continue;
                dirty_segments[seg_id] = 1;
            }
        }

        bool flushed_this_round = false;

        if (has_dirty_paths) {
            // Flush only nodes that belong to dirty segments.
            for (size_t seg_id = 0; seg_id < dirty_segments.size(); ++seg_id) {
                if (!dirty_segments[seg_id]) continue;
                auto const& seg_desc = net.segment_descriptors[seg_id];
                for (int node_id : seg_desc.node_ids) {
                    if (node_id < 0 || static_cast<size_t>(node_id) >= net.node_index.size()) continue;
                    auto* node = net.node_index[node_id];
                    if (!node) continue;
                    if (node->flush_pending(*this)) {
                        flushed_this_round = true;
                    }
                    if (is_node_pending_dirty(*node)) {
                        mark_phreak_dirty_for_node(node_id);
                    }
                }
            }
        } else {
            // Safety fallback: no dirty paths were marked, use legacy full scan.
            for (auto const& node : net.all_nodes) {
                if (node->flush_pending(*this)) {
                    flushed_this_round = true;
                }
                if (is_node_pending_dirty(*node)) {
                    mark_phreak_dirty_for_node(node->id);
                }
            }
        }

        any_flushed = flushed_this_round;
    }

    clear_phreak_dirty_state();
}

void StatefulSession::reset() {
    halt_requested_ = false;
    deferred_mode_ = false;
    in_rhs_transaction_ = false;
    is_consistent_ = true;
    current_activation_ = nullptr;
    current_modified_fields_ = nullptr;

    transaction_inserted_facts_.clear();
    transaction_retracted_facts_.clear();
    transaction_updated_fact_order_.clear();
    transaction_updated_fact_snapshots_.clear();

    rules_fired_total_ = 0;
    facts_inserted_total_ = 0;
    facts_retracted_total_ = 0;
    reset_runtime_counters();

    agenda_ = Agenda{};
    wme_cache_.clear();
    working_memory_.clear();
    retained_shared_facts_.clear();
    if (tms_) {
        tms_->clear();
    }
    tracer_.clear_trace();

    globals_.clear();
    for (auto const& g : kb_->get_parser_state().parsed_globals) {
        globals_.emplace(g.name, default_global_value(g.type));
    }

    net_mem_ = NetworkMemory{};
    allocate_network_memory(kb_->network().mem_slot_counts);

    // Facts are session-owned; reset arena after clearing WM/agenda references.
    fact_arena_.reset();
    dummy_wme_ = token_pool_.get_root();

    phreak_epoch_ = 0;
    prime_network_state();
}

void StatefulSession::retain_shared_fact(std::shared_ptr<Fact> const& fact) {
    if (!fact || fact->id <= 0) return;
    retained_shared_facts_[fact->id] = fact;
}

void StatefulSession::release_retained_fact(int64_t fact_id) {
    if (fact_id <= 0) return;
    retained_shared_facts_.erase(fact_id);
}

Fact* StatefulSession::add_fact_from_binary(std::string const& type_name, uint8_t const* buf, size_t len) {
    ensure_consistent_for_mutation("adding fact from binary");

    auto* codec_registry = kb_->get_codec_registry();
    if (!codec_registry) {
        throw std::runtime_error("CodecRegistry not initialized");
    }

    std::string const canonical_type = canonicalize_fact_type_name(type_name);
    Fact* fact = codec_registry->parse_binary(fact_arena_, canonical_type, buf, len);
    if (!fact) {
        throw std::runtime_error("Failed to parse binary: " + codec_registry->get_last_error());
    }

    add_fact(fact);
    return fact;
}

Fact* StatefulSession::add_fact_from_json(std::string const& type_name, std::string const& json_str) {
    ensure_consistent_for_mutation("adding fact from JSON");

    auto* codec_registry = kb_->get_codec_registry();
    if (!codec_registry) {
        throw std::runtime_error("CodecRegistry not initialized");
    }

    std::string const canonical_type = canonicalize_fact_type_name(type_name);
    Fact* fact = codec_registry->parse_json(fact_arena_, canonical_type, json_str);
    if (!fact) {
        throw std::runtime_error("Failed to parse JSON: " + codec_registry->get_last_error());
    }

    add_fact(fact);
    return fact;
}

std::vector<Fact*> StatefulSession::add_facts_from_csv(std::string const& type_name, std::string const& csv_str) {
    ensure_consistent_for_mutation("adding facts from CSV");

    auto* codec_registry = kb_->get_codec_registry();
    if (!codec_registry) {
        throw std::runtime_error("CodecRegistry not initialized");
    }

    std::string const canonical_type = canonicalize_fact_type_name(type_name);
    std::vector<Fact*> facts = codec_registry->parse_csv(fact_arena_, canonical_type, csv_str);
    if (facts.empty()) {
        throw std::runtime_error("Failed to parse CSV: " + codec_registry->get_last_error());
    }

    add_facts(facts);
    return facts;
}

void StatefulSession::add_data(rulesforge::DataSource const& source) {
    ensure_consistent_for_mutation("adding data");

    auto* codec_registry = kb_->get_codec_registry();
    if (!codec_registry) {
        throw std::runtime_error("CodecRegistry not initialized");
    }

    switch (source.type()) {
        case rulesforge::DataSourceType::FACT:
            add_fact(source.get_fact());
            break;

        case rulesforge::DataSourceType::JSON: {
            std::string type_name = resolve_codec_type_name_or_throw(*this, "JSON");
            auto payloads = extract_json_documents_or_throw(source.get_content(), source.get_path_or_filter());
            if (payloads.empty()) {
                break;
            }

            std::vector<Fact*> facts;
            facts.reserve(payloads.size());
            for (auto const& payload : payloads) {
                Fact* fact = codec_registry->parse_json(fact_arena_, type_name, payload);
                if (!fact) {
                    throw std::runtime_error("Failed to parse JSON: " + codec_registry->get_last_error());
                }
                facts.push_back(fact);
            }

            if (facts.size() == 1) {
                add_fact(facts.front());
            } else {
                add_facts(facts);
            }
            break;
        }

        case rulesforge::DataSourceType::CSV: {
            std::string type_name = resolve_codec_type_name_or_throw(*this, "CSV");
            std::string csv_content = read_text_file_or_throw(source.get_content());
            std::vector<Fact*> facts = codec_registry->parse_csv(fact_arena_, type_name, csv_content);
            if (facts.empty()) {
                throw std::runtime_error("Failed to parse CSV: " + codec_registry->get_last_error());
            }
            add_facts(facts);
            break;
        }

        case rulesforge::DataSourceType::DSV: {
            // TODO: Implement DSV parsing
            throw std::runtime_error("DSV data source not yet implemented");
        }

        case rulesforge::DataSourceType::BINARY: {
            std::string type_name = resolve_codec_type_name_or_throw(*this, "binary");
            auto const& binary = source.get_binary();
            Fact* fact = codec_registry->parse_binary(fact_arena_, type_name, binary.data(), binary.size());
            if (!fact) {
                throw std::runtime_error("Failed to parse binary: " + codec_registry->get_last_error());
            }
            add_fact(fact);
            break;
        }
    }
}
