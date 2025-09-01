#include "lua_ast.hpp"

#include <iostream>

// ===============================================
// Expression Copying
// ===============================================
// Visitor to correctly deep-copy the LuaExpression variant
struct ExprCopyVisitor {
    // Trivial types
    LuaExpression operator()(LuaVariableNode const& node) const { return node; }

    LuaExpression operator()(LuaLiteralNode const& node) const { return node; }

    // Pointer type in variant - create a new pointer via deep copy
    LuaExpression operator()(std::unique_ptr<LuaFunctionCallNode> const& node) const {
        if (!node) return std::unique_ptr<LuaFunctionCallNode>(nullptr);
        return std::make_unique<LuaFunctionCallNode>(*node);   // Uses LuaFunctionCallNode's copy ctor
    }

    // Recursive types
    LuaExpression operator()(LuaFieldAccessNode const& node) const {
        LuaFieldAccessNode copy;
        copy.base = deep_copy_lua_expr(node.base.get());
        copy.field_name = node.field_name;
        return copy;
    }

    LuaExpression operator()(LuaBinaryOpNode const& node) const {
        LuaBinaryOpNode copy;
        copy.left = deep_copy_lua_expr(node.left.get());
        copy.op = node.op;
        copy.right = deep_copy_lua_expr(node.right.get());
        return copy;
    }
};

std::unique_ptr<LuaExpressionNode> deep_copy_lua_expr(LuaExpressionNode const* node) {
    if (!node) return nullptr;
    auto new_node = std::make_unique<LuaExpressionNode>();
    new_node->expression = std::visit(ExprCopyVisitor{}, node->expression);
    new_node->pos = node->pos;
    return new_node;
}

// ===============================================
// Statement Copying
// ===============================================
// Visitor to correctly deep-copy the LuaStatement variant
struct StmtCopyVisitor {
    LuaStatement operator()(std::unique_ptr<LuaFunctionCallNode> const& node) const {
        if (!node) return std::unique_ptr<LuaFunctionCallNode>(nullptr);
        return std::make_unique<LuaFunctionCallNode>(*node);
    }

    LuaStatement operator()(LuaLocalAssignmentNode const& node) const {
        LuaLocalAssignmentNode copy;
        copy.names = node.names;
        for (auto const& val : node.values) { copy.values.push_back(deep_copy_lua_expr(val.get())); }
        return copy;
    }

    LuaStatement operator()(LuaAssignmentNode const& node) const {
        LuaAssignmentNode copy;
        for (auto const& target : node.targets) { copy.targets.push_back(deep_copy_lua_expr(target.get())); }
        for (auto const& val : node.values) { copy.values.push_back(deep_copy_lua_expr(val.get())); }
        return copy;
    }

    LuaStatement operator()(LuaModifyNode const& node) const {
        LuaModifyNode copy;
        copy.target = deep_copy_lua_expr(node.target.get());
        for (auto const& [key, value] : node.field_assignments) {
            copy.field_assignments[key] = deep_copy_lua_expr(value.get());
        }
        return copy;
    }
};

std::unique_ptr<LuaStatementNode> deep_copy_lua_stmt(LuaStatementNode const* node) {
    if (!node) return nullptr;
    auto new_node = std::make_unique<LuaStatementNode>();
    new_node->statement = std::visit(StmtCopyVisitor{}, node->statement);
    new_node->pos = node->pos;
    return new_node;
}

// ===============================================
// Copy Constructors (no changes here)
// ===============================================
LuaFunctionCallNode::LuaFunctionCallNode(LuaFunctionCallNode const& other) :
    function(deep_copy_lua_expr(other.function.get())) {
    for (auto const& arg : other.arguments) { arguments.push_back(deep_copy_lua_expr(arg.get())); }
}

LuaFunctionCallNode& LuaFunctionCallNode::operator=(LuaFunctionCallNode const& other) {
    if (this == &other) return *this;
    function = deep_copy_lua_expr(other.function.get());
    arguments.clear();
    for (auto const& arg : other.arguments) { arguments.push_back(deep_copy_lua_expr(arg.get())); }
    return *this;
}

LuaAstRoot::LuaAstRoot(LuaAstRoot const& other) {
    for (auto const& stmt : other.statements) { statements.push_back(deep_copy_lua_stmt(stmt.get())); }
}

LuaAstRoot& LuaAstRoot::operator=(LuaAstRoot const& other) {
    if (this == &other) return *this;
    statements.clear();
    for (auto const& stmt : other.statements) { statements.push_back(deep_copy_lua_stmt(stmt.get())); }
    return *this;
}
