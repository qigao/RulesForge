#include "drools_js_manager.hpp"
#include "pubcxx/logger.hpp"

#include "drools_rete_defs.hpp"
#include "i_network_callback.hpp"
#include "js_handle_manager.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <limits>
#include <magic_enum/magic_enum.hpp>
#include <vector>
#include <glaze/glaze.hpp>
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jmespath/jmespath.hpp>

struct ParsedFunction;

// Helper to create a new Fact from a JavaScript object.
std::shared_ptr<Fact> JSScriptingManager::fact_from_js_object(JSValue fact_obj) {
    if (!JS_IsObject(fact_obj)) {
        return nullptr;
    }
    
    // Get the type property
    JSValue type_val = JS_GetPropertyStr(context_, fact_obj, "type");
    if (!JS_IsString(type_val)) {
        JS_FreeValue(context_, type_val);
        return nullptr;
    }
    
    auto new_fact = std::make_shared<Fact>();
    new_fact->type = get_js_string(type_val);
    JS_FreeValue(context_, type_val);
    
    // Iterate through all properties
    JSPropertyEnum* props;
    uint32_t prop_count;
    if (JS_GetOwnPropertyNames(context_, &props, &prop_count, fact_obj, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
        return new_fact;
    }
    
    for (uint32_t i = 0; i < prop_count; i++) {
        JSValue key_val = JS_AtomToValue(context_, props[i].atom);
        std::string key = get_js_string(key_val);
        JS_FreeValue(context_, key_val);
        
        if (key == "type") continue;
        
        JSValue val = JS_GetProperty(context_, fact_obj, props[i].atom);
        if (JS_IsString(val)) {
            new_fact->fields[key] = get_js_string(val);
        } else if (JS_IsNumber(val)) {
            double num = get_js_number(val);
            // Check if it's an integer
            if (num == std::floor(num)) {
                new_fact->fields[key] = static_cast<int64_t>(num);
            } else {
                new_fact->fields[key] = num;
            }
        } else if (JS_IsBool(val)) {
            new_fact->fields[key] = static_cast<int64_t>(get_js_bool(val));
        } else if (JS_IsNull(val) || JS_IsUndefined(val)) {
            new_fact->fields[key] = NilValue{};
        }
        JS_FreeValue(context_, val);
    }
    
    js_free(context_, props);
    return new_fact;
}

JSScriptingManager::JSScriptingManager(INetworkCallback& callback_provider) : callback_provider_(callback_provider) {
    runtime_ = JS_NewRuntime();
    if (!runtime_) {
        throw std::runtime_error("Failed to create QuickJS runtime");
    }
    
    context_ = JS_NewContext(runtime_);
    if (!context_) {
        JS_FreeRuntime(runtime_);
        throw std::runtime_error("Failed to create QuickJS context");
    }
    
    // Register this manager and get a handle
    handle_id_ = JSHandleManager::instance().register_manager(this);
    
    // Set up console.log functionality properly
    JSValue global = JS_GetGlobalObject(context_);
    JSValue console = JS_NewObject(context_);
    
    // Create console.log function
    JSValue log_func = JS_NewCFunction(context_, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
        std::string output;
        for (int i = 0; i < argc; i++) {
            if (i > 0) output += " ";
            const char* str = JS_ToCString(ctx, argv[i]);
            if (str) {
                output += str;
                JS_FreeCString(ctx, str);
            } else {
                output += "[undefined]";
            }
        }
        std::cout << output << std::endl;
        return JS_UNDEFINED;
    }, "log", 1);
    
    JS_SetPropertyStr(context_, console, "log", log_func);
    JS_SetPropertyStr(context_, global, "console", console);
    
    // Now bind jmespath functions
    bind_jmespath_functions();
    
    JS_FreeValue(context_, global);
}

JSScriptingManager::~JSScriptingManager() {
    // Unregister the handle
    if (handle_id_ != 0) {
        JSHandleManager::instance().unregister(handle_id_);
    }
    
    if (context_) {
        JS_FreeContext(context_);
    }
    if (runtime_) {
        JS_FreeRuntime(runtime_);
    }
}

JSContext* JSScriptingManager::get_js_context() { 
    return context_; 
}

void JSScriptingManager::load_functions(std::vector<ParsedFunction> const& functions) {
    for (auto const& func : functions) {
        try {
            std::string script = "function " + func.name + "(" + func.parameter_list + ") {\n" + func.body + "\n}";
            LOG_DEBUG("Loading JavaScript function '{}'", func.name);
            
            JSValue result = JS_Eval(context_, script.c_str(), script.length(), "<function>", JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(result)) {
                JSValue exception = JS_GetException(context_);
                std::string error_msg = get_js_string(exception);
                JS_FreeValue(context_, exception);
                LOG_ERROR("JavaScript function load error for '{}': {}", func.name, error_msg);
                std::cerr << "JavaScript function load error: " << error_msg << std::endl;
            }
            JS_FreeValue(context_, result);
        } catch (std::exception const& e) {
            LOG_ERROR("JavaScript function load error for '{}': {}", func.name, e.what());
            std::cerr << "JavaScript function load error: " << e.what() << std::endl;
        }
    }
}

JSValue JSScriptingManager::populate_js_object_from_fact(Fact const& fact) {
    JSValue fact_obj = JS_NewObject(context_);
    
    // Set all fields first
    for (auto const& [key, val] : fact.fields) {
        std::visit(
            [this, &fact_obj, &key](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, FactList>) {
                    JSValue array = JS_NewArray(context_);
                    for (size_t i = 0; i < arg.facts.size(); ++i) {
                        JSValue inner_fact_obj = populate_js_object_from_fact(*arg.facts[i]);
                        JS_SetPropertyUint32(context_, array, static_cast<uint32_t>(i), inner_fact_obj);
                    }
                    JS_SetPropertyStr(context_, fact_obj, key.c_str(), array);
                } else if constexpr (std::is_same_v<T, NilValue>) {
                    JS_SetPropertyStr(context_, fact_obj, key.c_str(), JS_NULL);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    JS_SetPropertyStr(context_, fact_obj, key.c_str(), JS_NewString(context_, arg.c_str()));
                } else if constexpr (std::is_same_v<T, double>) {
                    JS_SetPropertyStr(context_, fact_obj, key.c_str(), JS_NewFloat64(context_, arg));
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    JS_SetPropertyStr(context_, fact_obj, key.c_str(), JS_NewInt64(context_, arg));
                }
            },
            val);
    }
    
    // Set the internal fact ID AFTER fields, so it always takes precedence
    // This ensures that fact.id is never overridden by user fields
    JS_SetPropertyStr(context_, fact_obj, "id", JS_NewInt64(context_, fact.id));
    
    return fact_obj;
}

void JSScriptingManager::bind_variables(Token const& token, map<std::string, int> const& bindings) {
    LOG_WARN("bind_variables: Processing {} bindings for token with WME depth {}", bindings.size(), 
              token.wme ? token.wme->depth : -1);
    
    // Check if jmespath function exists before binding variables
    JSValue test_global_before = JS_GetGlobalObject(context_);
    JSValue test_func_before = JS_GetPropertyStr(context_, test_global_before, "jmespath");
    bool is_function_before = JS_IsFunction(context_, test_func_before);
    LOG_WARN("jmespath function status BEFORE variable binding: {}", is_function_before ? "FUNCTION" : "NOT FUNCTION");
    JS_FreeValue(context_, test_func_before);
    JS_FreeValue(context_, test_global_before);
    
    for (auto const& [binding, depth] : bindings) {
        LOG_WARN("  -> Binding '{}' at depth {}", binding, depth);
        if (binding.empty() || binding[0] != '$') continue;
        std::string js_var_name = binding.substr(1);
        auto fact_in_token = token.get_fact_at_depth(depth);
        if (fact_in_token) {
            auto current_fact_opt = callback_provider_.get_fact_by_id(fact_in_token->id);
            if (current_fact_opt) {
                // Bind the regular fact object
                JSValue fact_obj = populate_js_object_from_fact(**current_fact_opt);
                JSValue global = JS_GetGlobalObject(context_);
                JS_SetPropertyStr(context_, global, js_var_name.c_str(), fact_obj);
                
                // Also bind JSON representation for jmespath queries
                // Variable name pattern: $fact -> fact_json
                std::string json_var_name = js_var_name + "_json";
                std::string fact_json = fact_to_json(**current_fact_opt);
                JSValue json_str = JS_NewString(context_, fact_json.c_str());
                JS_SetPropertyStr(context_, global, json_var_name.c_str(), json_str);
                
                // Special case: if variable is $JSON, bind it directly for convenience
                if (js_var_name == "JSON") {
                    JS_SetPropertyStr(context_, global, "JSON_data", json_str);
                }
                
                JS_FreeValue(context_, global);
                LOG_WARN("JavaScript: Bound variable '{}' to fact ID {} (JSON: {})", 
                        js_var_name, (*current_fact_opt)->id, json_var_name);
            } else {
                LOG_WARN("JavaScript: Fact at depth {} found in token but missing from working memory", depth);
            }
        } else {
            LOG_WARN("JavaScript: No fact found at depth {} for binding '{}'", depth, binding);
        }
    }
    
    // Check if jmespath function exists after binding variables
    JSValue test_global_after = JS_GetGlobalObject(context_);
    JSValue test_func_after = JS_GetPropertyStr(context_, test_global_after, "jmespath");
    bool is_function_after = JS_IsFunction(context_, test_func_after);
    LOG_WARN("jmespath function status AFTER variable binding: {}", is_function_after ? "FUNCTION" : "NOT FUNCTION");
    JS_FreeValue(context_, test_func_after);
    JS_FreeValue(context_, test_global_after);
}

void JSScriptingManager::create_drools_api(Token& current_token) {
    // Check jmespath function status at the start of create_drools_api
    JSValue test_global_start = JS_GetGlobalObject(context_);
    JSValue test_func_start = JS_GetPropertyStr(context_, test_global_start, "jmespath");
    bool is_function_start = JS_IsFunction(context_, test_func_start);
    LOG_WARN("jmespath function status at START of create_drools_api: {}", is_function_start ? "FUNCTION" : "NOT FUNCTION");
    JS_FreeValue(context_, test_func_start);
    JS_FreeValue(context_, test_global_start);
    
    JSValue global = JS_GetGlobalObject(context_);
    
    // Store the handle ID instead of raw pointer
    JS_SetPropertyStr(context_, global, "__drools_handle_id", JS_NewInt32(context_, static_cast<int32_t>(handle_id_)));
    
    // Check jmespath function status after setting handle ID
    JSValue test_global_handle = JS_GetGlobalObject(context_);
    JSValue test_func_handle = JS_GetPropertyStr(context_, test_global_handle, "jmespath");
    bool is_function_handle = JS_IsFunction(context_, test_func_handle);
    LOG_WARN("jmespath function status after setting __drools_handle_id: {}", is_function_handle ? "FUNCTION" : "NOT FUNCTION");
    JS_FreeValue(context_, test_func_handle);
    JS_FreeValue(context_, test_global_handle);
    
    // Store the current token pointer for use in insertLogical
    JS_SetPropertyStr(context_, global, "__current_token", JS_NewBigUint64(context_, reinterpret_cast<uint64_t>(&current_token)));
    
    // Check jmespath function status after setting token
    JSValue test_global_token = JS_GetGlobalObject(context_);
    JSValue test_func_token = JS_GetPropertyStr(context_, test_global_token, "jmespath");
    bool is_function_token = JS_IsFunction(context_, test_func_token);
    LOG_WARN("jmespath function status after setting __current_token: {}", is_function_token ? "FUNCTION" : "NOT FUNCTION");
    JS_FreeValue(context_, test_func_token);
    JS_FreeValue(context_, test_global_token);
    
    // Create C++ callback function for drools.insert with exception boundary
    JSValue insert_func = JS_NewCFunction(context_, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
        try {
            if (argc != 1 || !JS_IsObject(argv[0])) {
                return JS_ThrowTypeError(ctx, "drools.insert requires one object argument");
            }
            
            // Get the manager instance using handle
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue handle_val = JS_GetPropertyStr(ctx, global, "__drools_handle_id");
            int32_t handle_id;
            JS_ToInt32(ctx, &handle_id, handle_val);
            
            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, handle_val);
            
            JSScriptingManager* manager = JSHandleManager::instance().get_manager(static_cast<uint32_t>(handle_id));
            
            if (manager) {
                auto fact = manager->fact_from_js_object(argv[0]);
                if (fact) {
                    manager->callback_provider_.add_fact(fact);
                    LOG_WARN("drools.insert: Created fact ID {} type '{}'", fact->id, fact->type);
                }
            } else {
                LOG_ERROR("Invalid handle ID: {}", handle_id);
            }
            return JS_UNDEFINED;
        } catch (std::exception const& e) {
            // Convert C++ exception to JavaScript error
            return JS_ThrowInternalError(ctx, "C++ exception: %s", e.what());
        } catch (...) {
            return JS_ThrowInternalError(ctx, "Unknown C++ exception");
        }
    }, "insert", 1);

    // Create C++ callback function for drools.insertLogical with exception boundary
    JSValue insert_logical_func = JS_NewCFunction(context_, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
        try {
            if (argc != 1 || !JS_IsObject(argv[0])) {
                return JS_ThrowTypeError(ctx, "drools.insertLogical requires one object argument");
            }
            
            // Get the manager instance using handle
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue handle_val = JS_GetPropertyStr(ctx, global, "__drools_handle_id");
            JSValue token_val = JS_GetPropertyStr(ctx, global, "__current_token");
            
            int32_t handle_id;
            JS_ToInt32(ctx, &handle_id, handle_val);
            
            uint64_t token_ptr;
            JS_ToBigUint64(ctx, &token_ptr, token_val);
            
            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, handle_val);
            JS_FreeValue(ctx, token_val);
            
            JSScriptingManager* manager = JSHandleManager::instance().get_manager(static_cast<uint32_t>(handle_id));
            Token* current_token = reinterpret_cast<Token*>(token_ptr);
            
            if (manager && current_token) {
                auto fact = manager->fact_from_js_object(argv[0]);
                if (fact) {
                    manager->callback_provider_.logical_insert(*current_token, fact);
                    LOG_WARN("drools.insertLogical: Created logical fact ID {} type '{}'", fact->id, fact->type);
                }
            } else {
                LOG_ERROR("Invalid handle ID: {} or token pointer", handle_id);
            }
            return JS_UNDEFINED;
        } catch (std::exception const& e) {
            // Convert C++ exception to JavaScript error
            return JS_ThrowInternalError(ctx, "C++ exception: %s", e.what());
        } catch (...) {
            return JS_ThrowInternalError(ctx, "Unknown C++ exception");
        }
    }, "insertLogical", 1);

    // Create C++ callback function for drools.retract with exception boundary
    JSValue retract_func = JS_NewCFunction(context_, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
        try {
            if (argc != 1) {
                return JS_ThrowTypeError(ctx, "drools.retract requires one argument (fact object or fact ID)");
            }
            
            // Get the manager instance using handle
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue handle_val = JS_GetPropertyStr(ctx, global, "__drools_handle_id");
            int32_t handle_id;
            JS_ToInt32(ctx, &handle_id, handle_val);
            
            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, handle_val);
            
            JSScriptingManager* manager = JSHandleManager::instance().get_manager(static_cast<uint32_t>(handle_id));
            
            if (manager) {
                // Support both fact objects and direct fact IDs
                if (JS_IsObject(argv[0])) {
                    // If it's an object, get the fact by ID
                    JSValue id_val = JS_GetPropertyStr(ctx, argv[0], "id");
                    if (JS_IsNumber(id_val)) {
                        int64_t fact_id;
                        JS_ToInt64(ctx, &fact_id, id_val);
                        auto fact_opt = manager->callback_provider_.get_fact_by_id(fact_id);
                        if (fact_opt && *fact_opt) {
                            manager->callback_provider_.retract_fact(*fact_opt);
                            LOG_WARN("drools.retract: Retracted fact ID {} type '{}'", fact_id, (*fact_opt)->type);
                        } else {
                            LOG_WARN("drools.retract: Fact ID {} not found", fact_id);
                        }
                        JS_FreeValue(ctx, id_val);
                    } else {
                        JS_FreeValue(ctx, id_val);
                        return JS_ThrowTypeError(ctx, "Fact object must have an 'id' property");
                    }
                } else if (JS_IsNumber(argv[0])) {
                    // Direct fact ID
                    int64_t fact_id;
                    JS_ToInt64(ctx, &fact_id, argv[0]);
                    auto fact_opt = manager->callback_provider_.get_fact_by_id(fact_id);
                    if (fact_opt && *fact_opt) {
                        manager->callback_provider_.retract_fact(*fact_opt);
                        LOG_WARN("drools.retract: Retracted fact ID {} type '{}'", fact_id, (*fact_opt)->type);
                    } else {
                        LOG_WARN("drools.retract: Fact ID {} not found", fact_id);
                    }
                } else {
                    return JS_ThrowTypeError(ctx, "drools.retract argument must be a fact object or fact ID");
                }
            } else {
                LOG_ERROR("Invalid handle ID: {}", handle_id);
            }
            return JS_UNDEFINED;
        } catch (std::exception const& e) {
            // Convert C++ exception to JavaScript error
            return JS_ThrowInternalError(ctx, "C++ exception: %s", e.what());
        } catch (...) {
            return JS_ThrowInternalError(ctx, "Unknown C++ exception");
        }
    }, "retract", 1);
    
    // Create the drools object and set all three methods
    JSValue drools = JS_NewObject(context_);
    JS_SetPropertyStr(context_, drools, "insert", insert_func);
    JS_SetPropertyStr(context_, drools, "insertLogical", insert_logical_func);
    JS_SetPropertyStr(context_, drools, "retract", retract_func);
    JS_SetPropertyStr(context_, global, "drools", drools);
    
    // Note: jmespath functions are already bound during initialization
    
    // Check jmespath function status at the END of create_drools_api
    JSValue test_global_end = JS_GetGlobalObject(context_);
    JSValue test_func_end = JS_GetPropertyStr(context_, test_global_end, "jmespath");
    bool is_function_end = JS_IsFunction(context_, test_func_end);
    LOG_WARN("jmespath function status at END of create_drools_api: {}", is_function_end ? "FUNCTION" : "NOT FUNCTION");
    JS_FreeValue(context_, test_func_end);
    JS_FreeValue(context_, test_global_end);
    
    JS_FreeValue(context_, global);
}

bool JSScriptingManager::execute_eval(std::string const& code, Token const& token,
                                      map<std::string, int> const& bindings) {
    if (code.empty()) return true;
    LOG_DEBUG("Executing JavaScript eval: '{}'", code);
    try {
        bind_variables(token, bindings);
        
        JSValue result = JS_Eval(context_, code.c_str(), code.length(), "<eval>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) {
            JSValue exception = JS_GetException(context_);
            std::string error_msg = get_js_string(exception);
            JS_FreeValue(context_, exception);
            JS_FreeValue(context_, result);
            throw std::runtime_error(error_msg);
        }
        
        bool bool_result = get_js_bool(result);
        JS_FreeValue(context_, result);
        LOG_DEBUG("JavaScript eval result: {}", bool_result);
        return bool_result;
    } catch (std::exception const& e) {
        LOG_ERROR("\n--- JAVASCRIPT EVAL ERROR ---\n"
                  "Expression: '{}'\n"
                  "Error: {}\n----------------------",
                  code, e.what());
        std::cerr << "\n--- JAVASCRIPT EVAL ERROR ---\n"
                  << "Expression: '" << code << "'\n"
                  << "Error: " << e.what() << "\n----------------------\n";
        return false;
    } catch (...) {
        LOG_ERROR("Unknown exception during JavaScript eval");
        return false;
    }
}

void JSScriptingManager::execute_rhs(std::string const& rhs_code, std::string const& rule_name, Token& token,
                                     map<std::string, int> const& bindings) {
    LOG_WARN("Executing RHS for rule '{}' with {} bindings", rule_name, bindings.size());
    LOG_WARN("RHS code:\n{}", rhs_code);
    try {
        LOG_DEBUG("Step 1: Binding variables");
        bind_variables(token, bindings);
        
        LOG_DEBUG("Step 2: Creating drools API");
        create_drools_api(token);
        
        // Final check right before JavaScript execution
        JSValue test_global_final = JS_GetGlobalObject(context_);
        JSValue test_func_final = JS_GetPropertyStr(context_, test_global_final, "jmespath");
        bool is_function_final = JS_IsFunction(context_, test_func_final);
        LOG_WARN("jmespath function status RIGHT BEFORE JS execution: {}", is_function_final ? "FUNCTION" : "NOT FUNCTION");
        JS_FreeValue(context_, test_func_final);
        JS_FreeValue(context_, test_global_final);
        
        LOG_DEBUG("Step 3: About to execute JavaScript code");
        JSValue result = JS_Eval(context_, rhs_code.c_str(), rhs_code.length(), rule_name.c_str(), JS_EVAL_TYPE_GLOBAL);
        
        LOG_DEBUG("Step 4: JavaScript execution completed");
        if (JS_IsException(result)) {
            JSValue exception = JS_GetException(context_);
            std::string error_msg = get_js_string(exception);
            JS_FreeValue(context_, exception);
            JS_FreeValue(context_, result);
            throw ReteExecutionException(error_msg, rule_name);
        }
        JS_FreeValue(context_, result);
        LOG_DEBUG("Step 5: RHS execution completed successfully");

    } catch (ReteExecutionException const&) {
        // Re-throw specific exceptions
        throw;
    } catch (std::exception const& e) {
        std::string error_msg = "JavaScript execution error: " + std::string(e.what());
        LOG_ERROR("Error executing RHS for rule '{}': {}", rule_name, error_msg);
        throw ReteExecutionException(error_msg, rule_name);
    } catch (...) {
        std::string error_msg = "Unknown exception during JavaScript execution";
        LOG_ERROR("Unknown error executing RHS for rule '{}'", rule_name);
        throw ReteExecutionException(error_msg, rule_name);
    }
}

void JSScriptingManager::bind_globals() {
    for (auto const& [name, obj] : callback_provider_.get_global_values()) {
        JSValue global = JS_GetGlobalObject(context_);
        if (!name.empty() && name[0] == '$') {
            // Convert sol::object to JSValue - this would need proper implementation
            // JS_SetPropertyStr(context_, global, name.substr(1).c_str(), converted_val);
        } else {
            // JS_SetPropertyStr(context_, global, name.c_str(), converted_val);
        }
        JS_FreeValue(context_, global);
    }
}

void JSScriptingManager::set_global(std::string const& name, JSValue obj) {
    JSValue global = JS_GetGlobalObject(context_);
    JS_SetPropertyStr(context_, global, name.c_str(), JS_DupValue(context_, obj));
    JS_FreeValue(context_, global);
}

// Helper methods for JS object manipulation
JSValue JSScriptingManager::create_js_object() {
    return JS_NewObject(context_);
}

void JSScriptingManager::set_js_property(JSValue obj, const char* prop, JSValue val) {
    JS_SetPropertyStr(context_, obj, prop, val);
}

JSValue JSScriptingManager::get_js_property(JSValue obj, const char* prop) {
    return JS_GetPropertyStr(context_, obj, prop);
}

JSValue JSScriptingManager::js_string(std::string const& str) {
    return JS_NewString(context_, str.c_str());
}

JSValue JSScriptingManager::js_number(double num) {
    return JS_NewFloat64(context_, num);
}

JSValue JSScriptingManager::js_int(int64_t num) {
    return JS_NewInt64(context_, num);
}

JSValue JSScriptingManager::js_bool(bool val) {
    return JS_NewBool(context_, val);
}

std::string JSScriptingManager::get_js_string(JSValue val) {
    const char* str = JS_ToCString(context_, val);
    if (!str) return "";
    std::string result(str);
    JS_FreeCString(context_, str);
    return result;
}

double JSScriptingManager::get_js_number(JSValue val) {
    double result = 0.0;
    JS_ToFloat64(context_, &result, val);
    return result;
}

int64_t JSScriptingManager::get_js_int(JSValue val) {
    int64_t result = 0;
    JS_ToInt64(context_, &result, val);
    return result;
}

bool JSScriptingManager::get_js_bool(JSValue val) {
    return JS_ToBool(context_, val) != 0;
}



// JMESPath implementation using jsoncons
static JSValue jmespath_native_func(JSContext* ctx, JSValueConst this_val, 
                                   int argc, JSValueConst* argv) {
    LOG_DEBUG("jmespath_native_func called with {} arguments", argc);
    
    if (argc < 2) {
        LOG_ERROR("jmespath requires 2 arguments, got {}", argc);
        return JS_NULL;
    }
    
    const char* json_str_c = nullptr;
    const char* jmespath_expr_c = nullptr;
    
    try {
        // Get JSON string and jmespath expression from JavaScript
        json_str_c = JS_ToCString(ctx, argv[0]);
        jmespath_expr_c = JS_ToCString(ctx, argv[1]);
        
        if (!json_str_c || !jmespath_expr_c) {
            if (json_str_c) JS_FreeCString(ctx, json_str_c);
            if (jmespath_expr_c) JS_FreeCString(ctx, jmespath_expr_c);
            LOG_ERROR("jmespath arguments are null");
            return JS_NULL;
        }
        
        std::string json_str(json_str_c);
        std::string jmespath_expr(jmespath_expr_c);

        LOG_DEBUG("Executing jmespath query '{}' on JSON: {}", jmespath_expr, json_str);
        
        // Parse JSON using jsoncons
        jsoncons::json data = jsoncons::json::parse(json_str);
        
        // Perform JMESPath query using jsoncons
        jsoncons::json result = jsoncons::jmespath::search(data, jmespath_expr);
        
        // Convert result back to JSON string
        std::string result_json = result.to_string();
        
        // Parse the result JSON string into JavaScript value with error checking
        JSValue js_result = JS_ParseJSON(ctx, result_json.c_str(), 
                                       result_json.length(), nullptr);
        
        // Clean up C strings
        JS_FreeCString(ctx, json_str_c);
        JS_FreeCString(ctx, jmespath_expr_c);
        
        if (JS_IsException(js_result)) {
            LOG_ERROR("Failed to parse jmespath result as JSON: {}", result_json);
            return JS_NULL;
        }
        
        LOG_DEBUG("JMESPath query successful, result: {}", result_json);
        return js_result;
        
    } catch (const jsoncons::jmespath::jmespath_error& e) {
        if (json_str_c) JS_FreeCString(ctx, json_str_c);
        if (jmespath_expr_c) JS_FreeCString(ctx, jmespath_expr_c);
        LOG_ERROR("JMESPath error: {}", e.what());
        return JS_NULL;
    } catch (const jsoncons::json_exception& e) {
        if (json_str_c) JS_FreeCString(ctx, json_str_c);
        if (jmespath_expr_c) JS_FreeCString(ctx, jmespath_expr_c);
        LOG_ERROR("JSON parsing error: {}", e.what());
        return JS_NULL;
    } catch (const std::exception& e) {
        // Ensure cleanup on exception
        if (json_str_c) JS_FreeCString(ctx, json_str_c);
        if (jmespath_expr_c) JS_FreeCString(ctx, jmespath_expr_c);
        LOG_ERROR("jmespath exception: {}", e.what());
        return JS_NULL;
    } catch (...) {
        // Handle any other exceptions
        if (json_str_c) JS_FreeCString(ctx, json_str_c);
        if (jmespath_expr_c) JS_FreeCString(ctx, jmespath_expr_c);
        LOG_ERROR("Unknown jmespath exception");
        return JS_NULL;
    }
}

void JSScriptingManager::bind_jmespath_functions() {
    LOG_DEBUG("JSScriptingManager::bind_jmespath_functions starting");
    
    // Register the static function
    JSValue global = JS_GetGlobalObject(context_);
    if (JS_IsException(global)) {
        LOG_ERROR("Failed to get global object");
        return;
    }
    
    // First try with a different name to test if there's a naming conflict
    JSValue jmespath_func = JS_NewCFunction(context_, jmespath_native_func, "jmespath_query", 2);
    if (JS_IsException(jmespath_func)) {
        LOG_ERROR("Failed to create jmespath function");
        JS_FreeValue(context_, global);
        return;
    }
    
    int result = JS_SetPropertyStr(context_, global, "jmespath", jmespath_func);
    if (result < 0) {
        LOG_ERROR("Failed to set jmespath property, result: {}", result);
        JS_FreeValue(context_, jmespath_func);
    } else {
        LOG_DEBUG("Successfully set jmespath property");
    }
    
    JS_FreeValue(context_, global);
    
    // Verify the function was set correctly
    JSValue test_global = JS_GetGlobalObject(context_);
    JSValue test_func = JS_GetPropertyStr(context_, test_global, "jmespath");
    bool is_function = JS_IsFunction(context_, test_func);
    
    // Add extra debugging
    if (is_function) {
        LOG_INFO("JMESPath function verification: PASS");
    } else {
        LOG_ERROR("JMESPath function verification: FAIL - not a function");
        
        // Check what type it actually is
        if (JS_IsUndefined(test_func)) {
            LOG_ERROR("jmespath property is undefined");
        } else if (JS_IsNull(test_func)) {
            LOG_ERROR("jmespath property is null");
        } else {
            LOG_ERROR("jmespath property exists but is not a function");
        }
    }
    
    LOG_INFO("JMESPath functions bound successfully, is_function: {}", is_function);
    
    JS_FreeValue(context_, test_func);
    JS_FreeValue(context_, test_global);
}

std::string JSScriptingManager::fact_to_json(Fact const& fact) {
    try {
        // Build JSON structure using glaze json_t
        glz::json_t json_obj;
        
        // Set the type field
        json_obj["type"] = fact.type;
        
        // Convert all fact fields to JSON
        for (auto const& [key, val] : fact.fields) {
            std::visit([&json_obj, &key](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    json_obj[key] = value;
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    json_obj[key] = value;
                } else if constexpr (std::is_same_v<T, double>) {
                    json_obj[key] = value;
                } else if constexpr (std::is_same_v<T, bool>) {
                    json_obj[key] = value;
                } else {
                    // For other types, convert to string representation
                    json_obj[key] = std::string("unsupported_type");
                }
            }, val);
        }
        
        // Include fact ID if available
        if (fact.id != -1) {
            json_obj["id"] = fact.id;
        }
        
        // Serialize to JSON string using glaze
        auto json_result = glz::write_json(json_obj);
        if (!json_result) {
            LOG_ERROR("Failed to serialize Fact to JSON");
            return "{}";
        }
        
        return json_result.value();
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during Fact to JSON conversion: {}", e.what());
        return "{}";
    }
}