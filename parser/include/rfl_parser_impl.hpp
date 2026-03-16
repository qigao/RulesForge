#ifndef RFL_PARSER_IMPL_HPP
#define RFL_PARSER_IMPL_HPP

#include "core/errors.hpp"
#include "core/rfl_parser_state.hpp"

#include <map>
#include <string>
#include <vector>

// Context passed into Lemon reduction actions via %extra_context
struct RflParserContext {
  parser_state state;
  std::vector<StructuredError> errors;
  std::string source_name;

  // Temporaries used during rule construction
  ParsedRule current_rule;
  std::vector<std::vector<ParsedPattern>> current_condition_groups;
  std::vector<ParsedPattern> current_and_block;

  // Temporaries for pattern construction
  ParsedPattern current_pattern;

  // Temporaries for declaration construction
  ParsedDeclaration current_decl;

  // Temporaries for enum construction
  ParsedEnum current_enum;

  // Temporaries for query construction
  ParsedQuery current_query;

  // Pending annotations — moved to current_rule or current_decl on reduction
  std::map<std::string, std::string> pending_annotations;

  // Set by %parse_failure — signals driver to stop feeding tokens
  bool failed = false;

  void add_error(std::size_t line, std::size_t col, std::string const &msg) {
    errors.push_back({.file_name = source_name, .line = line, .column = col, .message = msg});
  }
};

// Main entry point: lex + parse → parser_state
parser_state rfl_parse_lemon(std::string_view input, std::string const &source_name,
                             std::vector<StructuredError> &out_errors);

#endif // RFL_PARSER_IMPL_HPP
