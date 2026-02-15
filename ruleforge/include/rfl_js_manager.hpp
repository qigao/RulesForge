#ifndef RFL_JS_MANAGER_HPP
#define RFL_JS_MANAGER_HPP

#include "rfl_parser_state.hpp"
#include "rfl_rete_defs.hpp"
#include "i_network_callback.hpp"
#include "js_handle_manager.hpp"
#include "token_handle_manager.hpp"
#include "knowledge_base.hpp"
#include <chrono>
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

// PROD-001: Exception for JavaScript execution timeout
class JSExecutionTimeoutException : public std::runtime_error {
public:
    JSExecutionTimeoutException(std::string const& rule_name, std::chrono::milliseconds timeout) :
        std::runtime_error("JavaScript execution timeout after " + std::to_string(timeout.count()) +
                           "ms in rule '" + rule_name + "'"),
        rule_name_(rule_name), timeout_(timeout) {}

    char const* get_rule_name() const noexcept { return rule_name_.c_str(); }
    std::chrono::milliseconds get_timeout() const noexcept { return timeout_; }

private:
    std::string rule_name_;
    std::chrono::milliseconds timeout_;
};

class JSScriptingManager {
public:
    // Default timeout: 5 seconds (0 = disabled)
    static constexpr std::chrono::milliseconds DEFAULT_TIMEOUT{5000};

    explicit JSScriptingManager(INetworkCallback& callback_provider,
                                std::chrono::milliseconds execution_timeout = DEFAULT_TIMEOUT);
    ~JSScriptingManager();

    // Disable copy/move operations for safety
    JSScriptingManager(JSScriptingManager const&) = delete;
    JSScriptingManager& operator=(JSScriptingManager const&) = delete;
    JSScriptingManager(JSScriptingManager&&) = delete;
    JSScriptingManager& operator=(JSScriptingManager&&) = delete;

    // Native function data — public for friend function access
    struct NativeFuncData {
        NativeFunctionCallback callback;
        void* user_data;
        JSScriptingManager* manager;
        int func_id;
    };

    JSContext* get_js_context();
    void load_functions(std::vector<ParsedFunction> const& functions);
    void register_native_functions(std::map<std::string, NativeFunction> const& functions);
    bool execute_eval(std::string const& code, Token const& token, ruleforge::map<std::string, int> const& bindings);
    void execute_rhs(std::string const& rhs_code, std::string const& rule_name, Token& token,
                     ruleforge::map<std::string, int> const& bindings);
    void set_global(std::string const& name, JSValue obj);
    std::string const& get_current_rule_name() const { return current_rule_name_; }

    // PROD-001: Timeout configuration
    void set_execution_timeout(std::chrono::milliseconds timeout) { execution_timeout_ = timeout; }
    std::chrono::milliseconds get_execution_timeout() const { return execution_timeout_; }

private:
    void create_rfl_api(Token& current_token);
    void bind_globals();
    void bind_jmespath_functions();
    JSValue populate_js_object_from_fact(Fact const& fact);
    void bind_variables(Token const& token, ruleforge::map<std::string, int> const& bindings);

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

    // JSON conversion utilities
    std::string fact_to_json(Fact const& fact);

    // Helper for fact creation from JS object
    std::shared_ptr<Fact> fact_from_js_object(JSValue fact_obj);

    // PROD-001: Interrupt handler for timeout
    static int interrupt_handler(JSRuntime* rt, void* opaque);

    INetworkCallback& callback_provider_;
    JSRuntime* runtime_;
    JSContext* context_;
    JSHandleManager::HandleId handle_id_ = 0;  // Handle for this manager instance
    TokenHandleManager::HandleId current_token_handle_ = TokenHandleManager::INVALID_HANDLE;  // P0-001: Safe token handle
    std::string current_rule_name_;

    // PROD-001: Timeout tracking
    std::chrono::milliseconds execution_timeout_;
    std::chrono::steady_clock::time_point rhs_start_time_;
    bool timeout_occurred_ = false;

    std::map<std::string, std::unique_ptr<NativeFuncData>> native_func_data_;
    std::map<int, NativeFuncData*> func_by_id_;
    int next_func_id_ = 1;

    // Friend function for native wrapper
    friend JSValue native_function_wrapper(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic);
};

#endif   // RFL_JS_MANAGER_HPP

