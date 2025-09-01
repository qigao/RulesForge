#include "drools_lua_manager.hpp"
#include "pubcxx/logger.hpp"

#include "drools_rete_defs.hpp"
#include "i_network_callback.hpp"
#include "lua_code_generator.hpp"

#include <algorithm>
#include <iostream>
#include <magic_enum/magic_enum.hpp>
#include <vector>
struct LuaAstRoot;

// Helper to create a new Fact from a Lua table description.
static std::shared_ptr<Fact> fact_from_lua_table(sol::table& fact_table) {
    if (!fact_table["type"].is<std::string>()) { return nullptr; }
    auto new_fact = std::make_shared<Fact>();
    new_fact->type = fact_table["type"].get<std::string>();
    for (auto const& kvp : fact_table) {
        if (!kvp.first.is<std::string>()) continue;
        std::string key = kvp.first.as<std::string>();
        if (key == "type") continue;
        sol::object val = kvp.second;
        if (val.is<std::string>()) {
            new_fact->fields[key] = val.as<std::string>();
        } else if (val.is<double>()) {   // Lua numbers are doubles by default.
            new_fact->fields[key] = val.as<double>();
        } else if (val.is<int64_t>()) {
            new_fact->fields[key] = val.as<int64_t>();
        } else if (val.is<bool>()) {
            new_fact->fields[key] = static_cast<int64_t>(val.as<bool>());
        } else if (val.is<sol::nil_t>()) {
            new_fact->fields[key] = NilValue{};
        }
    }
    return new_fact;
}

LuaScriptingManager::LuaScriptingManager(INetworkCallback& callback_provider) : callback_provider_(callback_provider) {
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
}

sol::state& LuaScriptingManager::get_lua_state() { return lua; }

void LuaScriptingManager::load_functions(std::vector<ParsedFunction> const& functions) {
    for (auto const& func : functions) {
        try {
            std::string script = "function " + func.name + "(" + func.parameter_list + ")\n" + func.body + "\nend";
            LOG_DEBUG("Loading Lua function '{}'", func.name);
            lua.script(script);
        } catch (sol::error const& e) {
            LOG_ERROR("Lua function load error for '{}': {}", func.name, e.what());
            std::cerr << "Lua function load error: " << e.what() << std::endl;
        }
    }
}

// This function now uses a metatable to allow direct field access (e.g., `p.name` instead of `p.fields.name`)
void LuaScriptingManager::populate_lua_table_from_fact(sol::table& fact_table, Fact const& fact) {
    fact_table["id"] = fact.id;
    sol::state_view lua_view = fact_table.lua_state();
    sol::table fields_table = lua_view.create_table();

    for (auto const& [key, val] : fact.fields) {
        std::visit(
            [this, &lua_view, &fields_table, &key](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, FactList>) {
                    sol::table list_table = lua_view.create_table();
                    for (size_t i = 0; i < arg.facts.size(); ++i) {
                        sol::table inner_fact_table = lua_view.create_table();
                        populate_lua_table_from_fact(inner_fact_table, *arg.facts[i]);
                        list_table[i + 1] = inner_fact_table;
                    }
                    fields_table[key] = list_table;
                } else if constexpr (std::is_same_v<T, NilValue>) {
                    fields_table[key] = sol::nil;
                } else {
                    fields_table[key] = arg;
                }
            },
            val);
    }

    // Set up a metatable to redirect field lookups (e.g., d.value) to the fields sub-table.
    sol::table metatable = lua_view.create_table();
    metatable[sol::meta_method::index] = fields_table;
    fact_table[sol::metatable_key] = metatable;
}

void LuaScriptingManager::bind_variables(sol::environment& env, Token const& token,
                                         std::map<std::string, int> const& bindings) {
    for (auto const& [binding, depth] : bindings) {
        if (binding.empty() || binding[0] != '$') continue;
        std::string lua_var_name = binding.substr(1);
        auto fact_in_token = token.get_fact_at_depth(depth);
        if (fact_in_token) {
            auto current_fact_opt = callback_provider_.get_fact_by_id(fact_in_token->id);
            if (current_fact_opt) {
                sol::state_view lua_view = env.lua_state();
                sol::table fact_table = lua_view.create_table();
                populate_lua_table_from_fact(fact_table, **current_fact_opt);
                env[lua_var_name] = fact_table;
                LOG_DEBUG("Lua: Bound variable '{}' to fact ID {}", lua_var_name, (*current_fact_opt)->id);
            }
        }
    }
}

void LuaScriptingManager::create_drools_api(sol::environment& env, Token& current_token) {
    sol::table drools_api = env.create("drools");
    drools_api["update"] = [this](sol::table fact_table, sol::table new_fields) {
        if (fact_table["id"] == sol::nil || !fact_table["id"].is<int64_t>()) return;
        int64_t id = fact_table["id"].get<int64_t>();
        auto fact_it = callback_provider_.get_fact_by_id(id);
        if (fact_it) {
            callback_provider_.update_fact(*fact_it, [&](Fact& f) {
                for (auto const& kvp : new_fields) {
                    auto key = kvp.first.as<std::string>();
                    sol::object val = kvp.second;
                    if (val.is<std::string>())
                        f.fields[key] = val.as<std::string>();
                    else if (val.is<double>())
                        f.fields[key] = val.as<double>();
                    else if (val.is<int64_t>())
                        f.fields[key] = val.as<int64_t>();
                    else if (val.is<bool>())
                        f.fields[key] = static_cast<int64_t>(val.as<bool>());
                    else if (val.is<sol::nil_t>())
                        f.fields[key] = NilValue{};
                }
            });
        }
    };
    drools_api["insert"] = [this](sol::table fact_table) {
        auto new_fact = fact_from_lua_table(fact_table);
        if (new_fact) { callback_provider_.add_fact(new_fact); }
    };
    drools_api["insertLogical"] = [this, &current_token](sol::table fact_table) {
        auto new_fact = fact_from_lua_table(fact_table);
        if (new_fact) { callback_provider_.logical_insert(current_token, new_fact); }
    };
    drools_api["retract"] = [this](sol::table fact_table) {
        if (fact_table["id"] == sol::nil || !fact_table["id"].is<int64_t>()) return;
        int64_t id = fact_table["id"].get<int64_t>();
        auto fact_it = callback_provider_.get_fact_by_id(id);
        if (fact_it) { callback_provider_.retract_fact(*fact_it); }
    };
    drools_api["setFocus"] = [this](std::string const& group_name) { callback_provider_.set_focus(group_name); };
}

bool LuaScriptingManager::execute_eval(std::string const& code, Token const& token,
                                       std::map<std::string, int> const& bindings) {
    if (code.empty()) return true;
    LOG_DEBUG("Executing Lua eval: '{}'", code);
    try {
        sol::environment env(lua, sol::create, lua.globals());
        bind_variables(env, token, bindings);
        auto result = lua.script(code, env);
        if (!result.valid()) {
            sol::error err = result;
            throw err;
        }
        bool bool_result = result.get<bool>();
        LOG_DEBUG("Lua eval result: {}", bool_result);
        return bool_result;
    } catch (sol::error const& e) {
        LOG_ERROR("\n--- LUA EVAL ERROR ---\n"
                  "Expression: '{}'\n"
                  "Error: {}\n----------------------",
                  code, e.what());
        std::cerr << "\n--- LUA EVAL ERROR ---\n"
                  << "Expression: '" << code << "'\n"
                  << "Error: " << e.what() << "\n----------------------\n";
        return false;
    }
}

void LuaScriptingManager::execute_rhs(std::string const& rhs_code, std::string const& rule_name, Token& token,
                                      std::map<std::string, int> const& bindings) {
    LOG_DEBUG("Executing RHS for rule '{}'", rule_name);
    LOG_DEBUG("RHS code:\n{}", rhs_code);
    try {
        sol::environment env(lua, sol::create, lua.globals());
        bind_variables(env, token, bindings);
        create_drools_api(env, token);
        lua.script(rhs_code, env);

    } catch (sol::error const& e) {
        std::string error_msg = "Lua execution error: " + std::string(e.what());
        LOG_ERROR("Error executing RHS for rule '{}': {}", rule_name, error_msg);
        throw ReteExecutionException(error_msg, rule_name);
    }
}

void LuaScriptingManager::bind_globals(sol::environment& env) {
    for (auto const& [name, obj] : callback_provider_.get_global_values()) {
        if (!name.empty() && name[0] == '$') {
            env[name.substr(1)] = obj;
        } else {
            env[name] = obj;
        }
    }
}

void LuaScriptingManager::set_global(std::string const& name, sol::object obj) { lua[name] = obj; }
