#include "rfl_js_manager.hpp"
#include "logging_control.hpp"

#include "rfl_rete_defs.hpp"
#include "i_network_callback.hpp"
#include "js_handle_manager.hpp"
#include "token_handle_manager.hpp"

#include <algorithm>
#include <sstream>
#include <limits>
#include <vector>

using namespace ruleforge;
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

JSScriptingManager::JSScriptingManager(INetworkCallback& callback_provider,
                                       std::chrono::milliseconds execution_timeout)
    : callback_provider_(callback_provider), execution_timeout_(execution_timeout) {
    runtime_ = JS_NewRuntime();
    if (!runtime_) {
        throw std::runtime_error("Failed to create QuickJS runtime");
    }

    // PROD-001: Set up interrupt handler for timeout
    if (execution_timeout_.count() > 0) {
        JS_SetInterruptHandler(runtime_, interrupt_handler, this);
    }

    context_ = JS_NewContext(runtime_);
    if (!context_) {
        JS_FreeRuntime(runtime_);
        throw std::runtime_error("Failed to create QuickJS context");
    }

    // Store this pointer in context for native function callbacks
    JS_SetContextOpaque(context_, this);

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
        logi("[console.log] {}", output);
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

// PROD-001: Interrupt handler for JavaScript execution timeout
int JSScriptingManager::interrupt_handler(JSRuntime* rt, void* opaque) {
    auto* self = static_cast<JSScriptingManager*>(opaque);

    // If timeout is disabled, never interrupt
    if (self->execution_timeout_.count() == 0) {
        return 0;
    }

    auto elapsed = std::chrono::steady_clock::now() - self->rhs_start_time_;
    if (elapsed > self->execution_timeout_) {
        self->timeout_occurred_ = true;
        return 1;  // Non-zero = interrupt execution
    }
    return 0;  // Zero = continue execution
}

JSContext* JSScriptingManager::get_js_context() {
    return context_;
}

void JSScriptingManager::load_functions(std::vector<ParsedFunction> const& functions) {
    for (auto const& func : functions) {
        try {
            std::string script = "function " + func.name + "(" + func.parameter_list + ") {\n" + func.body + "\n}";
            logd("Loading JavaScript function '{}'", func.name);

            JSValue result = JS_Eval(context_, script.c_str(), script.length(), "<function>", JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(result)) {
                JSValue exception = JS_GetException(context_);
                std::string error_msg = get_js_string(exception);
                JS_FreeValue(context_, exception);
                loge("JavaScript function load error for '{}': {}", func.name, error_msg);
            }
            JS_FreeValue(context_, result);
        } catch (std::exception const& e) {
            loge("JavaScript function load error for '{}': {}", func.name, e.what());
        }
    }
}

// Static callback wrapper for native functions
static JSValue native_function_wrapper(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    try {
        // Get the manager from context opaque (set in constructor)
        auto* manager = static_cast<JSScriptingManager*>(JS_GetContextOpaque(ctx));
        if (!manager) {
            return JS_ThrowInternalError(ctx, "No JSScriptingManager associated with this context");
        }

        // magic contains the per-instance function ID
        auto it = manager->func_by_id_.find(magic);
        if (it == manager->func_by_id_.end()) {
            return JS_ThrowInternalError(ctx, "Native function ID %d not found in registry", magic);
        }

        auto* func_data = it->second;

        // Convert JS arguments to JSON strings
        std::vector<std::string> arg_storage;
        std::vector<const char*> args;

        for (int i = 0; i < argc; i++) {
            // Convert each argument to JSON string
            JSValue json_str = JS_JSONStringify(ctx, argv[i], JS_NULL, JS_NULL);
            if (JS_IsException(json_str)) {
                // Fallback to string conversion
                const char* str = JS_ToCString(ctx, argv[i]);
                if (str) {
                    arg_storage.push_back(str);
                    JS_FreeCString(ctx, str);
                } else {
                    arg_storage.push_back("null");
                }
            } else {
                const char* str = JS_ToCString(ctx, json_str);
                if (str) {
                    arg_storage.push_back(str);
                    JS_FreeCString(ctx, str);
                }
                JS_FreeValue(ctx, json_str);
            }
            args.push_back(arg_storage.back().c_str());
        }

        // Call the native C function
        char* result = nullptr;
        int status = func_data->callback(func_data->user_data, argc, args.data(), &result);

        if (status != 0) {  // RULES_FORGE_OK = 0
            std::string error_msg = "Native function failed";
            if (result) {
                error_msg += ": ";
                error_msg += result;
                free(result);
            }
            return JS_ThrowInternalError(ctx, "%s", error_msg.c_str());
        }

        JSValue ret = JS_UNDEFINED;
        if (result) {
            // Parse JSON result
            ret = JS_ParseJSON(ctx, result, strlen(result), nullptr);
            free(result);

            if (JS_IsException(ret)) {
                return JS_ThrowInternalError(ctx, "Failed to parse native function result as JSON");
            }
        }

        return ret;
    } catch (std::exception const& e) {
        return JS_ThrowInternalError(ctx, "C++ exception in native function: %s", e.what());
    } catch (...) {
        return JS_ThrowInternalError(ctx, "Unknown C++ exception in native function");
    }
}

void JSScriptingManager::register_native_functions(std::map<std::string, NativeFunction> const& functions) {
    if (functions.empty()) {
        logd("No native functions to register");
        return;
    }

    JSValue global = JS_GetGlobalObject(context_);

    for (auto const& [name, native_func] : functions) {
        logd("Registering native function '{}'", name);

        // Assign per-instance unique ID
        int func_id = next_func_id_++;

        // Store function data in manager
        native_func_data_[name] = std::make_unique<NativeFuncData>(
            NativeFuncData{native_func.callback, native_func.user_data, this, func_id}
        );

        // Register in per-instance map for lookup by ID
        func_by_id_[func_id] = native_func_data_[name].get();

        // Create JS function using JS_NewCFunction2 with magic parameter
        // Cast to JSCFunction* to match the expected signature
        JSValue js_func = JS_NewCFunction2(
            context_,
            reinterpret_cast<JSCFunction*>(native_function_wrapper),
            name.c_str(),
            0,  // length (variable args)
            JS_CFUNC_generic_magic,
            func_id  // magic = function ID
        );

        // Register the function globally
        JS_SetPropertyStr(context_, global, name.c_str(), js_func);

        // Verify it was registered
        JSValue test_val = JS_GetPropertyStr(context_, global, name.c_str());
        bool is_func = JS_IsFunction(context_, test_val);
        JS_FreeValue(context_, test_val);

        logi("Native function '{}' registered successfully with ID {} (is_function: {})", name, func_id, is_func);
    }

    JS_FreeValue(context_, global);
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
    logd("bind_variables: Processing {} bindings", bindings.size());

    for (auto const& [binding, depth] : bindings) {
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
                std::string json_var_name = js_var_name + "_json";
                std::string fact_json = fact_to_json(**current_fact_opt);
                JSValue json_str = JS_NewString(context_, fact_json.c_str());
                JS_SetPropertyStr(context_, global, json_var_name.c_str(), json_str);

                // Special case: if variable is $JSON, bind it directly for convenience
                if (js_var_name == "JSON") {
                    JS_SetPropertyStr(context_, global, "JSON_data", json_str);
                }

                JS_FreeValue(context_, global);
                logd("Bound variable '{}' to fact ID {}", js_var_name, (*current_fact_opt)->id);
            }
        }
    }
}

void JSScriptingManager::create_rfl_api(Token& current_token) {
    JSValue global = JS_GetGlobalObject(context_);

    // Store the handle ID instead of raw pointer
    JS_SetPropertyStr(context_, global, "__rfl_handle_id", JS_NewInt32(context_, static_cast<int32_t>(handle_id_)));

    // P0-001 FIX: Use handle-based token management instead of raw pointer
    // Register token and store handle ID (not raw pointer) - safer lifetime management
    auto token_handle = TokenHandleManager::instance().register_token(&current_token);
    JS_SetPropertyStr(context_, global, "__current_token_handle", JS_NewInt32(context_, static_cast<int32_t>(token_handle)));
    current_token_handle_ = token_handle;  // Store for cleanup

    // Create C++ callback function for rfl.insert with exception boundary
    JSValue insert_func = JS_NewCFunction(context_, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
        try {
            if (argc != 1 || !JS_IsObject(argv[0])) {
                return JS_ThrowTypeError(ctx, "rfl.insert requires one object argument");
            }

            // Get the manager instance using handle
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue handle_val = JS_GetPropertyStr(ctx, global, "__rfl_handle_id");
            int32_t handle_id;
            JS_ToInt32(ctx, &handle_id, handle_val);

            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, handle_val);

            JSScriptingManager* manager = JSHandleManager::instance().get_manager(static_cast<uint32_t>(handle_id));

            if (manager) {
                auto fact = manager->fact_from_js_object(argv[0]);
                if (fact) {
                    manager->callback_provider_.add_fact(fact);
                    logd("rfl.insert: Created fact ID {} type '{}'", fact->id, fact->type);
                }
            } else {
                loge("Invalid handle ID: {}", handle_id);
            }
            return JS_UNDEFINED;
        } catch (std::exception const& e) {
            // Convert C++ exception to JavaScript error
            return JS_ThrowInternalError(ctx, "C++ exception: %s", e.what());
        } catch (...) {
            return JS_ThrowInternalError(ctx, "Unknown C++ exception");
        }
    }, "insert", 1);

    // Create C++ callback function for rfl.insertLogical with exception boundary
    JSValue insert_logical_func = JS_NewCFunction(context_, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
        try {
            if (argc != 1 || !JS_IsObject(argv[0])) {
                return JS_ThrowTypeError(ctx, "rfl.insertLogical requires one object argument");
            }

            // Get the manager instance using handle
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue handle_val = JS_GetPropertyStr(ctx, global, "__rfl_handle_id");
            JSValue token_handle_val = JS_GetPropertyStr(ctx, global, "__current_token_handle");

            int32_t handle_id;
            JS_ToInt32(ctx, &handle_id, handle_val);

            // P0-001 FIX: Use TokenHandleManager instead of raw pointer
            int32_t token_handle;
            JS_ToInt32(ctx, &token_handle, token_handle_val);

            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, handle_val);
            JS_FreeValue(ctx, token_handle_val);

            JSScriptingManager* manager = JSHandleManager::instance().get_manager(static_cast<uint32_t>(handle_id));
            Token* current_token = TokenHandleManager::instance().get_token(static_cast<uint32_t>(token_handle));

            if (manager && current_token) {
                auto fact = manager->fact_from_js_object(argv[0]);
                if (fact) {
                    manager->callback_provider_.logical_insert(*current_token, fact);
                    logd("rfl.insertLogical: Created logical fact ID {} type '{}'", fact->id, fact->type);
                }
            } else {
                if (!manager) {
                    loge("rfl.insertLogical: Invalid manager handle ID: {}", handle_id);
                }
                if (!current_token) {
                    loge("rfl.insertLogical: Invalid or expired token handle: {}", token_handle);
                }
            }
            return JS_UNDEFINED;
        } catch (std::exception const& e) {
            // Convert C++ exception to JavaScript error
            return JS_ThrowInternalError(ctx, "C++ exception: %s", e.what());
        } catch (...) {
            return JS_ThrowInternalError(ctx, "Unknown C++ exception");
        }
    }, "insertLogical", 1);

    // Create C++ callback function for rfl.retract with exception boundary
    JSValue retract_func = JS_NewCFunction(context_, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
        try {
            if (argc != 1) {
                return JS_ThrowTypeError(ctx, "rfl.retract requires one argument (fact object or fact ID)");
            }

            // Get the manager instance using handle
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue handle_val = JS_GetPropertyStr(ctx, global, "__rfl_handle_id");
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
                            logd("rfl.retract: Retracted fact ID {} type '{}'", fact_id, (*fact_opt)->type);
                        } else {
                            logd("rfl.retract: Fact ID {} not found", fact_id);
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
                        logd("rfl.retract: Retracted fact ID {} type '{}'", fact_id, (*fact_opt)->type);
                    } else {
                        logd("rfl.retract: Fact ID {} not found", fact_id);
                    }
                } else {
                    return JS_ThrowTypeError(ctx, "rfl.retract argument must be a fact object or fact ID");
                }
            } else {
                loge("Invalid handle ID: {}", handle_id);
            }
            return JS_UNDEFINED;
        } catch (std::exception const& e) {
            // Convert C++ exception to JavaScript error
            return JS_ThrowInternalError(ctx, "C++ exception: %s", e.what());
        } catch (...) {
            return JS_ThrowInternalError(ctx, "Unknown C++ exception");
        }
    }, "retract", 1);

    // Create C++ callback function for rfl.update with exception boundary
    JSValue update_func = JS_NewCFunction(context_, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
        try {
            if (argc < 1 || !JS_IsObject(argv[0])) {
                return JS_ThrowTypeError(ctx, "rfl.update requires at least one argument (fact object)");
            }

            // Get the manager instance using handle
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue handle_val = JS_GetPropertyStr(ctx, global, "__rfl_handle_id");
            int32_t handle_id;
            JS_ToInt32(ctx, &handle_id, handle_val);

            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, handle_val);

            JSScriptingManager* manager = JSHandleManager::instance().get_manager(static_cast<uint32_t>(handle_id));

            if (manager) {
                // Get the fact by ID from the first argument
                JSValue id_val = JS_GetPropertyStr(ctx, argv[0], "id");
                if (!JS_IsNumber(id_val)) {
                    JS_FreeValue(ctx, id_val);
                    return JS_ThrowTypeError(ctx, "Fact object must have an 'id' property");
                }

                int64_t fact_id;
                JS_ToInt64(ctx, &fact_id, id_val);
                JS_FreeValue(ctx, id_val);

                auto fact_opt = manager->callback_provider_.get_fact_by_id(fact_id);
                if (!fact_opt || !*fact_opt) {
                    logd("rfl.update: Fact ID {} not found", fact_id);
                    return JS_UNDEFINED;
                }

                auto fact = *fact_opt;

                // If there's a second argument (update object), apply those changes
                if (argc >= 2 && JS_IsObject(argv[1])) {
                    JSPropertyEnum* props;
                    uint32_t prop_count;
                    if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, argv[1], JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) >= 0) {
                        for (uint32_t i = 0; i < prop_count; i++) {
                            JSValue key_val = JS_AtomToValue(ctx, props[i].atom);
                            const char* key_str = JS_ToCString(ctx, key_val);
                            std::string key(key_str ? key_str : "");
                            JS_FreeCString(ctx, key_str);
                            JS_FreeValue(ctx, key_val);

                            if (key == "type" || key == "id") continue;

                            JSValue val = JS_GetProperty(ctx, argv[1], props[i].atom);
                            if (JS_IsString(val)) {
                                const char* str = JS_ToCString(ctx, val);
                                fact->fields[key] = std::string(str ? str : "");
                                JS_FreeCString(ctx, str);
                            } else if (JS_IsNumber(val)) {
                                double num;
                                JS_ToFloat64(ctx, &num, val);
                                if (num == std::floor(num)) {
                                    fact->fields[key] = static_cast<int64_t>(num);
                                } else {
                                    fact->fields[key] = num;
                                }
                            } else if (JS_IsBool(val)) {
                                fact->fields[key] = static_cast<int64_t>(JS_ToBool(ctx, val));
                            } else if (JS_IsNull(val) || JS_IsUndefined(val)) {
                                fact->fields[key] = NilValue{};
                            }
                            JS_FreeValue(ctx, val);
                        }
                        js_free(ctx, props);
                    }
                }

                // Propagate the update through the RETE network
                manager->callback_provider_.update_fact(fact, [](Fact&) {});
                logd("rfl.update: Updated fact ID {} type '{}'", fact_id, fact->type);
            } else {
                loge("Invalid handle ID: {}", handle_id);
            }
            return JS_UNDEFINED;
        } catch (std::exception const& e) {
            return JS_ThrowInternalError(ctx, "C++ exception: %s", e.what());
        } catch (...) {
            return JS_ThrowInternalError(ctx, "Unknown C++ exception");
        }
    }, "update", 2);

    // P1 FIX: Create C++ callback function for rfl.halt()
    JSValue halt_func = JS_NewCFunction(context_, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
        try {
            // Get the manager instance using handle
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue handle_val = JS_GetPropertyStr(ctx, global, "__rfl_handle_id");
            int32_t handle_id;
            JS_ToInt32(ctx, &handle_id, handle_val);

            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, handle_val);

            JSScriptingManager* manager = JSHandleManager::instance().get_manager(static_cast<uint32_t>(handle_id));

            if (manager) {
                manager->callback_provider_.halt();
                logd("rfl.halt: Rule execution halt requested");
            } else {
                loge("rfl.halt: Invalid handle ID: {}", handle_id);
            }
            return JS_UNDEFINED;
        } catch (std::exception const& e) {
            return JS_ThrowInternalError(ctx, "C++ exception: %s", e.what());
        } catch (...) {
            return JS_ThrowInternalError(ctx, "Unknown C++ exception");
        }
    }, "halt", 0);
    JSValue set_focus_func = JS_NewCFunction(context_, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
        try {
            if (argc < 1) {
                return JS_ThrowTypeError(ctx, "setFocus requires 1 argument (group name)");
            }

            // Get the group name string
            char const* group_name_cstr = JS_ToCString(ctx, argv[0]);
            if (!group_name_cstr) {
                return JS_ThrowTypeError(ctx, "setFocus argument must be a string");
            }
            std::string group_name(group_name_cstr);
            JS_FreeCString(ctx, group_name_cstr);

            // Get the manager instance using handle
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue handle_val = JS_GetPropertyStr(ctx, global, "__rfl_handle_id");
            int32_t handle_id;
            JS_ToInt32(ctx, &handle_id, handle_val);

            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, handle_val);

            JSScriptingManager* manager = JSHandleManager::instance().get_manager(static_cast<uint32_t>(handle_id));

            if (manager) {
                manager->callback_provider_.set_focus(group_name);
                logd("rfl.setFocus: Setting focus to agenda-group '{}'", group_name);
            } else {
                loge("rfl.setFocus: Invalid handle ID: {}", handle_id);
            }
            return JS_UNDEFINED;
        } catch (std::exception const& e) {
            return JS_ThrowInternalError(ctx, "C++ exception: %s", e.what());
        } catch (...) {
            return JS_ThrowInternalError(ctx, "Unknown C++ exception");
        }
    }, "setFocus", 1);
    JSValue get_rule_func = JS_NewCFunction(context_, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
        try {
            // Get the manager instance using handle
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue handle_val = JS_GetPropertyStr(ctx, global, "__rfl_handle_id");
            int32_t handle_id;
            JS_ToInt32(ctx, &handle_id, handle_val);

            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, handle_val);

            JSScriptingManager* manager = JSHandleManager::instance().get_manager(static_cast<uint32_t>(handle_id));

            if (manager) {
                // Create a rule info object
                JSValue rule_obj = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, rule_obj, "name", JS_NewString(ctx, manager->get_current_rule_name().c_str()));
                logd("rfl.getRule: Returning rule name '{}'", manager->get_current_rule_name());
                return rule_obj;
            } else {
                loge("rfl.getRule: Invalid handle ID: {}", handle_id);
                return JS_NULL;
            }
        } catch (std::exception const& e) {
            return JS_ThrowInternalError(ctx, "C++ exception: %s", e.what());
        } catch (...) {
            return JS_ThrowInternalError(ctx, "Unknown C++ exception");
        }
    }, "getRule", 0);

    // Create the rfl object and set all methods
    JSValue rfl = JS_NewObject(context_);
    JS_SetPropertyStr(context_, rfl, "insert", insert_func);
    JS_SetPropertyStr(context_, rfl, "insertLogical", insert_logical_func);
    JS_SetPropertyStr(context_, rfl, "retract", retract_func);
    JS_SetPropertyStr(context_, rfl, "update", update_func);
    JS_SetPropertyStr(context_, rfl, "halt", halt_func);  // P1 FIX: Add halt
    JS_SetPropertyStr(context_, rfl, "setFocus", set_focus_func);
    JS_SetPropertyStr(context_, rfl, "getRule", get_rule_func);
    JS_SetPropertyStr(context_, global, "rfl", rfl);

    JS_FreeValue(context_, global);
}

bool JSScriptingManager::execute_eval(std::string const& code, Token const& token,
                                      map<std::string, int> const& bindings) {
    if (code.empty()) return true;
    logd("Executing JavaScript eval: '{}'", code);
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
        logd("JavaScript eval result: {}", bool_result);
        return bool_result;
    } catch (std::exception const& e) {
        loge("JavaScript eval error - Expression: '{}', Error: {}", code, e.what());
        return false;
    } catch (...) {
        loge("Unknown exception during JavaScript eval");
        return false;
    }
}

void JSScriptingManager::execute_rhs(std::string const& rhs_code, std::string const& rule_name, Token& token,
                                     ruleforge::map<std::string, int> const& bindings) {
    logd("Executing RHS for rule '{}'", rule_name);

    current_token_handle_ = TokenHandleManager::INVALID_HANDLE;
    current_rule_name_ = rule_name;
    timeout_occurred_ = false;
    rhs_start_time_ = std::chrono::steady_clock::now();

    // RAII: token handle cleanup on any exit path
    auto token_cleanup = [this]() {
        if (current_token_handle_ != TokenHandleManager::INVALID_HANDLE) {
            TokenHandleManager::instance().unregister(current_token_handle_);
            current_token_handle_ = TokenHandleManager::INVALID_HANDLE;
        }
    };
    struct ScopeGuard {
        std::function<void()> fn;
        ~ScopeGuard() { fn(); }
    } token_guard{token_cleanup};

    // RAII: transaction rollback unless explicitly committed
    bool committed = false;
    callback_provider_.begin_rhs_transaction();
    struct TxnGuard {
        INetworkCallback& cb;
        bool& committed;
        ~TxnGuard() { if (!committed) cb.end_rhs_transaction(false); }
    } txn_guard{callback_provider_, committed};

    try {
        bind_variables(token, bindings);
        create_rfl_api(token);

        JSValue result = JS_Eval(context_, rhs_code.c_str(), rhs_code.length(), rule_name.c_str(), JS_EVAL_TYPE_GLOBAL);

        if (timeout_occurred_) {
            JS_FreeValue(context_, result);
            throw JSExecutionTimeoutException(rule_name, execution_timeout_);
        }

        if (JS_IsException(result)) {
            JSValue exception = JS_GetException(context_);
            std::string error_msg = get_js_string(exception);
            JS_FreeValue(context_, exception);
            JS_FreeValue(context_, result);
            throw ReteExecutionException(error_msg, rule_name);
        }
        JS_FreeValue(context_, result);

        callback_provider_.end_rhs_transaction(true);
        committed = true;

    } catch (JSExecutionTimeoutException const&) {
        throw;
    } catch (ReteExecutionException const&) {
        throw;
    } catch (std::exception const& e) {
        std::string error_msg = "JavaScript execution error: " + std::string(e.what());
        loge("Error executing RHS for rule '{}': {}", rule_name, error_msg);
        throw ReteExecutionException(error_msg, rule_name);
    } catch (...) {
        loge("Unknown error executing RHS for rule '{}'", rule_name);
        throw ReteExecutionException("Unknown exception during JavaScript execution", rule_name);
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
    logd("jmespath_native_func called with {} arguments", argc);

    if (argc < 2) {
        loge("jmespath requires 2 arguments, got {}", argc);
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
            loge("jmespath arguments are null");
            return JS_NULL;
        }

        std::string json_str(json_str_c);
        std::string jmespath_expr(jmespath_expr_c);

        logd("Executing jmespath query '{}' on JSON: {}", jmespath_expr, json_str);

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
            loge("Failed to parse jmespath result as JSON: {}", result_json);
            return JS_NULL;
        }

        logd("JMESPath query successful, result: {}", result_json);
        return js_result;

    } catch (const jsoncons::jmespath::jmespath_error& e) {
        if (json_str_c) JS_FreeCString(ctx, json_str_c);
        if (jmespath_expr_c) JS_FreeCString(ctx, jmespath_expr_c);
        loge("JMESPath error: {}", e.what());
        return JS_NULL;
    } catch (const jsoncons::json_exception& e) {
        if (json_str_c) JS_FreeCString(ctx, json_str_c);
        if (jmespath_expr_c) JS_FreeCString(ctx, jmespath_expr_c);
        loge("JSON parsing error: {}", e.what());
        return JS_NULL;
    } catch (const std::exception& e) {
        // Ensure cleanup on exception
        if (json_str_c) JS_FreeCString(ctx, json_str_c);
        if (jmespath_expr_c) JS_FreeCString(ctx, jmespath_expr_c);
        loge("jmespath exception: {}", e.what());
        return JS_NULL;
    } catch (...) {
        // Handle any other exceptions
        if (json_str_c) JS_FreeCString(ctx, json_str_c);
        if (jmespath_expr_c) JS_FreeCString(ctx, jmespath_expr_c);
        loge("Unknown jmespath exception");
        return JS_NULL;
    }
}

void JSScriptingManager::bind_jmespath_functions() {
    logd("JSScriptingManager::bind_jmespath_functions starting");

    // Register the static function
    JSValue global = JS_GetGlobalObject(context_);
    if (JS_IsException(global)) {
        loge("Failed to get global object");
        return;
    }

    // First try with a different name to test if there's a naming conflict
    JSValue jmespath_func = JS_NewCFunction(context_, jmespath_native_func, "jmespath_query", 2);
    if (JS_IsException(jmespath_func)) {
        loge("Failed to create jmespath function");
        JS_FreeValue(context_, global);
        return;
    }

    int result = JS_SetPropertyStr(context_, global, "jmespath", jmespath_func);
    if (result < 0) {
        loge("Failed to set jmespath property, result: {}", result);
        JS_FreeValue(context_, jmespath_func);
    } else {
        logd("Successfully set jmespath property");
    }

    JS_FreeValue(context_, global);

    // Verify the function was set correctly
    JSValue test_global = JS_GetGlobalObject(context_);
    JSValue test_func = JS_GetPropertyStr(context_, test_global, "jmespath");
    bool is_function = JS_IsFunction(context_, test_func);

    // Add extra debugging
    if (is_function) {
        logd("JMESPath function verification: PASS");
    } else {
        loge("JMESPath function verification: FAIL - not a function");

        // Check what type it actually is
        if (JS_IsUndefined(test_func)) {
            loge("jmespath property is undefined");
        } else if (JS_IsNull(test_func)) {
            loge("jmespath property is null");
        } else {
            loge("jmespath property exists but is not a function");
        }
    }

    JS_FreeValue(context_, test_func);
    JS_FreeValue(context_, test_global);
}

std::string JSScriptingManager::fact_to_json(Fact const& fact) {
    try {
        // Build JSON using jsoncons (already linked for jmespath)
        jsoncons::json json_obj = jsoncons::json::object();

        // Set the type field
        json_obj["type"] = fact.type;

        // Include fact ID if available
        if (fact.id != 0) {
            json_obj["id"] = fact.id;
        }

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
                } else if constexpr (std::is_same_v<T, NilValue>) {
                    json_obj[key] = jsoncons::json::null();
                } else if constexpr (std::is_same_v<T, FactList>) {
                    // For FactList, create an array of fact objects
                    jsoncons::json arr = jsoncons::json::array();
                    for (auto const& nested_fact : value.facts) {
                        if (nested_fact) {
                            jsoncons::json nested_obj = jsoncons::json::object();
                            nested_obj["id"] = nested_fact->id;
                            nested_obj["type"] = nested_fact->type;
                            arr.push_back(std::move(nested_obj));
                        }
                    }
                    json_obj[key] = std::move(arr);
                }
            }, val);
        }

        return json_obj.to_string();

    } catch (const std::exception& e) {
        loge("Exception during Fact to JSON conversion: {}", e.what());
        return "{}";
    }
}


