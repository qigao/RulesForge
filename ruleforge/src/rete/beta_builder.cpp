#include "logging_control.hpp"

#include "knowledge_base.hpp"
#include "rete/beta_builder.hpp"
#include "rete/rete_node.hpp"
#include "stateful_session.hpp"



BetaNetworkBuilder::BetaNetworkBuilder(StatefulSession& session, std::vector<ParsedPattern> const& patterns,
                                       bool is_query, int param_count) :
    session_(session), patterns_(patterns), is_query_build_(is_query), parameter_count_(param_count),
    first_beta_node_in_chain(nullptr), last_node_(nullptr) {}

map<std::string, int> const& BetaNetworkBuilder::get_bindings() const { return binding_to_idx_; }

std::shared_ptr<ReteNode> BetaNetworkBuilder::build() {
    logd("BetaNetworkBuilder::build starting. is_query: {}, param_count: {}", is_query_build_, parameter_count_);
    last_node_ = nullptr;
    binding_to_idx_.clear();
    first_beta_node_in_chain = nullptr;
    int pattern_depth = 0;

    for (size_t i = 0; i < patterns_.size(); ++i) {
        ParsedPattern& pattern = patterns_[i];

        if (is_query_build_ && i < parameter_count_) {
            logd("  -> Processing query parameter pattern {}/{} at depth {}", i, parameter_count_, pattern_depth);
            if (!pattern.binding.empty()) { binding_to_idx_[pattern.binding] = pattern_depth; }
            pattern_depth++;
            continue;
        }

        logd("  -> Processing pattern {} at depth {}", i, pattern_depth);
        if (!pattern.binding.empty()) { binding_to_idx_[pattern.binding] = pattern_depth; }

        if (pattern.constraint_root) {
            std::function<void(ConstraintNode*)> find_inline_bindings = [&](ConstraintNode* node) {
                if (!node) return;
                if (node->type == NodeType::LEAF && node->constraint.field_binding) {
                    binding_to_idx_[*node->constraint.field_binding] = pattern_depth;
                    logd("    -> Found inline binding '{}' at depth {}", *node->constraint.field_binding,
                              pattern_depth);
                }
                for (auto& child : node->children) { find_inline_bindings(child.get()); }
            };
            find_inline_bindings(pattern.constraint_root.get());
        }

        std::shared_ptr<ReteNode> current_node = create_node_for_pattern(pattern, pattern_depth);

        if (last_node_ == nullptr && current_node) {
            first_beta_node_in_chain = current_node;
            // The priming of beta chains is now handled centrally in StatefulSession::prime_network_state()
            // to ensure it happens after the full network is built and linked.
            logd("  -> Set first beta node in chain to ID {}", current_node->id);
        }

        if (current_node) { last_node_ = current_node; }

        bool adds_fact_to_token =
            (pattern.type == PatternType::STANDARD && std::holds_alternative<std::monostate>(pattern.source)) ||
            std::holds_alternative<ParsedAccumulate>(pattern.source) ||
            std::holds_alternative<ParsedUnnest>(pattern.source);
        if (adds_fact_to_token) {
            logd("    -> Pattern adds fact to token, incrementing depth to {}", pattern_depth + 1);
            pattern_depth++;
        }
    }

    logd("BetaNetworkBuilder::build finished. Last node ID: {}", last_node_ ? last_node_->id : -1);
    return last_node_;
}

// In beta_builder.cpp

std::shared_ptr<ReteNode> BetaNetworkBuilder::create_node_for_pattern(ParsedPattern& pattern, int& pattern_depth) {
    logd("Entering BetaNetworkBuilder::create_node_for_pattern for pattern type: {}, fact_type: {}",
              ENUM_NAME(pattern.type), pattern.fact_type);

    if (pattern.type == PatternType::EVAL) {
        logd("Creating EvalNode");
        return create_eval_node(pattern);
    }
    if (std::holds_alternative<ParsedUnnest>(pattern.source)) {
        auto& un_source = std::get<ParsedUnnest>(pattern.source);
        return create_unnest_node(pattern, un_source);
    }

    // The core idea is to first build the "right input" (the alpha network)
    // and then create the correct beta node, connecting the left and right inputs.

    std::vector<ParsedConstraint> join_constraints;
    std::vector<std::shared_ptr<ReteNode>> alpha_tails;

    // 1. Determine which pattern provides the facts for the right input and build its alpha network.
    ParsedPattern* pattern_for_alpha = nullptr;
    if (pattern.type == PatternType::STANDARD) {
        pattern_for_alpha = &pattern;
    } else if (pattern.type == PatternType::NOT || pattern.type == PatternType::EXISTS) {
        if (!pattern.nested_patterns.empty()) { pattern_for_alpha = &pattern.nested_patterns.front(); }
    }

    // Special handling for Accumulate's source pattern
    if (auto* acc_source_ptr = std::get_if<ParsedAccumulate>(&pattern.source)) {
        if (acc_source_ptr->source_pattern) { pattern_for_alpha = acc_source_ptr->source_pattern.get(); }
    }

    if (pattern_for_alpha && !pattern_for_alpha->fact_type.empty()) {
        std::string const* entry_point_name = std::get_if<std::string>(&pattern_for_alpha->source);

        std::shared_ptr<ReteNode> entry;
        if (entry_point_name && !entry_point_name->empty()) {
            // Use named entry point for CEP streams
            auto& named_entry = session_.named_entry_points_[*entry_point_name][pattern_for_alpha->fact_type];
            if (!named_entry) {
                named_entry = session_.create_node<EntryPointNode>();
                logd("  -> Created new named EntryPointNode (ID {}) for type '{}' in entry-point '{}'",
                     named_entry->id, pattern_for_alpha->fact_type, *entry_point_name);
            }
            entry = named_entry;
        } else {
            // Use default entry point
            auto& default_entry = session_.alpha_entry_points_[pattern_for_alpha->fact_type];
            if (!default_entry) {
                default_entry = session_.create_node<EntryPointNode>();
                logd("  -> Created new EntryPointNode (ID {}) for type '{}'", default_entry->id, pattern_for_alpha->fact_type);
            }
            entry = default_entry;
        }
        auto alpha_root = session_.kb_->partition_and_get_alpha_root(*pattern_for_alpha, join_constraints);
        alpha_tails = session_.build_alpha_chain(alpha_root.get(), {entry});
    }

    // 2. Now, create the correct beta node based on the original pattern's type.
    if (std::holds_alternative<ParsedAccumulate>(pattern.source)) {
        auto& acc_source = std::get<ParsedAccumulate>(pattern.source);
        return create_accumulate_node(pattern, acc_source, alpha_tails, join_constraints);
    }
    if (pattern.type == PatternType::NOT) { return create_negative_node(pattern, alpha_tails, join_constraints); }
    if (pattern.type == PatternType::EXISTS) { return create_existential_node(pattern, alpha_tails, join_constraints); }

    // Default is a standard join node.
    return create_standard_node(pattern_depth, alpha_tails, join_constraints);
}

std::shared_ptr<ReteNode>
BetaNetworkBuilder::create_standard_node(int pattern_depth, std::vector<std::shared_ptr<ReteNode>> const& alpha_tails,
                                         std::vector<ParsedConstraint> const& join_constraints) {
    // For the very first pattern of a rule, we need a join node that can accept
    // the initial dummy token. A CrossProductJoinNode is suitable for this.
    if (!last_node_ && !is_query_build_) {
        logd("  -> Creating initial CrossProductJoinNode with {} join constraints", join_constraints.size());
        auto join_node = session_.create_node<CrossProductJoinNode>(join_constraints, binding_to_idx_);
        for (auto& alpha_tail : alpha_tails) { alpha_tail->add_child(join_node); }
        return join_node;
    }

    std::shared_ptr<BaseJoinNode> join_node;

    // A join can be hashed if there is an equality constraint between a field in the new pattern
    // and a field in a previous pattern (which includes query parameters).
    std::optional<std::pair<std::string, int>> left_hash_info;
    std::optional<std::string> right_hash_field;
    for (auto const& join : join_constraints) {
        if (join.op == "==" && join.right_bound_field) {
            if (auto it_bind = binding_to_idx_.find(join.right_bound_field->first); it_bind != binding_to_idx_.end()) {
                right_hash_field = join.left_field;
                left_hash_info = {{join.right_bound_field->second, it_bind->second}};
                break;
            }
        }
    }

    if (left_hash_info && right_hash_field) {
        logd("  -> Creating HashedJoinNode with {} join constraints, left_hash: {}.{}, right_hash: {}",
                  join_constraints.size(), left_hash_info->second, left_hash_info->first, *right_hash_field);
        join_node =
            session_.create_node<HashedJoinNode>(join_constraints, binding_to_idx_, *left_hash_info, *right_hash_field);
    } else {
        logd("  -> Creating CrossProductJoinNode with {} join constraints", join_constraints.size());
        join_node = session_.create_node<CrossProductJoinNode>(join_constraints, binding_to_idx_);
    }

    if (last_node_) { last_node_->add_child(join_node); }
    // If last_node_ is null, this is the first beta node in the chain. Its priming is now handled
    // in the main build() loop.

    // Connect the alpha network for this pattern to the right input of the new join node.
    for (auto& alpha_tail : alpha_tails) { alpha_tail->add_child(join_node); }
    return join_node;
}

std::shared_ptr<ReteNode>
BetaNetworkBuilder::create_negative_node(ParsedPattern& not_pattern,
                                         std::vector<std::shared_ptr<ReteNode>> const& alpha_tails,
                                         std::vector<ParsedConstraint> const& join_constraints) {
    logd("  -> Creating NotNode with {} join constraints", join_constraints.size());
    auto node = session_.create_node<NotNode>(join_constraints, binding_to_idx_);
    if (last_node_) { last_node_->add_child(node); }
    for (auto& tail : alpha_tails) { tail->add_child(node); }
    return node;
}

std::shared_ptr<ReteNode>
BetaNetworkBuilder::create_existential_node(ParsedPattern& exists_pattern,
                                            std::vector<std::shared_ptr<ReteNode>> const& alpha_tails,
                                            std::vector<ParsedConstraint> const& join_constraints) {
    logd("  -> Creating ExistsNode with {} join constraints", join_constraints.size());
    auto node = session_.create_node<ExistsNode>(join_constraints, binding_to_idx_);
    if (last_node_) { last_node_->add_child(node); }
    for (auto& tail : alpha_tails) { tail->add_child(node); }
    return node;
}

std::shared_ptr<ReteNode> BetaNetworkBuilder::create_eval_node(ParsedPattern& p) {
    auto node = session_.create_node<EvalNode>(std::move(p.eval_expression.value_or("")), binding_to_idx_);
    logd("  -> Created EvalNode (ID: {}), code: '{}'", node->id, p.eval_expression.value_or(""));
    if (last_node_) {
        logd("    -> Attaching EvalNode ID {} to previous node ID {}", node->id, last_node_->id);
        last_node_->add_child(node);
    }
    return node;
}

std::shared_ptr<ReteNode>
BetaNetworkBuilder::create_accumulate_node(ParsedPattern& p, ParsedAccumulate& acc,
                                           std::vector<std::shared_ptr<ReteNode>> const& alpha_tails,
                                           std::vector<ParsedConstraint> const& join_constraints) {
    logd("  -> Creating AccumulateNode: function '{}', result type '{}'", acc.function, p.fact_type);
    auto const* prototype = session_.kb_->get_accumulator_registry().get_prototype(acc.function);
    if (!prototype) throw std::runtime_error("Unknown accumulate function: " + acc.function);
    auto node =
        session_.create_node<AccumulateNode>(prototype, std::move(acc), p.fact_type, binding_to_idx_, join_constraints);

    if (last_node_) {
        logd("    -> Attaching AccumulateNode ID {} to previous node ID {}", node->id, last_node_->id);
        last_node_->add_child(node);
    }

    for (auto& tail : alpha_tails) {
        logd("    -> Attaching AccumulateNode ID {} to alpha tail node ID {}", node->id, tail->id);
        tail->add_child(node);
    }
    return node;
}

std::shared_ptr<ReteNode> BetaNetworkBuilder::create_unnest_node(ParsedPattern& p, ParsedUnnest& un) {
    logd("  -> Creating UnnestNode from source binding '{}.{}'", un.source_binding, un.source_field);
    auto node = session_.create_node<UnnestNode>(un, binding_to_idx_);

    if (last_node_) {
        logd("    -> Attaching UnnestNode ID {} to previous node ID {}", node->id, last_node_->id);
        last_node_->add_child(node);
    }
    return node;
}


