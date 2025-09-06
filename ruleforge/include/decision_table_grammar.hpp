#ifndef DECISION_TABLE_GRAMMAR_HPP
#define DECISION_TABLE_GRAMMAR_HPP

#include <tao/pegtl.hpp>

namespace dt_grammar {
    namespace pegtl = tao::pegtl;

    // A field is either quoted or unquoted
    struct unquoted_field_char : pegtl::not_one<',', '\r', '\n'> {};

    struct unquoted_field : pegtl::star<unquoted_field_char> {};   // Allow empty fields

    struct quoted_field_content : pegtl::until<pegtl::at<pegtl::one<'"'>>, pegtl::any> {};

    struct quoted_field : pegtl::seq<pegtl::one<'"'>, quoted_field_content, pegtl::must<pegtl::one<'"'>>> {};

    struct field : pegtl::sor<quoted_field, unquoted_field> {};

    // A record is a list of fields separated by commas
    struct record : pegtl::list<field, pegtl::one<','>> {};

    // A file is a list of records separated by newlines, with an optional final newline
    struct line_ending : pegtl::sor<pegtl::string<'\r', '\n'>, pegtl::one<'\n'>> {};

    // Grammar for comments (lines starting with #) and blank lines
    struct comment : pegtl::seq<pegtl::one<'#'>, pegtl::until<pegtl::eolf>> {};

    struct blank_line : pegtl::star<pegtl::blank> {};

    // An ignored line is a comment or a blank line that is not a data record.
    struct ignored_line : pegtl::sor<comment, blank_line> {};

    // A line in the file is either a data record or an ignored line.
    struct line : pegtl::sor<record, ignored_line> {};

    // The file consists of a list of lines.
    // list_tail handles the optional final newline correctly.
    struct file : pegtl::list_tail<line, line_ending> {};

    // The main grammar to be used by the parser. This is the top-level rule.
    // It ensures the entire file content is consumed by matching end-of-file.
    struct main_grammar : pegtl::seq<file, pegtl::eof> {};
}   // namespace dt_grammar

#endif   // DECISION_TABLE_GRAMMAR_HPP