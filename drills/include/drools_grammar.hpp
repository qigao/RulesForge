#ifndef DROOLS_GRAMMAR_HPP
#define DROOLS_GRAMMAR_HPP

#include <tao/pegtl.hpp>

namespace pegtl = tao::pegtl;

namespace grammar {
    // ===================================================================
    // == 1. Primitives, Whitespace, and Comments
    // ===================================================================
    struct cpp_line_comment : pegtl::seq<pegtl::string<'/', '/'>, pegtl::until<pegtl::eolf>> {};

    struct sql_line_comment : pegtl::seq<pegtl::string<'-', '-'>, pegtl::until<pegtl::eolf>> {};

    struct hash_line_comment : pegtl::seq<pegtl::one<'#'>, pegtl::until<pegtl::eolf>> {};

    struct block_comment : pegtl::seq<pegtl::string<'/', '*'>, pegtl::until<pegtl::string<'*', '/'>>> {};

    struct comment : pegtl::sor<cpp_line_comment, sql_line_comment, hash_line_comment, block_comment> {};

    struct ignored : pegtl::sor<pegtl::space, comment> {};

    struct whitespace : pegtl::plus<ignored> {};

    struct opt_whitespace : pegtl::star<ignored> {};

    struct expression;
    struct pattern;   // Forward-declare for recursion

    // ===================================================================
    // == 2. Keywords
    // ===================================================================
    struct identifier_chars : pegtl::sor<pegtl::alnum, pegtl::one<'_'>> {};

    template <char... S>
    struct keyword : pegtl::seq<pegtl::string<S...>, pegtl::not_at<identifier_chars>> {};

    struct keyword_rule : keyword<'r', 'u', 'l', 'e'> {};

    struct keyword_when : keyword<'w', 'h', 'e', 'n'> {};

    struct keyword_then : keyword<'t', 'h', 'e', 'n'> {};

    struct keyword_end : keyword<'e', 'n', 'd'> {};

    struct keyword_salience : keyword<'s', 'a', 'l', 'i', 'e', 'n', 'c', 'e'> {};

    struct keyword_extends : keyword<'e', 'x', 't', 'e', 'n', 'd', 's'> {};

    struct keyword_agenda_group : keyword<'a', 'g', 'e', 'n', 'd', 'a', '-', 'g', 'r', 'o', 'u', 'p'> {};

    struct keyword_timer : keyword<'t', 'i', 'm', 'e', 'r'> {};

    struct keyword_from : keyword<'f', 'r', 'o', 'm'> {};

    struct keyword_not : keyword<'n', 'o', 't'> {};

    struct keyword_exists : keyword<'e', 'x', 'i', 's', 't', 's'> {};

    struct keyword_collect : keyword<'c', 'o', 'l', 'l', 'e', 'c', 't'> {};

    struct keyword_accumulate : keyword<'a', 'c', 'c', 'u', 'm', 'u', 'l', 'a', 't', 'e'> {};

    struct keyword_forall : keyword<'f', 'o', 'r', 'a', 'l', 'l'> {};

    struct keyword_eval : keyword<'e', 'v', 'a', 'l'> {};

    struct keyword_entry_point : keyword<'e', 'n', 't', 'r', 'y', '-', 'p', 'o', 'i', 'n', 't'> {};

    struct keyword_in : keyword<'i', 'n'> {};

    struct keyword_function : keyword<'f', 'u', 'n', 'c', 't', 'i', 'o', 'n'> {};

    struct keyword_declare : keyword<'d', 'e', 'c', 'l', 'a', 'r', 'e'> {};

    struct keyword_query : keyword<'q', 'u', 'e', 'r', 'y'> {};

    struct keyword_global : keyword<'g', 'l', 'o', 'b', 'a', 'l'> {};

    struct keyword_package : keyword<'p', 'a', 'c', 'k', 'a', 'g', 'e'> {};

    struct keyword_import : keyword<'i', 'm', 'p', 'o', 'r', 't'> {};

    struct keyword_or : keyword<'o', 'r'> {};

    struct keyword_modify : keyword<'m', 'o', 'd', 'i', 'f', 'y'> {};

    struct keyword_nil : keyword<'n', 'i', 'l'> {};

    struct keyword_true : keyword<'t', 'r', 'u', 'e'> {};

    struct keyword_false : keyword<'f', 'a', 'l', 's', 'e'> {};

    struct keyword_this : keyword<'t', 'h', 'i', 's'> {};

    struct keyword_unnest : keyword<'u', 'n', 'n', 'e', 's', 't'> {};

    struct any_keyword :
        pegtl::sor<keyword_rule, keyword_when, keyword_then, keyword_end, keyword_salience, keyword_extends,
                   keyword_agenda_group, keyword_timer, keyword_from, keyword_not, keyword_exists, keyword_collect,
                   keyword_accumulate, keyword_forall, keyword_eval, keyword_entry_point, keyword_in, keyword_function,
                   keyword_declare, keyword_query, keyword_global, keyword_package, keyword_import, keyword_or,
                   keyword_modify, keyword_nil, keyword_true, keyword_false, keyword_this> {};

    // ===================================================================
    // == 3. Identifiers and Literals
    // ===================================================================
    struct raw_identifier : pegtl::seq<pegtl::alpha, pegtl::star<identifier_chars>> {};

    struct name_part : pegtl::seq<pegtl::not_at<any_keyword>, raw_identifier> {};

    struct qualified_name : pegtl::list<name_part, pegtl::one<'.'>> {};

    struct variable_binding : pegtl::seq<pegtl::one<'$'>, raw_identifier> {};

    struct integer : pegtl::seq<pegtl::opt<pegtl::one<'-'>>, pegtl::plus<pegtl::digit>> {};

    struct double_ :
        pegtl::seq<pegtl::opt<pegtl::one<'-'>>, pegtl::plus<pegtl::digit>, pegtl::one<'.'>, pegtl::plus<pegtl::digit>> {
    };

    struct string_literal : pegtl::seq<pegtl::one<'\"'>, pegtl::star<pegtl::not_one<'\"'>>, pegtl::one<'\"'>> {};

    // ===================================================================
    // == 4. Expressions and Constraints
    // ===================================================================
    // Use ':=' for inline binding
    struct binding : pegtl::seq<variable_binding, opt_whitespace, pegtl::string<':', '='>> {};

    struct simple_name_part : pegtl::seq<pegtl::not_at<any_keyword>, raw_identifier> {};

    struct constraint_field : pegtl::list<pegtl::sor<variable_binding, simple_name_part>, pegtl::one<'.'>> {};

    struct primary_expr :
        pegtl::sor<double_, integer, string_literal, keyword_true, keyword_false, keyword_nil, constraint_field,
                   variable_binding,
                   pegtl::seq<pegtl::one<'('>, opt_whitespace, expression, opt_whitespace, pegtl::one<')'>>> {};

    struct not_in_op : pegtl::seq<keyword_not, whitespace, keyword_in> {};

    struct in_op : pegtl::sor<not_in_op, keyword_in> {};

    struct value_list_content : pegtl::list<primary_expr, pegtl::one<','>, ignored> {};

    struct value_list :
        pegtl::seq<pegtl::one<'('>, opt_whitespace, value_list_content, opt_whitespace, pegtl::one<')'>> {};

    struct cmp_op :
        pegtl::sor<pegtl::string<'<', '='>, pegtl::string<'>', '='>, pegtl::string<'!', '='>, pegtl::string<'=', '='>,
                   pegtl::one<'<'>, pegtl::one<'>'>> {};

    struct cmp_clause : pegtl::seq<pegtl::pad<cmp_op, ignored>, primary_expr> {};

    struct in_clause : pegtl::seq<pegtl::pad<in_op, ignored>, value_list> {};

    // Rule for an inline binding, e.g., "$v :"
    struct inline_binding : pegtl::seq<variable_binding, opt_whitespace, pegtl::one<':'>, opt_whitespace> {};

    // `relational_expression` now includes an optional binding and an optional comparison.
    // This single rule covers: `field`, `field > 10`, `$v : field`, and `$v : field > 10`.
    struct relational_expression : pegtl::seq<pegtl::opt<inline_binding>, primary_expr, pegtl::opt<cmp_clause>> {};

    // New: Grammar for a time duration literal (e.g., "300ms", "5m", "1h")
    struct duration_literal :
        pegtl::seq<pegtl::plus<pegtl::digit>,
                   pegtl::sor<pegtl::string<'m', 's'>, pegtl::one<'s'>, pegtl::one<'m'>, pegtl::one<'h'>>> {};

    // Temporal operators
    struct op_after : keyword<'a', 'f', 't', 'e', 'r'> {};

    struct op_before : keyword<'b', 'e', 'f', 'o', 'r', 'e'> {};

    struct op_within : keyword<'w', 'i', 't', 'h', 'i', 'n'> {};

    struct op_of : keyword<'o', 'f'> {};

    // Matches: `timestamp after $e1.timestamp`
    struct temporal_seq_clause :
        pegtl::seq<primary_expr,                                           // The subject (e.g., 'timestamp')
                   pegtl::pad<pegtl::sor<op_after, op_before>, ignored>,   // The operator
                   primary_expr                                            // The object (e.g., '$e1.timestamp')
                   > {};

    // Matches: `within 60s of $e1`
    struct temporal_window_clause :
        pegtl::seq<op_within, whitespace, duration_literal, whitespace, op_of, whitespace,
                   variable_binding   // The anchor point for the window (e.g., '$e1')
                   > {};

    // The main constraint rule must now prioritize these new, more specific rules.
    struct constraint_item :
        pegtl::sor<temporal_window_clause, temporal_seq_clause,
                   relational_expression   // The general-purpose comparison rule
                   > {};

    struct and_expr :
        pegtl::list<constraint_item, pegtl::pad<pegtl::sor<pegtl::string<'&', '&'>, pegtl::one<','>>, ignored>> {};

    struct or_expr : pegtl::list<and_expr, pegtl::pad<pegtl::string<'|', '|'>, ignored>> {};

    struct expression : or_expr {};

    // ===================================================================
    // == 5. Patterns (LHS)
    // ===================================================================
    struct from_clause;

    struct fact_type_name : qualified_name {};

    struct pattern_constraints :
        pegtl::seq<pegtl::one<'('>, opt_whitespace, pegtl::opt<expression>, opt_whitespace, pegtl::one<')'>> {};

    struct standard_pattern_body :
        pegtl::seq<fact_type_name, opt_whitespace, pegtl::opt<pattern_constraints>, opt_whitespace,
                   pegtl::opt<from_clause>> {};

    struct not_pattern_body :
        pegtl::seq<keyword_not, opt_whitespace, pegtl::one<'('>, opt_whitespace, pattern, opt_whitespace,
                   pegtl::one<')'>> {};

    struct exists_pattern_body :
        pegtl::seq<keyword_exists, opt_whitespace, pegtl::one<'('>, opt_whitespace, pattern, opt_whitespace,
                   pegtl::one<')'>> {};
    struct balanced_parens;

    struct balanced_content :
        pegtl::star<pegtl::sor<pegtl::seq<pegtl::one<'\"'>, pegtl::until<pegtl::one<'\"'>>>, balanced_parens,
                               pegtl::not_one<')'>>> {};

    struct balanced_parens : pegtl::seq<pegtl::one<'('>, balanced_content, pegtl::one<')'>> {};

    struct eval_expression : balanced_content {};

    struct eval_pattern_body :
        pegtl::seq<keyword_eval, opt_whitespace, pegtl::one<'('>, opt_whitespace, eval_expression, opt_whitespace,
                   pegtl::one<')'>> {};

    struct forall_pattern_list : pegtl::list<pattern, pegtl::one<','>, ignored> {};

    struct forall_pattern_body :
        pegtl::seq<keyword_forall, opt_whitespace, pegtl::one<'('>, opt_whitespace, pegtl::opt<forall_pattern_list>,
                   opt_whitespace, pegtl::one<')'>> {};

    struct query_call_name : string_literal {};

    struct query_call_arg : variable_binding {};

    struct query_call_arg_list_content :
        pegtl::list<query_call_arg, pegtl::seq<opt_whitespace, pegtl::one<','>, opt_whitespace>> {};

    struct query_call_args :
        pegtl::seq<pegtl::opt<query_call_arg_list_content>, opt_whitespace, pegtl::opt<pegtl::one<';'>>,
                   opt_whitespace> {};

    struct query_call_pattern_body :
        pegtl::seq<query_call_name, opt_whitespace, pegtl::one<'('>, opt_whitespace, query_call_args, opt_whitespace,
                   pegtl::one<')'>> {};

    struct pattern :
        pegtl::seq<pegtl::opt<pegtl::seq<variable_binding, opt_whitespace, pegtl::one<':'>, opt_whitespace>>,
                   pegtl::sor<not_pattern_body, exists_pattern_body, forall_pattern_body, eval_pattern_body,
                              query_call_pattern_body, standard_pattern_body>> {};

    struct accumulate_function_name : raw_identifier {};

    // A pattern that is the source for an accumulate/collect operation.
    // It must not contain a 'from' clause itself, to prevent left-recursion in the grammar.
    struct standard_pattern_body_no_from :
        pegtl::seq<fact_type_name, opt_whitespace, pegtl::opt<pattern_constraints>> {};

    struct accumulate_source_pattern :
        pegtl::seq<pegtl::opt<pegtl::seq<variable_binding, opt_whitespace, pegtl::one<':'>, opt_whitespace>>,
                   standard_pattern_body_no_from> {};

    // This now accepts `$p.value` as well as `$v`.
    struct accumulate_source_ref : constraint_field {};

    struct accumulate_function :
        pegtl::seq<accumulate_function_name, opt_whitespace, pegtl::one<'('>, opt_whitespace,
                   pegtl::opt<accumulate_source_ref>, opt_whitespace, pegtl::one<')'>> {};

    struct unnest_source : constraint_field {};

    struct from_unnest_clause :
        pegtl::seq<keyword_from, whitespace, keyword_unnest, opt_whitespace, pegtl::one<'('>, opt_whitespace,
                   unnest_source, opt_whitespace, pegtl::one<')'>> {};

    struct entry_point_name : string_literal {};

    struct from_entry_point_clause :
        pegtl::seq<keyword_from, whitespace, keyword_entry_point, whitespace, entry_point_name> {};

    struct from_collect_clause :
        pegtl::seq<keyword_from, whitespace, keyword_collect, opt_whitespace, pegtl::one<'('>, opt_whitespace, pattern,
                   opt_whitespace, pegtl::one<')'>> {};

    struct from_accumulate_clause :
        pegtl::seq<keyword_from, whitespace, keyword_accumulate, opt_whitespace, pegtl::one<'('>, opt_whitespace,
                   accumulate_source_pattern, pegtl::one<','>, opt_whitespace, accumulate_function, opt_whitespace,
                   pegtl::one<')'>> {};

    struct from_clause :
        pegtl::sor<from_accumulate_clause, from_collect_clause, from_unnest_clause, from_entry_point_clause> {};

    // ===================================================================
    // == 6. RHS (Consequence)
    // ===================================================================
    struct modify_target : variable_binding {};

    struct modify_setter_name : raw_identifier {};

    struct modify_setter_args : balanced_parens {};

    struct modify_setter :
        pegtl::seq<modify_setter_name, opt_whitespace, modify_setter_args, opt_whitespace,
                   pegtl::opt<pegtl::one<';'>>> {};

    struct modify_body :
        pegtl::seq<pegtl::one<'{'>, opt_whitespace, pegtl::star<modify_setter>, opt_whitespace, pegtl::one<'}'>> {};

    struct modify_statement :
        pegtl::seq<keyword_modify, opt_whitespace, pegtl::one<'('>, opt_whitespace, modify_target, opt_whitespace,
                   pegtl::one<')'>, opt_whitespace, modify_body> {};

    struct rhs_terminator : pegtl::at<keyword_end> {};

    struct code_chunk :
        pegtl::plus<pegtl::seq<pegtl::not_at<pegtl::sor<modify_statement, rhs_terminator>>, pegtl::any>> {};

    struct rhs_element : pegtl::sor<modify_statement, code_chunk> {};

    struct rhs : pegtl::star<rhs_element> {};

    // ===================================================================
    // == 7. Top-Level Statements
    // ===================================================================
    struct lhs_or_op : pegtl::pad<keyword_or, whitespace> {};

    struct lhs_terminator : pegtl::sor<keyword_then, keyword_end, lhs_or_op> {};

    struct single_and_block : pegtl::plus<pegtl::seq<pegtl::not_at<lhs_terminator>, pattern, opt_whitespace>> {};

    struct lhs_and_block :
        pegtl::sor<pegtl::seq<pegtl::one<'('>, opt_whitespace, single_and_block, opt_whitespace, pegtl::one<')'>>,
                   single_and_block> {};

    struct lhs : pegtl::list<lhs_and_block, lhs_or_op> {};

    struct annotation_name : raw_identifier {};

    struct annotation_value : balanced_parens {};

    struct annotation :
        pegtl::seq<pegtl::one<'@'>, annotation_name, pegtl::opt<pegtl::seq<opt_whitespace, annotation_value>>> {};

    struct annotation_list : pegtl::plus<pegtl::seq<annotation, opt_whitespace>> {};

    struct rule_name : string_literal {};

    struct salience_value : integer {};

    struct salience_attribute : pegtl::seq<keyword_salience, whitespace, salience_value> {};

    struct parent_rule_name : string_literal {};

    struct extends_clause : pegtl::seq<keyword_extends, whitespace, parent_rule_name> {};

    struct agenda_group_name : string_literal {};

    struct agenda_group_attribute : pegtl::seq<keyword_agenda_group, whitespace, agenda_group_name> {};

    struct timer_value : integer {};

    struct timer_attribute :
        pegtl::seq<keyword_timer, whitespace, timer_value,
                   pegtl::opt<pegtl::seq<opt_whitespace, pegtl::one<','>, opt_whitespace, timer_value>>> {};

    struct attribute : pegtl::sor<salience_attribute, agenda_group_attribute, extends_clause, timer_attribute> {};

    struct attributes : pegtl::plus<pegtl::seq<attribute, opt_whitespace>> {};

    struct when_block : pegtl::seq<keyword_when, opt_whitespace, pegtl::opt<lhs>> {};

    struct then_block : pegtl::seq<keyword_then, opt_whitespace, rhs, opt_whitespace, keyword_end> {};

    struct rule :
        pegtl::seq<pegtl::opt<annotation_list>, keyword_rule, whitespace, rule_name, opt_whitespace,
                   pegtl::opt<attributes>, when_block, opt_whitespace, then_block> {};

    struct package_name : qualified_name {};

    struct package_statement :
        pegtl::seq<keyword_package, whitespace, package_name, opt_whitespace, pegtl::opt<pegtl::one<';'>>> {};

    struct import_name : pegtl::seq<qualified_name, pegtl::opt<pegtl::string<'.', '*'>>> {};

    struct import_statement :
        pegtl::seq<keyword_import, whitespace, import_name, opt_whitespace, pegtl::opt<pegtl::one<';'>>> {};

    struct function_signature_part : raw_identifier {};

    struct function_signature : pegtl::list<function_signature_part, whitespace> {};

    struct function_params_content : pegtl::until<pegtl::at<pegtl::one<')'>>> {};

    struct function_params : pegtl::seq<pegtl::one<'('>, function_params_content, pegtl::one<')'>> {};

    struct function_body_internals : pegtl::until<pegtl::at<pegtl::one<'}'>>> {};

    struct function_body :
        pegtl::seq<pegtl::one<'{'>, opt_whitespace, function_body_internals, opt_whitespace, pegtl::one<'}'>> {};

    struct function_statement :
        pegtl::seq<keyword_function, whitespace, function_signature, opt_whitespace, function_params, opt_whitespace,
                   function_body> {};

    struct declared_type_name : name_part {};

    struct field_name : raw_identifier {};

    struct field_type : pegtl::sor<qualified_name, raw_identifier> {};

    struct field_definition : pegtl::seq<field_name, opt_whitespace, pegtl::one<':'>, opt_whitespace, field_type> {};

    struct declaration_body :
        pegtl::star<pegtl::seq<field_definition, opt_whitespace, pegtl::opt<pegtl::one<','>>, opt_whitespace>> {};

    struct declaration_statement :
        pegtl::seq<keyword_declare, whitespace, declared_type_name, opt_whitespace,
                   pegtl::opt<pegtl::seq<annotation_list, opt_whitespace>>, declaration_body, opt_whitespace,
                   keyword_end> {};

    struct query_name : pegtl::sor<string_literal, raw_identifier> {};

    struct query_parameter : pegtl::seq<fact_type_name, whitespace, variable_binding> {};

    struct query_parameter_list :
        pegtl::seq<
            pegtl::one<'('>, opt_whitespace,
            pegtl::opt<pegtl::list<query_parameter, pegtl::seq<opt_whitespace, pegtl::one<','>, opt_whitespace>>>,
            opt_whitespace, pegtl::one<')'>> {};

    struct query_statement :
        pegtl::seq<keyword_query, whitespace, query_name, opt_whitespace, pegtl::opt<query_parameter_list>,
                   opt_whitespace, pegtl::opt<lhs>, keyword_end> {};

    struct global_type : pegtl::sor<qualified_name, raw_identifier> {};

    struct global_name : raw_identifier {};

    struct global_statement :
        pegtl::seq<keyword_global, whitespace, global_type, whitespace, global_name, opt_whitespace,
                   pegtl::opt<pegtl::one<';'>>> {};

    struct statement :
        pegtl::sor<package_statement, import_statement, global_statement, declaration_statement, query_statement,
                   function_statement, rule> {};

    struct main : pegtl::seq<opt_whitespace, pegtl::star<pegtl::seq<statement, opt_whitespace>>, pegtl::eof> {};

}   // namespace grammar
#endif
