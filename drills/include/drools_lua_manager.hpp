#ifndef DROOLS_LUA_MANAGER_HPP
#define DROOLS_LUA_MANAGER_HPP

#include "drools_parser_state.hpp"
#include "drools_rete_defs.hpp"
#include "i_network_callback.hpp"

#include <map>
#include <sol/sol.hpp>
#include <stdexcept>
#include <string>
#include <vector>

// Forward declare to break circular dependency
class ReteNetwork;
struct Token;
struct Fact;

// Custom exception for runtime errors during rule execution.
class ReteExecutionException : public std::runtime_error {
public:
    ReteExecutionException(std::string const& message, std::string rule_name) :
        std::runtime_error(message), rule_name_(std::move(rule_name)) {}

    char const* get_rule_name() const noexcept { return rule_name_.c_str(); }

private:
    std::string rule_name_;
};

class LuaScriptingManager {
public:
    explicit LuaScriptingManager(INetworkCallback& callback_provider);

    sol::state& get_lua_state();
    void load_functions(std::vector<ParsedFunction> const& functions);
    bool execute_eval(std::string const& code, Token const& token, std::map<std::string, int> const& bindings);
    void execute_rhs(std::string const& rhs_code, std::string const& rule_name, Token& token,
                     std::map<std::string, int> const& bindings);
    void set_global(std::string const& name, sol::object obj);

private:
    void create_drools_api(sol::environment& env, Token& current_token);

    void bind_globals(sol::environment& env);
    void populate_lua_table_from_fact(sol::table& fact_table, Fact const& fact);
    void bind_variables(sol::environment& env, Token const& token, std::map<std::string, int> const& bindings);

    INetworkCallback& callback_provider_;
    sol::state lua;
};

#endif   // DROOLS_LUA_MANAGER_HPP
