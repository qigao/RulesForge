#ifndef DECISION_TABLE_COMPILER_HPP
#define DECISION_TABLE_COMPILER_HPP

#include "core/errors.hpp"
#include "core/rfl_parser_state.hpp"
#include "decision_table.hpp"

#include <cstddef>
#include <string>
#include <vector>

struct DecisionTableCompileStats {
    std::size_t direct_success = 0;
    std::size_t parser_path_success = 0;
    std::size_t parser_path_unknown_preamble = 0;
    std::size_t parser_path_declare_parse = 0;
    std::size_t parser_path_query_parse = 0;
    std::size_t parser_path_salience_parse = 0;
    std::size_t parser_path_condition_parse = 0;
};

class DirectTableCompiler {
public:
    static parser_state compile(DecisionTable const& table,
                                std::string const& source_name,
                                std::vector<StructuredError>& errors);
    static DecisionTableCompileStats get_stats();
    static void reset_stats();
};

#endif  // DECISION_TABLE_COMPILER_HPP
