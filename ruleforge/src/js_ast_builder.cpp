#include "js_ast_builder.hpp"
#include "fmtlog.h"
#include <regex>
#include <sstream>
#include <set>

JSAstBuilder::JSAstBuilder() {
    runtime_ = JS_NewRuntime();
    if (!runtime_) {
        throw std::runtime_error("Failed to create QuickJS runtime");
    }

    context_ = JS_NewContext(runtime_);
    if (!context_) {
        JS_FreeRuntime(runtime_);
        throw std::runtime_error("Failed to create QuickJS context");
    }
}

JSAstBuilder::~JSAstBuilder() {
    if (context_) {
        JS_FreeContext(context_);
    }
    if (runtime_) {
        JS_FreeRuntime(runtime_);
    }
}

bool JSAstBuilder::validate_syntax(const std::string& js_code, std::string& error_message) {
    logd("JSAstBuilder: Validating JavaScript syntax for: {}", js_code);

    // Use JS_EVAL_FLAG_COMPILE_ONLY to parse without executing
    JSValue result = JS_Eval(context_, js_code.c_str(), js_code.length(),
                             "<syntax_check>",
                             JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);

    if (JS_IsException(result)) {
        // Get the exception and extract error message
        JSValue exception = JS_GetException(context_);
        const char* error_str = JS_ToCString(context_, exception);

        if (error_str) {
            error_message = error_str;
            JS_FreeCString(context_, error_str);
        } else {
            error_message = "Unknown JavaScript syntax error";
        }

        JS_FreeValue(context_, exception);
        JS_FreeValue(context_, result);


        return false;
    }

    JS_FreeValue(context_, result);
    logd("JSAstBuilder: Syntax validation succeeded");
    return true;
}

JSAstRoot JSAstBuilder::parse(const std::string& js_code) {
    JSAstRoot ast;
    ast.raw_code = js_code;

    // First validate syntax
    if (!validate_syntax(js_code, ast.error_message)) {
        ast.is_valid = false;
        return ast;
    }

    try {
        // Extract function calls
        auto function_calls = extract_function_calls(js_code);
        for (const auto& call : function_calls) {
            auto node = std::make_unique<JSAstNode>();
            node->type = JSAstNode::FUNCTION_CALL;
            node->function_call = call;
            node->raw_code = call.object_name + "." + call.method_name + "(...)";
            ast.statements.push_back(std::move(node));
        }

        // Extract variable references
        auto variables = extract_variables(js_code);
        for (const auto& var : variables) {
            auto node = std::make_unique<JSAstNode>();
            node->type = JSAstNode::VARIABLE_REF;
            node->variable_ref = var;
            node->raw_code = var.name + (var.field ? "." + *var.field : "");
            ast.statements.push_back(std::move(node));
        }

        ast.is_valid = true;
        logd("JSAstBuilder: Successfully parsed {} statements", ast.statements.size());

    } catch (const std::exception& e) {
        ast.is_valid = false;
        ast.error_message = "Failed to build AST: " + std::string(e.what());
        loge("JSAstBuilder: {}", ast.error_message);
    }

    return ast;
}

std::vector<JSFunctionCall> JSAstBuilder::extract_function_calls(const std::string& js_code) {
    std::vector<JSFunctionCall> calls;

    // Regex to match function calls like: rfl.insert({...})
    // Pattern: object.method(arguments)
    std::regex call_regex(R"((\w+)\.(\w+)\s*\(\s*([^)]*)\s*\))");
    std::smatch match;

    std::string::const_iterator search_start(js_code.cbegin());
    while (std::regex_search(search_start, js_code.cend(), match, call_regex)) {
        JSFunctionCall call;
        call.object_name = match[1].str();
        call.method_name = match[2].str();

        // Parse arguments (simplified - just store the raw string)
        std::string args_str = match[3].str();
        if (!args_str.empty()) {
            call.arguments.push_back(args_str);
        }

        calls.push_back(call);
        search_start = match.suffix().first;

        logd("JSAstBuilder: Found function call: {}.{}({})",
             call.object_name, call.method_name, args_str);
    }

    return calls;
}

std::vector<JSVariableRef> JSAstBuilder::extract_variables(const std::string& js_code) {
    std::vector<JSVariableRef> variables;

    // First, remove string literals to avoid matching patterns inside strings
    // Use character-by-character parsing to avoid regex stack overflow
    std::string code_without_strings;
    code_without_strings.reserve(js_code.size());

    bool in_string = false;
    char string_char = 0;

    for (size_t i = 0; i < js_code.size(); ++i) {
        char c = js_code[i];

        if (!in_string) {
            if (c == '"' || c == '\'' || c == '`') {
                in_string = true;
                string_char = c;
                code_without_strings += ' ';  // Replace string with space
            } else {
                code_without_strings += c;
            }
        } else {
            // Inside string - skip characters
            if (c == '\\' && i + 1 < js_code.size()) {
                ++i;  // Skip escaped character
            } else if (c == string_char) {
                in_string = false;
            }
        }
    }

    // Regex to match variable references in two forms:
    // 1. Rules Forge Language bindings: $p.name, $customer.age
    // 2. Regular JS variables: p.name, customer.age (when not preceded by . or $)
    // Using negative lookbehind to avoid matching object.method patterns
    std::regex var_regex(R"((?:^|[^.\w$])(\$?[a-zA-Z_][a-zA-Z0-9_]*)\.([a-zA-Z_][a-zA-Z0-9_]*)\b)");
    std::smatch match;

    std::string::const_iterator search_start(code_without_strings.cbegin());
    while (std::regex_search(search_start, code_without_strings.cend(), match, var_regex)) {
        JSVariableRef var;
        std::string var_name = match[1].str();

        // Remove $ prefix if present
        if (!var_name.empty() && var_name[0] == '$') {
            var.name = var_name.substr(1);
        } else {
            var.name = var_name;
        }

        if (match[2].matched) {
            var.field = match[2].str();
        }

        variables.push_back(var);
        search_start = match.suffix().first;

        logd("JSAstBuilder: Found variable reference: {}{}",
             var.name, var.field ? "." + *var.field : "");
    }

    return variables;
}

std::set<std::string> JSAstBuilder::extract_local_declarations(const std::string& js_code) {
    std::set<std::string> local_vars;

    // Match: var x, let x, const x (with optional initializer)
    // Also handles: var x = ..., var x, y, z = ...
    std::regex decl_regex(R"(\b(var|let|const)\s+([a-zA-Z_][a-zA-Z0-9_]*))");
    std::smatch match;

    std::string::const_iterator search_start(js_code.cbegin());
    while (std::regex_search(search_start, js_code.cend(), match, decl_regex)) {
        std::string var_name = match[2].str();
        local_vars.insert(var_name);
        logd("JSAstBuilder: Found local declaration: {}", var_name);
        search_start = match.suffix().first;
    }

    return local_vars;
}

JSAstRoot JSAstBuilder::build_ast_from_bytecode(JSValue bytecode_obj) {
    JSAstRoot ast;

    // This would be a more advanced implementation that uses QuickJS
    // internal structures to build a proper AST from bytecode
    // For now, we'll rely on the regex-based approach above

    ast.is_valid = false;
    ast.error_message = "Bytecode AST building not implemented";
    return ast;
}


