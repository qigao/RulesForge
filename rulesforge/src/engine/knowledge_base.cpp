#include "engine/knowledge_base.hpp"
#include "core/logging_control.hpp"

#include "engine/rfl_accumulators.hpp"
#include "rete/beta_builder.hpp"
#include "rete/compiled_network.hpp"
#include "rete/rete_node.hpp"
#include "engine/stateful_session.hpp"

#include <sstream>
#include <unordered_map>

using namespace rulesforge;


KnowledgeBase::KnowledgeBase(private_key) {
    accumulator_registry_ = std::make_shared<AccumulatorRegistry>();
    accumulator_registry_->register_accumulator("count", std::make_unique<CountAccumulator>());
    accumulator_registry_->register_accumulator("sum", std::make_unique<SumAccumulator>());
    accumulator_registry_->register_accumulator("collect", std::make_unique<CollectAccumulator>());
    accumulator_registry_->register_accumulator("collectList", std::make_unique<CollectAccumulator>());
    accumulator_registry_->register_accumulator("collectSet", std::make_unique<CollectSetAccumulator>());
    accumulator_registry_->register_accumulator("average", std::make_unique<AverageAccumulator>());
    accumulator_registry_->register_accumulator("min", std::make_unique<MinAccumulator>());
    accumulator_registry_->register_accumulator("max", std::make_unique<MaxAccumulator>());
}

KnowledgeBase::~KnowledgeBase() {}

std::shared_ptr<KnowledgeBase> KnowledgeBase::create(parser_state&& state) {
    logd("KnowledgeBase::create -> Creating new knowledge base from parser state with {} rules.",
         state.parsed_rules.size());
    auto kb = std::make_shared<KnowledgeBase>(KnowledgeBase::private_key{});
    kb->build(std::move(state));
    return kb;
}

std::unique_ptr<StatefulSession> KnowledgeBase::create_session() {
    logd("KnowledgeBase::create_session -> Creating new stateful session.");
    auto session = std::make_unique<StatefulSession>(StatefulSession::private_key{}, this->shared_from_this());
    session->allocate_network_memory(compiled_network_->mem_slot_counts);
    session->prime_network_state();
    return session;
}

AccumulatorRegistry const& KnowledgeBase::get_accumulator_registry() const { return *accumulator_registry_; }

FactTypeRegistry& KnowledgeBase::get_fact_type_registry() { return fact_type_registry_; }

FactTypeRegistry const& KnowledgeBase::get_fact_type_registry() const { return fact_type_registry_; }
void KnowledgeBase::register_accumulator(std::string const& name, std::unique_ptr<IAccumulator> prototype) {
    logd("KnowledgeBase::register_accumulator -> Registering custom accumulator '{}'", name);
    accumulator_registry_->register_accumulator(name, std::move(prototype));
}

void KnowledgeBase::register_native_function(std::string const& name, NativeFunctionCallback callback, void* user_data) {
    logd("KnowledgeBase::register_native_function -> Registering native function '{}'", name);
    native_functions_[name] = NativeFunction{callback, user_data};
}

void KnowledgeBase::build(parser_state&& state) {
    logd("KnowledgeBase::build -> Building from parser state with {} rules.", state.parsed_rules.size());
    this->parser_state_ = std::move(state);

    // Initialize CodecRegistry when declarations exist.
    if (!parser_state_.parsed_declarations.empty()) {
        codec_registry_ = std::make_unique<rulesforge::CodecRegistry>();
        codec_registry_->load_declarations(parser_state_.parsed_declarations, parser_state_.parsed_enums);
        logd("KnowledgeBase::build -> Loaded {} type declarations and {} enums into CodecRegistry",
             parser_state_.parsed_declarations.size(), parser_state_.parsed_enums.size());
    }

    // Build rule map once for O(1) parent lookup
    std::unordered_map<std::string, ParsedRule const*> rule_map;
    rule_map.reserve(parser_state_.parsed_rules.size());
    for (auto const& rule : parser_state_.parsed_rules) { rule_map[rule.name] = &rule; }

    // Process rule inheritance
    processed_rules_.reserve(parser_state_.parsed_rules.size());
    for (auto const& rule : parser_state_.parsed_rules) {
        if (!rule.parent_rule_name) {
            rule_name_index_[rule.name] = processed_rules_.size();
            processed_rules_.push_back(rule);
            continue;
        }
        if (auto it = rule_map.find(*rule.parent_rule_name); it != rule_map.end()) {
            ParsedRule child_rule = rule;
            ParsedRule const& parent_rule = *it->second;
            logd("KnowledgeBase::build -> Rule '{}' extends '{}'. Merging conditions.", child_rule.name,
                 parent_rule.name);
            if (!parent_rule.condition_groups.empty()) {
                auto const& parent_patterns = parent_rule.condition_groups.front();
                for (auto& child_group : child_rule.condition_groups) {
                    child_group.insert(child_group.begin(), parent_patterns.begin(), parent_patterns.end());
                }
            }
            if (!child_rule.salience_explicitly_set) child_rule.salience = parent_rule.salience;
            if (!child_rule.agenda_group) child_rule.agenda_group = parent_rule.agenda_group;
            rule_name_index_[child_rule.name] = processed_rules_.size();
            processed_rules_.push_back(std::move(child_rule));
        }
    }
    logd("KnowledgeBase::build -> Finished processing rule inheritance. Total processed rules: {}",
         processed_rules_.size());

    compile_network();
}

namespace {
void partition_constraints(ConstraintNode const* node,
                           ConstraintNode& alpha_root,
                           std::vector<ParsedConstraint>& out_join) {
    if (!node) return;
    if (node->type == NodeType::LEAF) {
        if (node->constraint.right_bound_field || node->constraint.temporal_constraint) {
            out_join.push_back(node->constraint);
        } else {
            alpha_root.children.push_back(std::make_unique<ConstraintNode>(*node));
        }
    } else {
        for (auto const& child : node->children) { partition_constraints(child.get(), alpha_root, out_join); }
    }
}
} // namespace

std::unique_ptr<ConstraintNode>
KnowledgeBase::partition_and_get_alpha_root(ParsedPattern const& pattern,
                                            std::vector<ParsedConstraint>& out_join_constraints) const {
    if (!pattern.constraint_root) return nullptr;
    auto alpha_root = std::make_unique<ConstraintNode>(NodeType::AND);
    partition_constraints(pattern.constraint_root.get(), *alpha_root, out_join_constraints);
    if (alpha_root->children.empty()) return nullptr;
    return alpha_root;
}

ParsedRule const* KnowledgeBase::find_rule_by_name(std::string const& name) const {
    if (auto it = rule_name_index_.find(name); it != rule_name_index_.end()) {
        return &processed_rules_[it->second];
    }
    return nullptr;
}

CompiledNetwork const& KnowledgeBase::network() const {
    return *compiled_network_;
}

void KnowledgeBase::compile_network() {
    logd("KnowledgeBase::compile_network -> Compiling RETE network.");
    compiled_network_ = std::make_unique<CompiledNetwork>();

    // Pre-allocate: ~4 nodes per rule (entry shared, 2 alpha, 1 join, 1 terminal)
    // + query nodes. Avoids repeated vector reallocation with atomic refcount copies.
    size_t estimated_nodes = processed_rules_.size() * 4 + parser_state_.parsed_queries.size() * 4;
    compiled_network_->all_nodes.reserve(estimated_nodes);

    for (auto const& rule : processed_rules_) {
        if (!rule.enabled) {
            logd("Skipping disabled rule: '{}'", rule.name);
            continue;
        }
        if (rule.condition_groups.empty() || (rule.condition_groups.size() == 1 && rule.condition_groups[0].empty())) {
            // Rules with no conditions are handled at session creation time (immediate activation)
            continue;
        }

        for (auto const& condition_group : rule.condition_groups) {
            BetaNetworkBuilder builder(*compiled_network_, *this, condition_group, false, 0);
            auto last_node = builder.build();
            auto terminal_node = compiled_network_->create_node<TerminalNode>(rule, builder.get_bindings());
            if (last_node) { last_node->add_child(terminal_node); }
        }
    }

    // Build the network for queries.
    for (auto& query : parser_state_.parsed_queries) {
        BetaNetworkBuilder builder(*compiled_network_, *this, query.patterns, true, query.parameter_count);
        auto last_node = builder.build();
        auto query_terminal_node = compiled_network_->create_node<QueryTerminalNode>(builder.get_bindings());
        compiled_network_->query_nodes[query.name] = query_terminal_node;
        if (last_node) { last_node->add_child(query_terminal_node); }

        if (query.parameter_count > 0) {
            auto query_input_node = compiled_network_->create_node<QueryInputNode>(query_terminal_node);
            compiled_network_->parameterized_query_inputs[query.name] = query_input_node;
            if (builder.first_beta_node_in_chain) {
                query_input_node->add_child(builder.first_beta_node_in_chain);
            } else if (!last_node) {
                logd("  -> Query '{}' has no body, connecting input directly to terminal.", query.name);
                query_input_node->add_child(query_terminal_node);
            }
        }
    }

    // Build PHREAK skeleton metadata (segments/paths). This is structural-only for now.
    compiled_network_->build_phreak_skeleton();

    logd("KnowledgeBase::compile_network -> Done. Total nodes: {}, mem_slots: seg={} path={} hj={} cpj={} bc={} acc={} t={} qt={} ev={} un={} jp={}",
         compiled_network_->all_nodes.size(),
         compiled_network_->mem_slot_counts.segment,
         compiled_network_->mem_slot_counts.path,
         compiled_network_->mem_slot_counts.hashed_join,
         compiled_network_->mem_slot_counts.cross_product_join,
         compiled_network_->mem_slot_counts.beta_condition,
         compiled_network_->mem_slot_counts.accumulate,
         compiled_network_->mem_slot_counts.terminal,
         compiled_network_->mem_slot_counts.query_terminal,
         compiled_network_->mem_slot_counts.eval,
         compiled_network_->mem_slot_counts.unnest);
}
