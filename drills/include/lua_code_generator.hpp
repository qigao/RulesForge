#ifndef LUA_CODE_GENERATOR_HPP
#define LUA_CODE_GENERATOR_HPP

#include "lua_ast.hpp"

#include <string>

// Generates a Lua code string from a given Lua AST.
std::string generate_lua_code(LuaAstRoot const& ast);
std::string generate_lua_expr(LuaExpressionNode const& node);

#endif   // LUA_CODE_GENERATOR_HPP
