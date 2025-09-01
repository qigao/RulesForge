#ifndef LUA_AST_BUILDER_HPP
#define LUA_AST_BUILDER_HPP

#include "lua_ast.hpp"
#include "lua_grammar.hpp"

#include <tao/pegtl/contrib/parse_tree.hpp>

namespace pegtl = tao::pegtl;

// Selector to build a parse tree with only the nodes we need for semantic analysis.
namespace lua_ast_builder_selectors {
    namespace pegtl = tao::pegtl;

    template <typename Rule>
    struct selector :
        pegtl::parse_tree::selector<
            Rule,
            pegtl::parse_tree::store_content::on<lua_grammar::name, lua_grammar::literal_string, lua_grammar::numeral,
                                                 lua_grammar::key_true, lua_grammar::key_false, lua_grammar::key_nil>,
            pegtl::parse_tree::remove_content::on<
                // We don't need the content of these structural nodes, just their type.
                // The builder will inspect their children instead.
                >> {};
}   // namespace lua_ast_builder_selectors

class LuaAstBuilder {
public:
    LuaAstBuilder() = default;
    LuaAstRoot build(pegtl::parse_tree::node& root);
    std::unique_ptr<LuaExpressionNode> build_expression(pegtl::parse_tree::node* n);

private:
    std::unique_ptr<LuaStatementNode> build_statement(pegtl::parse_tree::node* n);
    std::unique_ptr<LuaExpressionNode> build_binary_op_expr(pegtl::parse_tree::node* n);
    std::unique_ptr<LuaExpressionNode> build_prefix_expr(pegtl::parse_tree::node* n);
};

#endif   // LUA_AST_BUILDER_HPP
