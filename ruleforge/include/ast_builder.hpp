#ifndef AST_BUILDER_HPP
#define AST_BUILDER_HPP

#include "rfl_grammar.hpp"
#include "rfl_parser_state.hpp"
#include "rfl_rete_defs.hpp"
#include "errors.hpp"

#include <memory>
#include <tao/pegtl/contrib/parse_tree.hpp>
#include <vector>

enum class NodeType;
struct ConstraintNode;
struct ParsedPattern;
struct ParsedRule;
struct ParsedDeclaration;
struct ParsedQuery;
struct ParsedFunction;
struct ParsedGlobal;

class AstBuilder {
public:
    explicit AstBuilder(std::unique_ptr<pegtl::parse_tree::node> root, std::string const& source_name);
    parser_state build(std::vector<StructuredError>& out_errors);

private:
    ParsedRule build_rule(pegtl::parse_tree::node const& n, std::vector<StructuredError>& out_errors);
    void build_rule_attributes(pegtl::parse_tree::node const& n, ParsedRule& rule);
    void build_rhs(pegtl::parse_tree::node const& n, ParsedRule& rule);
    std::string build_modify_statement(pegtl::parse_tree::node const& n);

    ParsedPattern build_pattern(pegtl::parse_tree::node const& n);
    ParsedPattern build_source_pattern(pegtl::parse_tree::node const& n);
    void build_from_clause(pegtl::parse_tree::node const& n, ParsedPattern& pattern);
    std::unique_ptr<ConstraintNode> build_constraint_expression(pegtl::parse_tree::node const& n);
    std::unique_ptr<ConstraintNode> build_logical_op_node(pegtl::parse_tree::node const& n, NodeType type);
    std::unique_ptr<ConstraintNode> build_constraint_item(pegtl::parse_tree::node const& n);
    ConstraintValue build_literal(pegtl::parse_tree::node const& n);
    std::vector<ConstraintValue> build_value_list(pegtl::parse_tree::node const& n);
    ArithExprValue parse_arith_expr_string(std::string const& expr_str); 
    ParsedDeclaration build_declaration(pegtl::parse_tree::node const& n);
    ParsedQuery build_query(pegtl::parse_tree::node const& n);
    ParsedFunction build_function(pegtl::parse_tree::node const& n);
    ParsedGlobal build_global(pegtl::parse_tree::node const& n);
    void build_lhs(pegtl::parse_tree::node const& n, std::vector<std::vector<ParsedPattern>>& condition_groups);
    void build_import(pegtl::parse_tree::node const& n, parser_state& state);
    void build_annotations(pegtl::parse_tree::node const& n, ParsedRule& rule);
    void normalize_indentation(std::string& code);
    void build_package(pegtl::parse_tree::node const& n, parser_state& state);

    std::unique_ptr<pegtl::parse_tree::node> root_;
    std::string const source_name_;
};

#endif   // AST_BUILDER_HPP


