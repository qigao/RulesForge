#ifndef EXPRESSION_EVALUATOR_HPP
#define EXPRESSION_EVALUATOR_HPP

#include "expression_descriptor.hpp"

namespace rulesforge {

/**
 * Backward-compatible frontend name.
 *
 * The type no longer evaluates expressions. Runtime execution is owned by the
 * C++/exprtk expression paths; this descriptor only preserves expression text
 * and source variable references for lowering and diagnostics.
 */
using ExpressionEvaluator = ExpressionDescriptor;

}  // namespace rulesforge

#endif  // EXPRESSION_EVALUATOR_HPP
