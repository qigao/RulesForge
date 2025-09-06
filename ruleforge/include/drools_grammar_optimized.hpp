#ifndef DROOLS_GRAMMAR_OPTIMIZED_HPP
#define DROOLS_GRAMMAR_OPTIMIZED_HPP

#include <tao/pegtl.hpp>

namespace pegtl = tao::pegtl;

namespace grammar {
    // ===================================================================
    // == X-Macro Pattern for Keywords - Eliminate Repetition
    // ===================================================================
    
    // Define all keywords in one place
    #define DROOLS_KEYWORDS(X) \
        X(rule) \
        X(when) \
        X(then) \
        X(end) \
        X(salience) \
        X(extends) \
        X(timer) \
        X(from) \
        X(not) \
        X(exists) \
        X(collect) \
        X(accumulate) \
        X(forall) \
        X(eval) \
        X(in) \
        X(function) \
        X(declare) \
        X(query) \
        X(global) \
        X(package) \
        X(import) \
        X(or) \
        X(modify) \
        X(nil) \
        X(true) \
        X(false) \
        X(this) \
        X(unnest)
    
    // Define hyphenated keywords separately
    #define DROOLS_HYPHENATED_KEYWORDS(X) \
        X(agenda_group, 'a', 'g', 'e', 'n', 'd', 'a', '-', 'g', 'r', 'o', 'u', 'p') \
        X(entry_point, 'e', 'n', 't', 'r', 'y', '-', 'p', 'o', 'i', 'n', 't')
    
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
    
    // Forward declarations
    struct expression;
    struct pattern;
    
    // ===================================================================
    // == 2. Keywords - Generated via X-Macros
    // ===================================================================
    struct identifier_chars : pegtl::sor<pegtl::alnum, pegtl::one<'_'>> {};
    
    // Template for keyword definition
    template <char... S>
    struct keyword : pegtl::seq<pegtl::string<S...>, pegtl::not_at<identifier_chars>> {};
    
    // Helper to convert keyword name to chars
    #define CHAR_SEQ_1(c) c
    #define CHAR_SEQ_2(c1, c2) c1, c2
    #define CHAR_SEQ_3(c1, c2, c3) c1, c2, c3
    #define CHAR_SEQ_4(c1, c2, c3, c4) c1, c2, c3, c4
    #define CHAR_SEQ_5(c1, c2, c3, c4, c5) c1, c2, c3, c4, c5
    #define CHAR_SEQ_6(c1, c2, c3, c4, c5, c6) c1, c2, c3, c4, c5, c6
    #define CHAR_SEQ_7(c1, c2, c3, c4, c5, c6, c7) c1, c2, c3, c4, c5, c6, c7
    #define CHAR_SEQ_8(c1, c2, c3, c4, c5, c6, c7, c8) c1, c2, c3, c4, c5, c6, c7, c8
    #define CHAR_SEQ_9(c1, c2, c3, c4, c5, c6, c7, c8, c9) c1, c2, c3, c4, c5, c6, c7, c8, c9
    #define CHAR_SEQ_10(c1, c2, c3, c4, c5, c6, c7, c8, c9, c10) c1, c2, c3, c4, c5, c6, c7, c8, c9, c10
    
    // Generate keyword structs
    #define DEFINE_KEYWORD(name) \
        struct keyword_##name : keyword<CHAR_SEQ_##name> {};
    
    // Manual character sequences for each keyword
    #define CHAR_SEQ_rule 'r', 'u', 'l', 'e'
    #define CHAR_SEQ_when 'w', 'h', 'e', 'n'
    #define CHAR_SEQ_then 't', 'h', 'e', 'n'
    #define CHAR_SEQ_end 'e', 'n', 'd'
    #define CHAR_SEQ_salience 's', 'a', 'l', 'i', 'e', 'n', 'c', 'e'
    #define CHAR_SEQ_extends 'e', 'x', 't', 'e', 'n', 'd', 's'
    #define CHAR_SEQ_timer 't', 'i', 'm', 'e', 'r'
    #define CHAR_SEQ_from 'f', 'r', 'o', 'm'
    #define CHAR_SEQ_not 'n', 'o', 't'
    #define CHAR_SEQ_exists 'e', 'x', 'i', 's', 't', 's'
    #define CHAR_SEQ_collect 'c', 'o', 'l', 'l', 'e', 'c', 't'
    #define CHAR_SEQ_accumulate 'a', 'c', 'c', 'u', 'm', 'u', 'l', 'a', 't', 'e'
    #define CHAR_SEQ_forall 'f', 'o', 'r', 'a', 'l', 'l'
    #define CHAR_SEQ_eval 'e', 'v', 'a', 'l'
    #define CHAR_SEQ_in 'i', 'n'
    #define CHAR_SEQ_function 'f', 'u', 'n', 'c', 't', 'i', 'o', 'n'
    #define CHAR_SEQ_declare 'd', 'e', 'c', 'l', 'a', 'r', 'e'
    #define CHAR_SEQ_query 'q', 'u', 'e', 'r', 'y'
    #define CHAR_SEQ_global 'g', 'l', 'o', 'b', 'a', 'l'
    #define CHAR_SEQ_package 'p', 'a', 'c', 'k', 'a', 'g', 'e'
    #define CHAR_SEQ_import 'i', 'm', 'p', 'o', 'r', 't'
    #define CHAR_SEQ_or 'o', 'r'
    #define CHAR_SEQ_modify 'm', 'o', 'd', 'i', 'f', 'y'
    #define CHAR_SEQ_nil 'n', 'i', 'l'
    #define CHAR_SEQ_true 't', 'r', 'u', 'e'
    #define CHAR_SEQ_false 'f', 'a', 'l', 's', 'e'
    #define CHAR_SEQ_this 't', 'h', 'i', 's'
    #define CHAR_SEQ_unnest 'u', 'n', 'n', 'e', 's', 't'
    
    // Generate all keyword structs
    DROOLS_KEYWORDS(DEFINE_KEYWORD)
    
    // Hyphenated keywords
    #define DEFINE_HYPHENATED_KEYWORD(name, ...) \
        struct keyword_##name : keyword<__VA_ARGS__> {};
    
    DROOLS_HYPHENATED_KEYWORDS(DEFINE_HYPHENATED_KEYWORD)
    
    // Combined any_keyword using generated structs
    struct any_keyword : pegtl::sor<
        #define KEYWORD_SOR(name) keyword_##name,
        DROOLS_KEYWORDS(KEYWORD_SOR)
        keyword_agenda_group,
        keyword_entry_point
        #undef KEYWORD_SOR
    > {};
    
    // Clean up macros
    #undef DEFINE_KEYWORD
    #undef DEFINE_HYPHENATED_KEYWORD
    
    // ===================================================================
    // == 3. Identifiers and Literals
    // ===================================================================
    struct raw_identifier : pegtl::seq<pegtl::alpha, pegtl::star<identifier_chars>> {};
    struct name_part : pegtl::seq<pegtl::not_at<any_keyword>, raw_identifier> {};
    struct qualified_name : pegtl::list<name_part, pegtl::one<'.'>> {};
    struct variable_binding : pegtl::seq<pegtl::one<'$'>, raw_identifier> {};
    struct integer : pegtl::seq<pegtl::opt<pegtl::one<'-'>>, pegtl::plus<pegtl::digit>> {};
    struct double_ : pegtl::seq<pegtl::opt<pegtl::one<'-'>>, pegtl::plus<pegtl::digit>, pegtl::one<'.'>, pegtl::plus<pegtl::digit>> {};
    struct string_literal : pegtl::seq<pegtl::one<'\"'>, pegtl::star<pegtl::not_one<'\"'>>, pegtl::one<'\"'>> {};
    
    // Rest of the grammar would continue here...
    // This shows the pattern for eliminating repetition
}

#endif // DROOLS_GRAMMAR_OPTIMIZED_HPP