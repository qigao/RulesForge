#include "knowledge_base.hpp"
#include "logging_control.hpp"

#include "rfl_accumulators.hpp"
#include "rete/beta_builder.hpp"
#include "stateful_session.hpp"

#include <iostream>
#include <sstream>


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

std::shared_ptr<KnowledgeBase> KnowledgeBase::create(parser_state& state) {
    logd("KnowledgeBase::create -> Creating new knowledge base from parser state with {} rules.",
         state.parsed_rules.size());
    auto kb = std::make_shared<KnowledgeBase>(KnowledgeBase::private_key{});
    kb->build(state);
    return kb;
}

std::unique_ptr<StatefulSession> KnowledgeBase::create_session() {
    logd("KnowledgeBase::create_session -> Creating new stateful session.");
    auto session = std::make_unique<StatefulSession>(StatefulSession::private_key{}, this->shared_from_this());
    session->build_network();
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

void KnowledgeBase::build(parser_state& state) {
    logd("KnowledgeBase::build -> Building from parser state with {} rules.", state.parsed_rules.size());
    this->parser_state_ = state;

    // Process rule inheritance to create the final set of rules for the blueprint.
    map<std::string, ParsedRule> rule_map;
    for (auto const& rule : state.parsed_rules) { rule_map[rule.name] = rule; }
    for (auto const& rule : state.parsed_rules) {
        if (!rule.parent_rule_name) {
            processed_rules_.push_back(rule);
            continue;
        }
        if (auto it = rule_map.find(*rule.parent_rule_name); it != rule_map.end()) {
            ParsedRule child_rule = rule;
            ParsedRule const& parent_rule = it->second;
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
            processed_rules_.push_back(child_rule);
        }
    }
    logd("KnowledgeBase::build -> Finished processing rule inheritance. Total processed rules: {}",
         processed_rules_.size());
}

std::unique_ptr<ConstraintNode>
KnowledgeBase::partition_and_get_alpha_root(ParsedPattern const& pattern,
                                            std::vector<ParsedConstraint>& out_join_constraints) const {
    if (!pattern.constraint_root) return nullptr;
    logd("KnowledgeBase::partition_and_get_alpha_root for pattern with fact type '{}'", pattern.fact_type);
    auto alpha_root = std::make_unique<ConstraintNode>(NodeType::AND);
    std::function<void(ConstraintNode*)> process_node = [&](ConstraintNode* node) {
        if (!node) return;
        if (node->type == NodeType::LEAF) {
            if (node->constraint.right_bound_field || node->constraint.temporal_constraint) {
                logd("  -> Partitioned to JOIN: {}", constraint_to_string(node->constraint));
                out_join_constraints.push_back(node->constraint);
            } else {
                logd("  -> Partitioned to ALPHA: {}", constraint_to_string(node->constraint));
                alpha_root->children.push_back(std::make_unique<ConstraintNode>(*node));
            }
        } else {
            for (auto& child : node->children) { process_node(child.get()); }
        }
    };
    process_node(pattern.constraint_root.get());
    if (alpha_root->children.empty()) return nullptr;
    return alpha_root;
}

ParsedRule const* KnowledgeBase::find_rule_by_name(std::string const& name) const {
    for (auto const& rule : processed_rules_) {
        if (rule.name == name) { return &rule; }
    }
    return nullptr;
}


