#include "ast_transformer.hpp"
#include "fmtlog.h"

#include "drools_rete_defs.hpp"   // For ConstraintNode, NodeType, etc.

#include <iostream>
#include <magic_enum/magic_enum.hpp>
#include <memory>   // For std::unique_ptr

// Helper function to correctly negate a constraint, with special handling for booleans.
void negate_constraint(ParsedConstraint& c) {
    logd("Negating constraint: {}.{} {} {}", c.left_binding.value_or("fact"), c.left_field, c.op,
         c.right_literal ? to_string(*c.right_literal) : "RHS_VAR");

    if (c.op == "==")
        c.op = "!=";
    else if (c.op == "!=")
        c.op = "==";
    else if (c.op == ">")
        c.op = "<=";
    else if (c.op == "<=")
        c.op = ">";
    else if (c.op == ">=")
        c.op = "<";
    else if (c.op == "<")
        c.op = ">=";
    else if (c.op.empty()) {
        // This is the negation of an existence check, e.g., "not Person(name)".
        // It becomes a check for nil.
        c.op = "==";
        c.right_literal = NilValue{};
    }
    logd("  -> New constraint: {}.{} {} {}", c.left_binding.value_or("fact"), c.left_field, c.op,
         c.right_literal ? to_string(*c.right_literal) : "RHS_VAR");
}

// Helper function to recursively negate a constraint tree using De Morgan's laws.
void negate_constraint_tree(std::unique_ptr<ConstraintNode>& root) {
    if (!root) return;
    logd("Negating constraint tree. Initial type: {}", magic_enum::enum_name(root->type));

    if (root->type == NodeType::LEAF) {
        negate_constraint(root->constraint);
        return;
    }

    // Apply De Morgan's Laws: not(A and B) -> not(A) or not(B)
    if (root->type == NodeType::AND)
        root->type = NodeType::OR;
    else if (root->type == NodeType::OR)
        root->type = NodeType::AND;

    logd("Negated constraint tree. Final type: {}", magic_enum::enum_name(root->type));
    for (auto& child : root->children) { negate_constraint_tree(child); }
}

// The constructor must initialize the reference member 'state_'.
AstTransformer::AstTransformer(parser_state& st) : state_(st) {}

void AstTransformer::transform() {
    logd("AstTransformer::transform() starting for {} rules and {} queries.", state_.parsed_rules.size(),
         state_.parsed_queries.size());
    for (auto& rule : state_.parsed_rules) {
        logd("AstTransformer transforming rule '{}'", rule.name);
        for (auto& group : rule.condition_groups) { expand_foralls_in_list(group); }
    }

    for (auto& query : state_.parsed_queries) {
        logd("AstTransformer transforming query '{}'", query.name);
        expand_foralls_in_list(query.patterns);
    }
    logd("AstTransformer::transform() finished.");
}

void AstTransformer::expand_foralls_in_list(std::vector<ParsedPattern>& patterns) {
    logd("AstTransformer::expand_foralls_in_list() called with {} patterns.", patterns.size());
    // We must use an index-based loop or iterators because we are modifying the vector.
    for (auto it = patterns.begin(); it != patterns.end(); ++it) {
        auto& pattern = *it;
        logd("AstTransformer::expand_foralls_in_list() processing pattern type {}",
             magic_enum::enum_name(pattern.type));
        // Recurse into nested patterns first
        if (!pattern.nested_patterns.empty()) { expand_foralls_in_list(pattern.nested_patterns); }

        if (pattern.type == PatternType::FORALL) {
            logd("Expanding 'forall' pattern.");
            if (!pattern.forall_info || pattern.forall_info->patterns.size() < 2) {
                logw("'forall' pattern is invalid or has fewer than two sub-patterns. Skipping.");
                continue;   // Not a valid forall, leave it to the semantic analyzer
            }

            auto& forall_patterns = pattern.forall_info->patterns;

            // The correct transformation for `forall(A, B)` is `not(A and not(B))`.
            // We directly transform the `forall` pattern into this structure.

            // 1. Create the "violating" pattern `A and not(B)`.
            //    'A' is the base pattern, which is the first pattern in the forall list.
            ParsedPattern violating_pattern = std::move(forall_patterns[0]);
            logd("  -> Base pattern (A) is of type '{}'.", violating_pattern.fact_type);

            //    'not(B)' is constructed by taking the constraints of all subsequent
            //    patterns (the restrictions), combining them, and negating them.
            auto restrictions_root = std::make_unique<ConstraintNode>(NodeType::AND);
            for (size_t i = 1; i < forall_patterns.size(); ++i) {
                if (forall_patterns[i].constraint_root) {
                    logd("  -> Adding restriction from pattern {}.", i);
                    restrictions_root->children.push_back(std::move(forall_patterns[i].constraint_root));
                }
            }

            logd("  -> Negating combined restriction constraints.");
            negate_constraint_tree(restrictions_root);

            //    Combine `A`'s constraints with `not(B)`'s constraints.
            if (violating_pattern.constraint_root) {
                auto new_root = std::make_unique<ConstraintNode>(NodeType::AND);
                new_root->children.push_back(std::move(violating_pattern.constraint_root));
                new_root->children.push_back(std::move(restrictions_root));
                violating_pattern.constraint_root = std::move(new_root);
                logd("  -> Merged base constraints with negated restrictions.");
            } else {
                violating_pattern.constraint_root = std::move(restrictions_root);
                logd("  -> Set negated restrictions as the new constraint root.");
            }

            // 2. Modify the current pattern (which was the 'forall') to become the final `not(...)` pattern.
            pattern.type = PatternType::NOT;
            pattern.nested_patterns.clear();
            pattern.nested_patterns.push_back(std::move(violating_pattern));
            pattern.forall_info.reset();
            logd("  -> 'forall' successfully transformed into 'not'.");
        }
    }
}
