#pragma once

#include <string_view>
#include <unordered_set>

namespace rulesforge {

/**
 * @brief Checks if a given function name is natively supported by the
 *        underlying Expression Evaluator (ExprTk) engine.
 */
inline bool is_exprtk_builtin(std::string_view name) {
  static const std::unordered_set<std::string_view> builtins = {
      "floor",      "ceil",      "abs",       "min",      "max",         "round", "pow",
      "sqrt",       "log",       "exp",       "sin",      "cos",         "tan",   "fmod",
      "trunc",      "asin",      "acos",      "atan",     "atan2",       "sinh",  "cosh",
      "tanh",       "sgn",       "root",      "erf",      "log2",        "log10", "log1p",
      "expm1",      "hypot",     "concat",    "strlen",   "substr",      "trim",  "replace",
      "indexOf",    "contains",  "now_ms",    "upper",    "lower",       "toUpper",      "toLower",    "to_upper",   "to_lower",  "year",      "month",
      "day",        "to_string", "to_number", "is_null",  "is_not_null", "uuid",  "random",
      "plugin_load"};
  return builtins.count(name) > 0;
}

} // namespace rulesforge


