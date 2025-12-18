#ifndef SEMANTIC_ANALYZER_HPP
#define SEMANTIC_ANALYZER_HPP

#include "rfl_parser_state.hpp"
#include "rfl_rete_defs.hpp"
#include "errors.hpp"   // For StructuredError

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// ===================================================================
// == Symbol Table and Scope Definitions
// ===================================================================

/**
 * @struct SymbolInfo
 * @brief Holds information about a bound variable (symbol) in a rule's scope.
 */
struct SymbolInfo {
    ParsedPattern const* pattern;
    int depth;
    std::optional<std::pair<std::string, std::string>> source_field_of_binding;
};

/**
 * @using SymbolTable
 * @brief Maps a binding name (e.g., "$p") to its SymbolInfo.
 */
using SymbolTable = map<std::string, SymbolInfo>;

// ===================================================================
// == SemanticAnalyzer Class Definition
// ===================================================================

// Forward-declare the recursive helper function. It can now use SymbolTable safely.
class SemanticAnalyzer;
void analyze_constraint_node_recursive(ConstraintNode* node, ParsedPattern const& pattern, int depth,
                                       SymbolTable const& existing_symbols, SymbolTable& new_symbols,
                                       SemanticAnalyzer& analyzer, ParsedRule const& rule);

/**
 * @class SemanticAnalyzer
 * @brief Validates the semantic correctness of a parsed RFL Abstract Syntax Tree (AST).
 */
class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(parser_state& state, std::string const& source_name);

    bool build_and_analyze_declarations();
    bool analyze_rules_and_queries();

    std::vector<StructuredError> const& get_errors() const { return errors_; }

    map<std::string, std::set<std::string>> const& get_type_schemas() const { return type_schemas_; }

    std::optional<std::string> resolve_type(std::string const& type_name, std::string const& package_ctx,
                                            std::vector<std::string> const& imports_ctx);
    void add_error(tao::pegtl::position const& pos, std::string const& message);

private:
    void build_schema();
    void analyze_rule(ParsedRule& rule);
    void analyze_query(ParsedQuery& query);
    void analyze_pattern_list(std::vector<ParsedPattern>& patterns, SymbolTable& symbols, int& depth,
                              ParsedRule const& rule);
    void analyze_pattern(ParsedPattern& pattern, SymbolTable& symbols, ParsedRule const& rule,
                         tao::pegtl::position const& pattern_pos, int depth);
    void analyze_rhs(ParsedRule& rule, SymbolTable const& symbols);

    parser_state& state_;
    std::string source_name_;
    std::vector<StructuredError> errors_;
    map<std::string, std::set<std::string>> type_schemas_;
};

#endif   // SEMANTIC_ANALYZER_HPP


