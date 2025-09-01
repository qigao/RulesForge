#include "lua_code_generator.hpp"

#include <iostream>
#include <sstream>
#include <variant>

// Forward declare for mutual recursion
std::string generate_lua_expr(LuaExpressionNode const& node);
std::string generate_lua_stmt(LuaStatementNode const& node);

struct ExprCodeGenVisitor {
    std::string operator()(LuaVariableNode const& node) const { return node.name; }

    std::string operator()(LuaFieldAccessNode const& node) const {
        return generate_lua_expr(*node.base) + "." + node.field_name;
    }

    std::string operator()(LuaBinaryOpNode const& node) const {
        return "(" + generate_lua_expr(*node.left) + " " + node.op + " " + generate_lua_expr(*node.right) + ")";
    }

    std::string operator()(std::unique_ptr<LuaFunctionCallNode> const& node) const {
        if (!node) return "nil";
        std::stringstream ss;
        ss << generate_lua_expr(*node->function) << "(";
        for (size_t i = 0; i < node->arguments.size(); ++i) {
            ss << generate_lua_expr(*node->arguments[i]);
            if (i < node->arguments.size() - 1) ss << ", ";
        }
        ss << ")";
        return ss.str();
    }

    std::string operator()(LuaLiteralNode const& node) const {
        return std::visit(
            [](auto&& arg) -> std::string {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::string>) return "\"" + arg + "\"";
                if constexpr (std::is_same_v<T, int64_t>) return std::to_string(arg);
                if constexpr (std::is_same_v<T, double>) return std::to_string(arg);
                if constexpr (std::is_same_v<T, bool>) return arg ? "true" : "false";
                if constexpr (std::is_same_v<T, LuaNilValue>) return "nil";
                return "";
            },
            node);
    }
};

// Visitor for generating code from Statement variants
struct StmtCodeGenVisitor {
    std::string operator()(LuaLocalAssignmentNode const& node) const {
        std::stringstream ss;
        ss << "local ";
        for (size_t i = 0; i < node.names.size(); ++i) {
            ss << node.names[i] << (i < node.names.size() - 1 ? ", " : "");
        }
        if (!node.values.empty()) {
            ss << " = ";
            for (size_t i = 0; i < node.values.size(); ++i) {
                ss << generate_lua_expr(*node.values[i]) << (i < node.values.size() - 1 ? ", " : "");
            }
        }
        return ss.str();
    }

    std::string operator()(LuaAssignmentNode const& node) const {
        std::stringstream ss;
        for (size_t i = 0; i < node.targets.size(); ++i) {
            ss << generate_lua_expr(*node.targets[i]) << (i < node.targets.size() - 1 ? ", " : "");
        }
        ss << " = ";
        for (size_t i = 0; i < node.values.size(); ++i) {
            ss << generate_lua_expr(*node.values[i]) << (i < node.values.size() - 1 ? ", " : "");
        }
        return ss.str();
    }

    std::string operator()(std::unique_ptr<LuaFunctionCallNode> const& node) const {
        return ExprCodeGenVisitor{}(node);
    }

    std::string operator()(LuaModifyNode const& node) const {
        std::stringstream ss;
        ss << "drools.update(" << generate_lua_expr(*node.target) << ", {";
        bool first = true;
        for (auto const& [key, value] : node.field_assignments) {
            if (!first) { ss << ", "; }
            ss << key << " = " << generate_lua_expr(*value);
            first = false;
        }
        ss << "})";
        return ss.str();
    }
};

std::string generate_lua_expr(LuaExpressionNode const& node) {
    return std::visit(ExprCodeGenVisitor{}, node.expression);
}

std::string generate_lua_stmt(LuaStatementNode const& node) { return std::visit(StmtCodeGenVisitor{}, node.statement); }

std::string generate_lua_code(LuaAstRoot const& ast) {
    std::stringstream ss;
    for (auto const& stmt : ast.statements) { ss << generate_lua_stmt(*stmt) << "\n"; }
    return ss.str();
}
