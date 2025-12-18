#ifndef JS_SEMANTIC_ANALYZER_HPP
#define JS_SEMANTIC_ANALYZER_HPP

#include "js_ast_builder.hpp"
#include "rfl_rete_defs.hpp"
#include "semantic_analyzer.hpp"
#include <string>
#include <map>

class JSSemanticAnalyzer {
public:
    JSSemanticAnalyzer(SymbolTable const& symbols, ParsedRule const& rule, SemanticAnalyzer& base_analyzer);
    
    // Analyze JavaScript RHS code and update the rule
    bool analyze_js_rhs(ParsedRule& rule);
    
    // Validate JavaScript syntax
    bool validate_syntax(const std::string& js_code, std::string& error_message);
    
    // Extract and validate function calls
    std::vector<JSFunctionCall> analyze_function_calls(const std::string& js_code);
    
    // Extract and validate variable references
    std::vector<JSVariableRef> analyze_variables(const std::string& js_code);

private:
    SymbolTable const& symbols_;
    ParsedRule const& rule_;
    SemanticAnalyzer& analyzer_;
    JSAstBuilder ast_builder_;
    
    // Helper methods
    std::string resolve_and_substitute_types(const std::string& js_code);
    std::string substitute_variables(const std::string& js_code);
    bool validate_variable_bindings(const std::vector<JSVariableRef>& variables, const std::set<std::string>& local_vars);
    bool validate_function_calls(const std::vector<JSFunctionCall>& calls);
};

#endif // JS_SEMANTIC_ANALYZER_HPP

