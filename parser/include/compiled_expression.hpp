#ifndef COMPILED_EXPRESSION_HPP
#define COMPILED_EXPRESSION_HPP

#include "expression_evaluator.hpp"

namespace rulesforge {

/**
 * @brief Backward-compatible name for ExpressionEvaluator.
 *
 * New code should include/use ExpressionEvaluator directly. This alias keeps
 * older includes and type names working during migration.
 */
using CompiledExpression = ExpressionEvaluator;

} // namespace rulesforge

#endif // COMPILED_EXPRESSION_HPP
