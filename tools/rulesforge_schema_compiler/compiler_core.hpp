#pragma once

#include "core/errors.hpp"
#include "core/rfl_parser_state.hpp"

#include <string>

namespace rulesforge_compiler {

enum class ValidationMode {
    Both,
    JsonOnly,
    BinaryOnly
};

enum class OutputLanguage {
    C,
    Cpp,
    Go,
    TypeScript,
    Python
};

bool compile_source(std::string const& source,
                    std::string const& source_name,
                    ParsingResult& result,
                    parser_state& out_state);

bool compile_file(std::string const& file_path,
                  ParsingResult& result,
                  parser_state& out_state);

std::string emit_declarations_only(parser_state const& state);

bool render_with_template_file(parser_state const& state,
                               std::string const& template_path,
                               std::string& out_text,
                               std::string& out_error);

std::string builtin_template_name(OutputLanguage language);

bool validate_databind_support(parser_state const& state,
                               ValidationMode mode,
                               std::string& out_error);

} // namespace rulesforge_compiler
