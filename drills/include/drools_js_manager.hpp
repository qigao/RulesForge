#ifndef DROOLS_JS_MANAGER_HPP
#define DROOLS_JS_MANAGER_HPP

#include "drools_parser_state.hpp"
#include "drools_rete_defs.hpp"
#include "i_network_callback.hpp"
#include "js_handle_manager.hpp"

#include <map>
#include <quickjs.h>
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

class JSScriptingManager {
public:
    explicit JSScriptingManager(INetworkCallback& callback_provider);
    ~JSScriptingManager();

    // Disable copy/move operations for safety
    JSScriptingManager(JSScriptingManager const&) = delete;
    JSScriptingManager& operator=(JSScriptingManager const&) = delete;
    JSScriptingManager(JSScriptingManager&&) = delete;
    JSScriptingManager& operator=(JSScriptingManager&&) = delete;

    JSContext* get_js_context();
    void load_functions(std::vector<ParsedFunction> const& functions);
    bool execute_eval(std::string const& code, Token const& token, std::map<std::string, int> const& bindings);
    void execute_rhs(std::string const& rhs_code, std::string const& rule_name, Token& token,
                     std::map<std::string, int> const& bindings);
    void set_global(std::string const& name, JSValue obj);

private:
    void create_drools_api(Token& current_token);
    void bind_globals();
    JSValue populate_js_object_from_fact(Fact const& fact);
    void bind_variables(Token const& token, std::map<std::string, int> const& bindings);

    // Helper methods for JS object manipulation
    JSValue create_js_object();
    void set_js_property(JSValue obj, const char* prop, JSValue val);
    JSValue get_js_property(JSValue obj, const char* prop);
    JSValue js_string(std::string const& str);
    JSValue js_number(double num);
    JSValue js_int(int64_t num);
    JSValue js_bool(bool val);
    std::string get_js_string(JSValue val);
    double get_js_number(JSValue val);
    int64_t get_js_int(JSValue val);
    bool get_js_bool(JSValue val);

    // Helper for fact creation from JS object
    std::shared_ptr<Fact> fact_from_js_object(JSValue fact_obj);

    INetworkCallback& callback_provider_;
    JSRuntime* runtime_;
    JSContext* context_;
    JSHandleManager::HandleId handle_id_ = 0;  // Handle for this manager instance
};

#endif   // DROOLS_JS_MANAGER_HPP