#include "lua_ast_builder.hpp"
#include "pubcxx/logger.hpp"

#include <iostream>
#include <stdexcept>

namespace {
    // Helper to find a direct child of a specific PEGTL rule type.
    template <typename T>
    pegtl::parse_tree::node* find_child(pegtl::parse_tree::node* parent) {
        if (!parent) return nullptr;
        for (auto& child : parent->children) {
            if (child->is_type<T>()) { return child.get(); }
        }
        return nullptr;
    }
}   // namespace

LuaAstRoot LuaAstBuilder::build(pegtl::parse_tree::node& root) {
    LOG_DEBUG("LuaAstBuilder::build starting.");
    LuaAstRoot ast;
    if (root.children.empty()) return ast;

    pegtl::parse_tree::node* statement_list = root.children.front().get();
    for (auto& statement_node : statement_list->children) {
        if (auto stmt = build_statement(statement_node.get())) { ast.statements.push_back(std::move(stmt)); }
    }
    LOG_DEBUG("LuaAstBuilder::build finished with {} statements.", ast.statements.size());
    return ast;
}

std::unique_ptr<LuaStatementNode> LuaAstBuilder::build_statement(pegtl::parse_tree::node* n) {
    if (!n || n->children.empty()) return nullptr;
    auto* content = n->children.front().get();
    auto stmt_node = std::make_unique<LuaStatementNode>();
    stmt_node->pos = content->begin();
    LOG_DEBUG("LuaAstBuilder::build_statement for node type '{}'", content->type);

    if (content->is_type<lua_grammar::local_statement>()) {
        LuaLocalAssignmentNode assign;
        if (auto* names_node = find_child<lua_grammar::name_list_must>(content)) {
            for (auto& name_child : names_node->children) { assign.names.push_back(name_child->string()); }
        }
        if (auto* values_node = find_child<lua_grammar::assignments_one>(content)) {
            if (auto* expr_list = find_child<lua_grammar::expr_list_must>(values_node)) {
                for (auto& expr_child : expr_list->children) {
                    assign.values.push_back(build_expression(expr_child.get()));
                }
            }
        }
        stmt_node->statement = std::move(assign);
    } else if (content->is_type<lua_grammar::function_call>()) {
        auto expr = build_expression(content);
        if (expr) {
            // Check for the special 'drools.update' which we model as a 'modify' statement
            if (auto* fc_ptr_outer = std::get_if<std::unique_ptr<LuaFunctionCallNode>>(&expr->expression)) {
                auto& fc = **fc_ptr_outer;
                if (auto* fa = std::get_if<LuaFieldAccessNode>(&fc.function->expression)) {
                    if (fa->field_name == "update" && std::holds_alternative<LuaVariableNode>(fa->base->expression) &&
                        std::get<LuaVariableNode>(fa->base->expression).name == "drools") {
                        if (fc.arguments.size() == 2) {
                            LuaModifyNode modify;
                            modify.target = std::move(fc.arguments[0]);
                            stmt_node->statement = std::move(modify);
                            return stmt_node;
                        }
                    }
                }
                stmt_node->statement = std::move(*fc_ptr_outer);
            }
        }
    } else if (content->is_type<lua_grammar::assignments>()) {
        // Handle simple assignments like `var = value`
        LuaAssignmentNode assign;
        if (auto* var_list = find_child<lua_grammar::assignment_variable_list>(content)) {
            for (auto& var_child : var_list->children) { assign.targets.push_back(build_expression(var_child.get())); }
        }
        if (auto* values_node = find_child<lua_grammar::assignments_one>(content)) {
            if (auto* expr_list = find_child<lua_grammar::expr_list_must>(values_node)) {
                for (auto& expr_child : expr_list->children) {
                    assign.values.push_back(build_expression(expr_child.get()));
                }
            }
        }
        stmt_node->statement = std::move(assign);
    }

    return stmt_node;
}

std::unique_ptr<LuaExpressionNode> LuaAstBuilder::build_expression(pegtl::parse_tree::node* n) {
    if (!n) return nullptr;
    // The lua_grammar is structured by operator precedence. We just need to handle the binary operators.
    // 'expression' is the lowest precedence (or).
    if (n->is_type<lua_grammar::expression>() || n->is_type<lua_grammar::expr_one>() ||
        n->is_type<lua_grammar::expr_two>() || n->is_type<lua_grammar::expr_three>() ||
        n->is_type<lua_grammar::expr_four>() || n->is_type<lua_grammar::expr_five>() ||
        n->is_type<lua_grammar::expr_six>() || n->is_type<lua_grammar::expr_seven>() ||
        n->is_type<lua_grammar::expr_eight>() || n->is_type<lua_grammar::expr_nine>() ||
        n->is_type<lua_grammar::expr_eleven>()) {
        return build_binary_op_expr(n);
    }
    return build_prefix_expr(n);   // For everything else
}

std::unique_ptr<LuaExpressionNode> LuaAstBuilder::build_binary_op_expr(pegtl::parse_tree::node* n) {
    if (!n || n->children.empty()) return nullptr;
    auto current_expr = build_expression(n->children[0].get());
    for (size_t i = 1; i < n->children.size(); i += 2) {
        auto* op_node = n->children[i].get();
        auto* rhs_node = n->children[i + 1].get();

        auto new_root = std::make_unique<LuaExpressionNode>();
        new_root->pos = op_node->begin();
        LuaBinaryOpNode bin_op;
        bin_op.left = std::move(current_expr);
        bin_op.op = op_node->string();
        bin_op.right = build_expression(rhs_node);
        new_root->expression = std::move(bin_op);
        current_expr = std::move(new_root);
    }
    return current_expr;
}

std::unique_ptr<LuaExpressionNode> LuaAstBuilder::build_prefix_expr(pegtl::parse_tree::node* n) {
    if (!n) return nullptr;

    if (n->is_type<lua_grammar::drl_variable>()) {
        auto node = std::make_unique<LuaExpressionNode>();
        node->pos = n->begin();

        auto* base_var_node = find_child<lua_grammar::drl_variable_base>(n);
        if (!base_var_node) return nullptr;   // Should not happen

        // Start with the base variable (e.g., $p)
        auto base_expr = std::make_unique<LuaExpressionNode>();
        base_expr->pos = base_var_node->begin();
        base_expr->expression = LuaVariableNode{base_var_node->string()};

        std::unique_ptr<LuaExpressionNode> current_expr = std::move(base_expr);

        // Check for subsequent field accesses (.name, .address, etc.)
        for (auto const& child : n->children) {
            if (child->is_type<tao::pegtl::identifier>()) {
                auto fa_node = std::make_unique<LuaExpressionNode>();
                fa_node->pos = child->begin();
                LuaFieldAccessNode fa;
                fa.base = std::move(current_expr);
                fa.field_name = child->string();
                fa_node->expression = std::move(fa);
                current_expr = std::move(fa_node);
            }
        }
        return current_expr;
    }

    if (n->is_type<lua_grammar::name>()) {
        auto node = std::make_unique<LuaExpressionNode>();
        node->pos = n->begin();
        node->expression = LuaVariableNode{n->string()};
        return node;
    }
    if (auto* s = find_child<lua_grammar::literal_string>(n)) {
        auto node = std::make_unique<LuaExpressionNode>();
        node->pos = s->begin();
        auto sv = s->string_view();
        node->expression = LuaLiteralNode{std::string(sv.substr(1, sv.length() - 2))};
        return node;
    }
    if (n->is_type<lua_grammar::expr_thirteen>() || n->is_type<lua_grammar::function_call>()) {   // A chain like a.b()
        if (n->children.empty()) return nullptr;
        auto current_expr = build_prefix_expr(n->children.front().get());
        for (size_t i = 1; i < n->children.size(); ++i) {
            auto* tail = n->children[i].get();
            if (tail->is_type<lua_grammar::variable_tail_two>()) {   // .field
                auto fa_node = std::make_unique<LuaExpressionNode>();
                fa_node->pos = tail->begin();
                LuaFieldAccessNode fa;
                fa.base = std::move(current_expr);
                fa.field_name = tail->children.front()->string();
                fa_node->expression = std::move(fa);
                current_expr = std::move(fa_node);
            } else if (tail->is_type<lua_grammar::function_args>() ||
                       tail->is_type<lua_grammar::function_call_tail>()) {   // (...)
                auto fc_node_wrapper = std::make_unique<LuaExpressionNode>();
                fc_node_wrapper->pos = tail->begin();

                auto fc_on_heap = std::make_unique<LuaFunctionCallNode>();
                fc_on_heap->function = std::move(current_expr);

                if (auto* args_one = find_child<lua_grammar::function_args_one>(tail)) {
                    if (auto* expr_list = find_child<lua_grammar::expr_list_must>(args_one)) {
                        for (auto& expr_child : expr_list->children) {
                            fc_on_heap->arguments.push_back(build_expression(expr_child.get()));
                        }
                    }
                }

                fc_node_wrapper->expression = std::move(fc_on_heap);
                current_expr = std::move(fc_node_wrapper);
            }
        }
        return current_expr;
    }

    // Fallback: If it's a node with children, try the first child.
    if (!n->children.empty()) { return build_expression(n->children[0].get()); }
    return nullptr;
}
