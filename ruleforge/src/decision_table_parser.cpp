#include "decision_table_parser.hpp"

#include "decision_table_grammar.hpp"

#include <set>
#include <tao/pegtl/contrib/unescape.hpp>
#include <tao/pegtl/file_input.hpp>
#include <tao/pegtl/string_input.hpp>
#include <tao/pegtl/parse.hpp>

namespace {
    namespace pegtl = tao::pegtl;

    // State for the action-based parser
    struct ParserState {
        DecisionTable table;
        std::vector<std::string> current_record;
        bool header_parsed = false;
    };

    // Actions to populate the DecisionTable struct
    template <typename Rule>
    struct action : pegtl::nothing<Rule> {};

    template <>
    struct action<dt_grammar::quoted_field_content> {
        template <typename ActionInput>
        static void apply(ActionInput const& in, ParserState& state) {
            state.current_record.push_back(in.string());
        }
    };

    template <>
    struct action<dt_grammar::unquoted_field> {
        template <typename ActionInput>
        static void apply(ActionInput const& in, ParserState& state) {
            state.current_record.push_back(in.string());
        }
    };

    // This is the commit point, called after every successful record parse.
    template <>
    struct action<dt_grammar::record> {
        template <typename ActionInput>
        static void apply(ActionInput const& in, ParserState& state) {
            if (state.current_record.empty()) { return; }

            // Handle blank lines that get parsed as a record with one empty field.
            if (state.current_record.size() == 1 && state.current_record[0].empty()) {
                state.current_record.clear();
                return;
            }

            std::string const& first_col = state.current_record[0];
            std::set<std::string> const preamble_keywords = {"PACKAGE", "IMPORT", "DECLARE", "QUERY"};

            if (!state.header_parsed && preamble_keywords.count(first_col)) {
                // This is a preamble record, occurring before the main header
                state.table.preamble_records.push_back(std::move(state.current_record));
            } else if (!state.header_parsed) {
                // This is the first non-preamble line, so it must be the header.
                state.table.headers = std::move(state.current_record);
                state.header_parsed = true;
            } else {
                // The header has been parsed, so this is a data record.
                state.table.data.push_back(std::move(state.current_record));
            }
            state.current_record.clear();
        }
    };
}   // namespace

DecisionTable DecisionTableParser::parse(std::string const& file_path, ParsingResult& result) {
    ParserState state;
    try {
        pegtl::file_input<> in(file_path);
        pegtl::parse<pegtl::must<dt_grammar::main_grammar>, action>(in, state);
    } catch (pegtl::parse_error const& e) {
        result.success = false;
        auto const p = e.positions().front();
        result.errors.push_back({file_path, p.line, p.column, e.what()});
    }
    return state.table;
}

DecisionTable DecisionTableParser::parse_string(std::string const& csv_content, std::string const& source_name, ParsingResult& result) {
    ParserState state;
    try {
        pegtl::string_input<> in(csv_content, source_name);
        pegtl::parse<pegtl::must<dt_grammar::main_grammar>, action>(in, state);
    } catch (pegtl::parse_error const& e) {
        result.success = false;
        auto const p = e.positions().front();
        result.errors.push_back({source_name, p.line, p.column, e.what()});
    }
    return state.table;
}