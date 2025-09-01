#ifndef LUA_AST_HPP
#define LUA_AST_HPP

#include <map>
#include <memory>
#include <string>
#include <tao/pegtl/position.hpp>
#include <variant>
#include <vector>
// Forward declarations for deep copy and AST nodes
struct LuaExpressionNode;
struct LuaStatementNode;
struct LuaModifyNode;
struct LuaFunctionCallNode;   // Keep forward declaration
std::unique_ptr<LuaExpressionNode> deep_copy_lua_expr(LuaExpressionNode const* node);
std::unique_ptr<LuaStatementNode> deep_copy_lua_stmt(LuaStatementNode const* node);

// --- Expression Nodes ---
struct LuaVariableNode {
    std::string name;
};

struct LuaFieldAccessNode {
    std::unique_ptr<LuaExpressionNode> base;
    std::string field_name;
};

// LuaFunctionCallNode is now defined later
struct LuaNilValue {};

using LuaLiteralNode = std::variant<std::string, int64_t, double, bool, LuaNilValue>;

struct LuaBinaryOpNode {
    std::unique_ptr<LuaExpressionNode> left;
    std::string op;
    std::unique_ptr<LuaExpressionNode> right;
};

using LuaExpression = std::variant<LuaVariableNode, LuaFieldAccessNode, std::unique_ptr<LuaFunctionCallNode>,
                                   LuaLiteralNode, LuaBinaryOpNode>;

struct LuaExpressionNode {
    LuaExpression expression;
    tao::pegtl::position pos;

    LuaExpressionNode() : pos(0, 0, 0, "") {}
};

// --- Statement Nodes ---
struct LuaLocalAssignmentNode {
    std::vector<std::string> names;
    std::vector<std::unique_ptr<LuaExpressionNode>> values;
};

struct LuaAssignmentNode {
    std::vector<std::unique_ptr<LuaExpressionNode>> targets;
    std::vector<std::unique_ptr<LuaExpressionNode>> values;
};

struct LuaFunctionCallNode {   // Full Definition
    std::unique_ptr<LuaExpressionNode> function;
    std::vector<std::unique_ptr<LuaExpressionNode>> arguments;
    // Custom copy logic is needed
    LuaFunctionCallNode() = default;
    LuaFunctionCallNode(LuaFunctionCallNode&&) = default;
    LuaFunctionCallNode& operator=(LuaFunctionCallNode&&) = default;
    LuaFunctionCallNode(LuaFunctionCallNode const& other);
    LuaFunctionCallNode& operator=(LuaFunctionCallNode const& other);
};

struct LuaModifyNode {
    std::unique_ptr<LuaExpressionNode> target;
    std::map<std::string, std::unique_ptr<LuaExpressionNode>> field_assignments;
};

using LuaStatement =
    std::variant<LuaLocalAssignmentNode, LuaAssignmentNode, std::unique_ptr<LuaFunctionCallNode>, LuaModifyNode>;

struct LuaStatementNode {
    LuaStatement statement;
    tao::pegtl::position pos;

    LuaStatementNode() : pos(0, 0, 0, "") {}
};

// --- AST Root ---
struct LuaAstRoot {
    std::vector<std::unique_ptr<LuaStatementNode>> statements;
    // Custom copy logic is needed
    LuaAstRoot() = default;
    LuaAstRoot(LuaAstRoot&&) = default;
    LuaAstRoot& operator=(LuaAstRoot&&) = default;
    LuaAstRoot(LuaAstRoot const& other);
    LuaAstRoot& operator=(LuaAstRoot const& other);
};
#endif   // LUA_AST_HPP
