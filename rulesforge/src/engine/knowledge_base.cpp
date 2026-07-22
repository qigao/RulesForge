#include "engine/knowledge_base.hpp"
#include "core/logging_control.hpp"

#include "engine/rfl_accumulators.hpp"
#include "engine/rhs_backend_plan.hpp"
#include "engine/runtime_predicate.hpp"
#include "rete/beta_builder.hpp"
#include "rete/compiled_network.hpp"
#include "rete/rete_node.hpp"
#include "engine/stateful_session.hpp"

#include <data_bind.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <variant>

using namespace rulesforge;

namespace {
std::string trim_ascii(std::string value) {
    auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::optional<bool> parse_native_predicate_result(char const* out_result) {
    if (out_result == nullptr) {
        return std::nullopt;
    }
    std::string raw = trim_ascii(out_result);
    if (raw == "true" || raw == "TRUE" || raw == "1") {
        return true;
    }
    if (raw == "false" || raw == "FALSE" || raw == "0") {
        return false;
    }
    return std::nullopt;
}

std::string native_predicate_arg_to_string(ConstraintValue const& value) {
    return std::visit(
        [](auto const& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return arg;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return std::to_string(arg);
            } else if constexpr (std::is_same_v<T, double>) {
                return std::to_string(arg);
            } else if constexpr (std::is_same_v<T, NilValue>) {
                return "nil";
            } else {
                return to_string(ConstraintValue{arg});
            }
        },
        value);
}

} // namespace

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

std::shared_ptr<KnowledgeBase> KnowledgeBase::create_empty() {
    logd("KnowledgeBase::create_empty -> Creating empty knowledge base.");
    auto kb = std::make_shared<KnowledgeBase>(KnowledgeBase::private_key{});
    kb->compiled_network_ = std::make_unique<CompiledNetwork>();
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

DataBind* KnowledgeBase::find_data_bind_codec(std::string const& type_name) const {
    auto it = data_bind_type_schema_index_.find(type_name);
    if (it == data_bind_type_schema_index_.end()
        || it->second >= parser_state_.imported_data_bind_schemas.size()) {
        return nullptr;
    }
    return parser_state_.imported_data_bind_schemas[it->second].codec.get();
}

std::string const* KnowledgeBase::find_data_bind_schema_path(std::string const& type_name) const {
    auto it = data_bind_type_schema_index_.find(type_name);
    if (it == data_bind_type_schema_index_.end()
        || it->second >= parser_state_.imported_data_bind_schemas.size()) {
        return nullptr;
    }
    return &parser_state_.imported_data_bind_schemas[it->second].path;
}

bool KnowledgeBase::has_data_bind_type(std::string const& type_name) const {
    return find_data_bind_codec(type_name) != nullptr;
}

rulesforge::RhsBackendPlanSummary KnowledgeBase::rhs_backend_summary() const {
    return rhs_backend_plan_ ? rhs_backend_plan_->summary() : rulesforge::RhsBackendPlanSummary{};
}

std::string KnowledgeBase::rhs_backend_summary_text() const {
    if (rhs_backend_plan_) {
        return rhs_backend_plan_->format_summary();
    }
    return "RHS backend summary: cpp_action_plan_available=false, rule_coverage=0, cpp_action_plan_rules=0, compile_error_rules=0, commands=0, conditions=0, lowering_errors=rhs_backend_plan_unavailable";
}

std::string KnowledgeBase::rhs_backend_debug_dump() const {
    if (rhs_backend_plan_) {
        return rhs_backend_plan_->format_debug_dump();
    }
    return rhs_backend_summary_text() + "\nRHS backend coverage:\n  none\n";
}

std::vector<rulesforge::RhsBackendCoverage> const& KnowledgeBase::rhs_backend_coverages() const {
    static std::vector<rulesforge::RhsBackendCoverage> const empty;
    return rhs_backend_plan_ ? rhs_backend_plan_->coverages() : empty;
}

std::optional<bool> KnowledgeBase::runtime_predicate(
    rulesforge::RuntimePredicateRef predicate,
    std::vector<ConstraintValue> const& args) const {
    switch (predicate.backend) {
        case rulesforge::RuntimePredicateBackend::Native:
            if (!predicate.external_name.empty()) {
                auto id = native_predicate_id(predicate.external_name);
                if (!id) {
                    return std::nullopt;
                }
                return run_native_predicate(*id, args);
            }
            return run_native_predicate(predicate.predicate_id, args);
    }
    return std::nullopt;
}

void KnowledgeBase::register_accumulator(std::string const& name, std::unique_ptr<IAccumulator> prototype) {
    logd("KnowledgeBase::register_accumulator -> Registering custom accumulator '{}'", name);
    accumulator_registry_->register_accumulator(name, std::move(prototype));
}

std::size_t KnowledgeBase::register_native_predicate(std::string const& name,
                                                     NativeFunctionCallback callback,
                                                     void* user_data) {
    if (name.empty()) {
        throw std::invalid_argument("native predicate name must not be empty");
    }
    if (callback == nullptr) {
        throw std::invalid_argument("native predicate callback must not be null");
    }

    auto existing = native_predicate_name_to_id_.find(name);
    if (existing != native_predicate_name_to_id_.end()) {
        native_predicates_[existing->second] = NativeFunction{callback, user_data};
        return existing->second;
    }

    std::size_t const predicate_id = native_predicates_.size();
    native_predicates_.push_back(NativeFunction{callback, user_data});
    native_predicate_name_to_id_[name] = predicate_id;
    return predicate_id;
}

std::optional<std::size_t> KnowledgeBase::native_predicate_id(std::string const& name) const {
    auto it = native_predicate_name_to_id_.find(name);
    if (it == native_predicate_name_to_id_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<bool> KnowledgeBase::run_native_predicate(
    std::size_t predicate_id,
    std::vector<ConstraintValue> const& args) const {
    if (predicate_id >= native_predicates_.size()) {
        return std::nullopt;
    }

    auto const& predicate = native_predicates_[predicate_id];
    if (predicate.callback == nullptr) {
        return std::nullopt;
    }

    std::vector<std::string> arg_storage;
    std::vector<char const*> argv;
    arg_storage.reserve(args.size());
    argv.reserve(args.size());
    for (auto const& arg : args) {
        arg_storage.push_back(native_predicate_arg_to_string(arg));
        argv.push_back(arg_storage.back().c_str());
    }

    char* out_result = nullptr;
    int const status = predicate.callback(predicate.user_data,
                                          static_cast<int>(argv.size()),
                                          argv.empty() ? nullptr : argv.data(),
                                          &out_result);
    if (status != 0) {
        if (out_result != nullptr) {
            std::free(out_result);
        }
        return std::nullopt;
    }

    auto parsed = parse_native_predicate_result(out_result);
    if (out_result != nullptr) {
        std::free(out_result);
    }
    return parsed;
}

void KnowledgeBase::build(parser_state&& state) {
    logd("KnowledgeBase::build -> Building from parser state with {} rules.", state.parsed_rules.size());
    this->parser_state_ = std::move(state);

    auto index_data_bind_type = [this](std::string const& type_name,
                                       std::string const& schema_source) {
        for (std::size_t i = 0; i < parser_state_.imported_data_bind_schemas.size(); ++i) {
            if (parser_state_.imported_data_bind_schemas[i].path == schema_source) {
                data_bind_type_schema_index_.emplace(type_name, i);
                auto const dot = type_name.find_last_of('.');
                if (dot != std::string::npos) {
                    data_bind_type_schema_index_.emplace(type_name.substr(dot + 1), i);
                }
                return;
            }
        }
    };
    for (auto const& decl : parser_state_.parsed_declarations) {
        auto source = decl.annotations.find("schema_source");
        if (source != decl.annotations.end()) {
            index_data_bind_type(decl.type_name, source->second);
        }
    }
    for (auto const& enum_decl : parser_state_.parsed_enums) {
        auto source = enum_decl.annotations.find("schema_source");
        if (source != enum_decl.annotations.end()) {
            index_data_bind_type(enum_decl.enum_name, source->second);
        }
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

    rhs_backend_plan_ = rulesforge::RhsBackendPlan::compile(processed_rules_);
    logd("KnowledgeBase::build -> {}", rhs_backend_plan_->format_summary());
    for (auto const& coverage : rhs_backend_plan_->coverages()) {
        if (coverage.backend == rulesforge::RhsBackendKind::CompileError) {
            std::ostringstream message;
            message << "RHS backend lowering failed for rule '" << coverage.rule_name << "'";
            if (!coverage.lowering_errors.empty()) {
                message << ": ";
                for (std::size_t index = 0; index < coverage.lowering_errors.size(); ++index) {
                    if (index != 0) {
                        message << "|";
                    }
                    message << coverage.lowering_errors[index];
                }
            }
            throw std::runtime_error(message.str());
        }
    }

    compile_network();
}

namespace {
bool expression_references_dotted_binding(std::string const& expression) {
    for (std::size_t pos = 0; pos < expression.size(); ++pos) {
        if (expression[pos] != '$') {
            continue;
        }
        ++pos;
        while (pos < expression.size()) {
            unsigned char const ch = static_cast<unsigned char>(expression[pos]);
            if (!std::isalnum(ch) && expression[pos] != '_') {
                break;
            }
            ++pos;
        }
        if (pos < expression.size() && expression[pos] == '.') {
            return true;
        }
    }
    return false;
}

void partition_constraints(ConstraintNode const* node,
                           ConstraintNode& alpha_root,
                           std::vector<ParsedConstraint>& out_join) {
    if (!node) return;
    if (node->type == NodeType::LEAF) {
        if (node->constraint.right_bound_field
            || node->constraint.temporal_constraint
            || (node->constraint.right_arith_expr
                && expression_references_dotted_binding(*node->constraint.right_arith_expr))) {
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

std::string KnowledgeBase::network_dot() const {
    return compiled_network_ ? compiled_network_->to_dot() : "digraph ReteNetwork {\n}\n";
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

    logd("KnowledgeBase::compile_network -> Done. Total nodes: {}, mem_slots: alpha={} seg={} path={} hj={} cpj={} bc={} acc={} t={} qt={} ev={} un={} win={}",
         compiled_network_->all_nodes.size(),
         compiled_network_->mem_slot_counts.alpha,
         compiled_network_->mem_slot_counts.segment,
         compiled_network_->mem_slot_counts.path,
         compiled_network_->mem_slot_counts.hashed_join,
         compiled_network_->mem_slot_counts.cross_product_join,
         compiled_network_->mem_slot_counts.beta_condition,
         compiled_network_->mem_slot_counts.accumulate,
         compiled_network_->mem_slot_counts.terminal,
         compiled_network_->mem_slot_counts.query_terminal,
         compiled_network_->mem_slot_counts.eval,
         compiled_network_->mem_slot_counts.unnest,
         compiled_network_->mem_slot_counts.window);
}
