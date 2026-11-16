#pragma once

#include <string_view>

namespace rulesforge {

/**
 * @brief Recognizes reserved RHS function names. Execution is admitted by the
 * native scalar/command plan; complex application computation uses typed CALL.
 */
bool is_rhs_builtin(std::string_view name);

} // namespace rulesforge
