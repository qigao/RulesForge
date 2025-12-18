#ifndef JS_AST_BUILDER_HPP
#define JS_AST_BUILDER_HPP

#include "quickjs.h"
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <set>

// Forward declarations
struct JSFunctionCall {
    std::string object_name;    // e.g., "rfl"
    std::string method_name;    // e.g., "insert"
    std::vector<std::string> arguments; // Raw argument strings
};

struct JSVariableRef {
    std::string name;           // e.g., "p" from "$p.name"
    std::optional<std::string> field; // e.g., "name" from "$p.name"
};

struct JSAstNode {
    enum Type {
        FUNCTION_CALL,
        VARIABLE_REF,
        LITERAL,
        EXPRESSION
    };
    
    Type type;
    std::string raw_code;       // Original code fragment
    
    // Specific node data
    std::optional<JSFunctionCall> function_call;
    std::optional<JSVariableRef> variable_ref;
};

struct JSAstRoot {
    std::vector<std::unique_ptr<JSAstNode>> statements;
    std::string raw_code;           // Original JavaScript code
    bool is_valid = false;
    std::string error_message;
};

class JSAstBuilder {
public:
    JSAstBuilder();
    ~JSAstBuilder();
    
    // Parse JavaScript code and build AST
    JSAstRoot parse(const std::string& js_code);
    
    // Validate JavaScript syntax without building full AST
    bool validate_syntax(const std::string& js_code, std::string& error_message);
    
    // Extract function calls like rfl.insert(), rfl.retract()
    std::vector<JSFunctionCall> extract_function_calls(const std::string& js_code);
    
    // Extract variable references like $p.name
    std::vector<JSVariableRef> extract_variables(const std::string& js_code);

    // Extract locally declared variables (var, let, const)
    std::set<std::string> extract_local_declarations(const std::string& js_code);

private:
    JSContext* context_;
    JSRuntime* runtime_;
    
    // Helper methods
    JSAstRoot build_ast_from_bytecode(JSValue bytecode_obj);
    void analyze_bytecode(JSValue bytecode_obj, JSAstRoot& ast);
};

#endif // JS_AST_BUILDER_HPP

