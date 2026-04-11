#include "core/logging_control.hpp"

#include "engine/knowledge_base.hpp"
#include "rete/beta_builder.hpp"
#include "rete/compiled_network.hpp"
#include "rete/rete_node.hpp"

using namespace rulesforge;

namespace {
void collect_leaf_constraints(ConstraintNode const* node, std::vector<ParsedConstraint>& out) {
    if (!node) return;
    if (node->type == NodeType::LEAF) {
        ParsedConstraint c = node->constraint;
        // Inline field-binding metadata is for parser symbol resolution; it should not force
        // a token lookup during runtime comparison.
        c.left_binding = std::nullopt;
        out.push_back(std::move(c));
        return;
    }
    for (auto const& child : node->children) {
        collect_leaf_constraints(child.get(), out);
    }
}
} // namespace

BetaNetworkBuilder::BetaNetworkBuilder(CompiledNetwork& network, KnowledgeBase const& kb,
                                       std::vector<ParsedPattern> const& patterns,
                                       bool is_query, int param_count) :
    network_(network), kb_(kb), patterns_(patterns), is_query_build_(is_query), parameter_count_(param_count),
    first_beta_node_in_chain(nullptr), last_node_(nullptr) {}

std::map<std::string, int> const& BetaNetworkBuilder::get_bindings() const { return binding_to_idx_; }

std::shared_ptr<ReteNode> BetaNetworkBuilder::build() {
    logd("BetaNetworkBuilder::build starting. is_query: {}, param_count: {}", is_query_build_, parameter_count_);
    last_node_ = nullptr;
    binding_to_idx_.clear();
    inline_binding_to_field_.clear();
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
            collect_inline_bindings(pattern.constraint_root.get(), pattern_depth);
        }

        std::shared_ptr<ReteNode> current_node = create_node_for_pattern(pattern, pattern_depth);

        if (last_node_ == nullptr && current_node) {
            first_beta_node_in_chain = current_node;
            logd("  -> Set first beta node in chain to ID {}", current_node->id);
        }

        if (current_node) { last_node_ = current_node; }

        bool adds_fact_to_token =
            pattern.type == PatternType::STANDARD ||
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

std::vector<std::shared_ptr<ReteNode>>
BetaNetworkBuilder::build_alpha_chain(ConstraintNode const* node, std::vector<std::shared_ptr<ReteNode>> parent_tails) {
    if (!node || node->children.empty()) { return parent_tails; }

    std::vector<std::shared_ptr<ReteNode>> current_tails = parent_tails;
    for (auto const& constraint_leaf : node->children) {
        // O(1) alpha sharing via hash map instead of O(N) child scan
        auto alpha_node = network_.find_or_create_alpha(current_tails[0], constraint_leaf->constraint);
        current_tails = {alpha_node};
    }

    return current_tails;
}

void BetaNetworkBuilder::collect_inline_bindings(ConstraintNode const* node, int depth) {
    if (!node) return;
    if (node->type == NodeType::LEAF && node->constraint.field_binding) {
        binding_to_idx_[*node->constraint.field_binding] = depth;
        inline_binding_to_field_[*node->constraint.field_binding] = node->constraint.left_field;
    }
    for (auto const& child : node->children) { collect_inline_bindings(child.get(), depth); }
}

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

    std::vector<ParsedConstraint> join_constraints;
    std::vector<std::shared_ptr<ReteNode>> alpha_tails;

    ParsedPattern* pattern_for_alpha = nullptr;
    if (pattern.type == PatternType::STANDARD) {
        pattern_for_alpha = &pattern;
    } else if (pattern.type == PatternType::NOT || pattern.type == PatternType::EXISTS) {
        if (!pattern.nested_patterns.empty()) { pattern_for_alpha = &pattern.nested_patterns.front(); }
    }

    if (auto* acc_source_ptr = std::get_if<ParsedAccumulate>(&pattern.source)) {
        if (acc_source_ptr->source_pattern) { pattern_for_alpha = acc_source_ptr->source_pattern.get(); }
    }

    if (pattern_for_alpha && !pattern_for_alpha->fact_type.empty()) {
        std::string const* entry_point_name = std::get_if<std::string>(&pattern_for_alpha->source);

        std::shared_ptr<ReteNode> entry;
        if (entry_point_name && !entry_point_name->empty()) {
            auto& named_entry = network_.named_entry_points[*entry_point_name][pattern_for_alpha->fact_type];
            if (!named_entry) {
                named_entry = network_.create_node<EntryPointNode>();
                logd("  -> Created new named EntryPointNode (ID {}) for type '{}' in entry-point '{}'",
                     named_entry->id, pattern_for_alpha->fact_type, *entry_point_name);
            }
            entry = named_entry;
        } else {
            auto& default_entry = network_.alpha_entry_points[pattern_for_alpha->fact_type];
            if (!default_entry) {
                default_entry = network_.create_node<EntryPointNode>();
                logd("  -> Created new EntryPointNode (ID {}) for type '{}'", default_entry->id, pattern_for_alpha->fact_type);
            }
            entry = default_entry;
        }
        auto alpha_root = kb_.partition_and_get_alpha_root(*pattern_for_alpha, join_constraints);
        alpha_tails = build_alpha_chain(alpha_root.get(), {entry});

        if (pattern_for_alpha->window_info.has_value()) {
            std::vector<std::shared_ptr<ReteNode>> new_tails;
            for (auto& tail : alpha_tails) {
                auto window_node = network_.create_node<WindowNode>(*pattern_for_alpha->window_info);
                tail->add_child(window_node);
                new_tails.push_back(window_node);
            }
            alpha_tails = new_tails;
            logd("  -> Attached WindowNode to {} alpha tail(s)", new_tails.size());
        }
    }

    if (std::holds_alternative<ParsedAccumulate>(pattern.source)) {
        auto& acc_source = std::get<ParsedAccumulate>(pattern.source);
        return create_accumulate_node(pattern, acc_source, alpha_tails, join_constraints);
    }
    if (pattern.type == PatternType::NOT) { return create_negative_node(pattern, alpha_tails, join_constraints); }
    if (pattern.type == PatternType::EXISTS) { return create_existential_node(pattern, alpha_tails, join_constraints); }

    return create_standard_node(pattern_depth, alpha_tails, join_constraints);
}

std::shared_ptr<ReteNode>
BetaNetworkBuilder::create_standard_node(int pattern_depth, std::vector<std::shared_ptr<ReteNode>> const& alpha_tails,
                                         std::vector<ParsedConstraint> const& join_constraints) {

    std::vector<std::shared_ptr<ReteNode>> parents = alpha_tails;
    if (last_node_) parents.push_back(last_node_);

    std::shared_ptr<BaseJoinNode> join_node;

    if (!last_node_ && !is_query_build_) {
        // Initial node logic - always CrossProduct
        detail::BetaCacheKey key;
        key.kind = NodeKind::CrossProductJoin;
        key.constraints = join_constraints;
        key.bindings = binding_to_idx_;

        logd("  -> Creating/Sharing initial CrossProductJoinNode with {} join constraints", join_constraints.size());
        join_node = std::static_pointer_cast<BaseJoinNode>(
            network_.find_or_create_beta_node<CrossProductJoinNode>(parents, std::move(key), join_constraints, binding_to_idx_)
        );
        return join_node;
    }

    std::optional<std::pair<std::string, int>> left_hash_info;
    std::optional<std::string> right_hash_field;
    for (auto const& join : join_constraints) {
        if (join.op == CompareOp::EQ && join.right_bound_field) {
            if (auto it_bind = binding_to_idx_.find(join.right_bound_field->first); it_bind != binding_to_idx_.end()) {
                right_hash_field = join.left_field;
                left_hash_info = {{join.right_bound_field->second, it_bind->second}};
                break;
            }
        }
    }

    if (left_hash_info && right_hash_field) {
        logd("  -> Creating/Sharing HashedJoinNode with {} join constraints, left_hash: {}.{}, right_hash: {}",
                  join_constraints.size(), left_hash_info->second, left_hash_info->first, *right_hash_field);

        detail::BetaCacheKey key;
        key.kind = NodeKind::HashedJoin;
        key.constraints = join_constraints;
        key.bindings = binding_to_idx_;
        key.left_hash_info = *left_hash_info;
        key.right_hash_field = *right_hash_field;

        join_node = std::static_pointer_cast<BaseJoinNode>(
            network_.find_or_create_beta_node<HashedJoinNode>(parents, std::move(key),
                join_constraints, binding_to_idx_, *left_hash_info, *right_hash_field)
        );
    } else {
        logd("  -> Creating/Sharing CrossProductJoinNode with {} join constraints", join_constraints.size());

        detail::BetaCacheKey key;
        key.kind = NodeKind::CrossProductJoin;
        key.constraints = join_constraints;
        key.bindings = binding_to_idx_;

        join_node = std::static_pointer_cast<BaseJoinNode>(
             network_.find_or_create_beta_node<CrossProductJoinNode>(parents, std::move(key), join_constraints, binding_to_idx_)
        );
    }

    return join_node;
}

std::shared_ptr<ReteNode>
BetaNetworkBuilder::create_negative_node(ParsedPattern& not_pattern,
                                         std::vector<std::shared_ptr<ReteNode>> const& alpha_tails,
                                         std::vector<ParsedConstraint> const& join_constraints) {
    logd("  -> Creating/Sharing NotNode with {} join constraints", join_constraints.size());
    std::vector<std::shared_ptr<ReteNode>> parents = alpha_tails;
    if (last_node_) parents.push_back(last_node_);

    detail::BetaCacheKey key;
    key.kind = NodeKind::Not;
    key.constraints = join_constraints;
    key.bindings = binding_to_idx_;

    auto node = network_.find_or_create_beta_node<NotNode>(parents, std::move(key), join_constraints, binding_to_idx_);
    return node;
}

std::shared_ptr<ReteNode>
BetaNetworkBuilder::create_existential_node(ParsedPattern& exists_pattern,
                                            std::vector<std::shared_ptr<ReteNode>> const& alpha_tails,
                                            std::vector<ParsedConstraint> const& join_constraints) {
    logd("  -> Creating/Sharing ExistsNode with {} join constraints", join_constraints.size());
    std::vector<std::shared_ptr<ReteNode>> parents = alpha_tails;
    if (last_node_) parents.push_back(last_node_);

    detail::BetaCacheKey key;
    key.kind = NodeKind::Exists;
    key.constraints = join_constraints;
    key.bindings = binding_to_idx_;

    auto node = network_.find_or_create_beta_node<ExistsNode>(parents, std::move(key), join_constraints, binding_to_idx_);
    return node;
}

std::shared_ptr<ReteNode> BetaNetworkBuilder::create_eval_node(ParsedPattern& p) {
    std::vector<std::shared_ptr<ReteNode>> parents;
    if (last_node_) parents.push_back(last_node_);

    detail::BetaCacheKey key;
    key.kind = NodeKind::Eval;
    key.eval_expr = p.eval_expression.value_or("");
    key.bindings = binding_to_idx_;
    key.eval_scalar_fields = inline_binding_to_field_;

    auto node = network_.find_or_create_beta_node<EvalNode>(parents, std::move(key),
        std::move(p.eval_expression.value_or("")), binding_to_idx_, inline_binding_to_field_);

    logd("  -> Created/Shared EvalNode (ID: {}), code: '{}'", node->id, p.eval_expression.value_or(""));
    return node;
}

std::shared_ptr<ReteNode>
BetaNetworkBuilder::create_accumulate_node(ParsedPattern& p, ParsedAccumulate& acc,
                                           std::vector<std::shared_ptr<ReteNode>> const& alpha_tails,
                                           std::vector<ParsedConstraint> const& join_constraints) {
    logd("  -> Creating AccumulateNode: function '{}', result type '{}'", acc.function, p.fact_type);
    auto const* prototype = kb_.get_accumulator_registry().get_prototype(acc.function);
    if (!prototype) throw std::runtime_error("Unknown accumulate function: " + acc.function);
    auto node =
        network_.create_node<AccumulateNode>(prototype, std::move(acc), p.fact_type, binding_to_idx_, join_constraints);

    if (last_node_) {
        logd("    -> Attaching AccumulateNode ID {} to previous node ID {}", node->id, last_node_->id);
        last_node_->add_child(node);
    } else {
        network_.beta_root_nodes.push_back(node.get());
        logd("    -> AccumulateNode ID {} has no beta parent, added to beta_root_nodes", node->id);
    }

    for (auto& tail : alpha_tails) {
        logd("    -> Attaching AccumulateNode ID {} to alpha tail node ID {}", node->id, tail->id);
        tail->add_child(node);
    }
    return node;
}

std::shared_ptr<ReteNode> BetaNetworkBuilder::create_unnest_node(ParsedPattern& p, ParsedUnnest& un) {
    logd("  -> Creating UnnestNode from source binding '{}.{}'", un.source_binding, un.source_field);
    auto node = network_.create_node<UnnestNode>(un, binding_to_idx_);

    if (last_node_) {
        logd("    -> Attaching UnnestNode ID {} to previous node ID {}", node->id, last_node_->id);
        last_node_->add_child(node);
    }
    return node;
}
