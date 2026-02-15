#ifndef JS_AST_BUILDER_HPP
#define JS_AST_BUILDER_HPP

#include <tree_sitter/api.h>
#include <string>
#include <string_view>
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

// Result of single-pass extraction
struct JSExtractionResult {
    bool is_valid = false;
    std::string error_message;
    std::vector<JSFunctionCall> function_calls;
    std::vector<JSVariableRef> variables;
    std::set<std::string> local_declarations;
};

/**
 * @brief JavaScript AST builder using Tree-sitter
 *
 * Replaces regex-based parsing with proper AST traversal for accurate
 * extraction of function calls, variable references, and declarations.
 */
class JSAstBuilder {
public:
    JSAstBuilder();
    ~JSAstBuilder();

    // Non-copyable
    JSAstBuilder(JSAstBuilder const&) = delete;
    JSAstBuilder& operator=(JSAstBuilder const&) = delete;

    // Parse JavaScript code and build AST
    JSAstRoot parse(std::string_view js_code);

    // Single-pass extraction: parse once, extract everything
    JSExtractionResult extract_all(std::string_view js_code);

    // Validate JavaScript syntax without building full AST
    bool validate_syntax(std::string_view js_code, std::string& error_message);

    // Extract function calls like rfl.insert(), rfl.retract()
    std::vector<JSFunctionCall> extract_function_calls(std::string_view js_code);

    // Extract variable references like $p.name or p.name
    std::vector<JSVariableRef> extract_variables(std::string_view js_code);

    // Extract locally declared variables (var, let, const)
    std::set<std::string> extract_local_declarations(std::string_view js_code);

private:
    TSParser* parser_;

    // Helper to get node text from source
    std::string node_text(TSNode node, std::string_view source) const;

    // Recursive AST traversal helpers
    void collect_call_expressions(TSNode node, std::string_view source, std::vector<JSFunctionCall>& calls);
    void collect_member_expressions(TSNode node, std::string_view source, std::vector<JSVariableRef>& vars);
    void collect_variable_declarations(TSNode node, std::string_view source, std::set<std::string>& vars);
    void collect_identifiers(TSNode node, std::string_view source, std::vector<JSVariableRef>& vars);

    // Single-pass collection
    void collect_all(TSNode node, std::string_view source, JSExtractionResult& result);
};

#endif // JS_AST_BUILDER_HPP
