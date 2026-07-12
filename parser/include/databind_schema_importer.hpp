#pragma once

#include "core/errors.hpp"
#include "core/rfl_parser_state.hpp"

#include <string>
#include <vector>

namespace rulesforge {

bool import_databind_schemas(parser_state& state,
                             std::vector<std::string> const& base_dirs,
                             std::vector<StructuredError>& errors,
                             std::string const& default_source_name);

} // namespace rulesforge
