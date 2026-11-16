#ifndef EXPRESSION_EVALUATOR_HPP
#define EXPRESSION_EVALUATOR_HPP

#include "expression_descriptor.hpp"

namespace rulesforge {

/**
 * Backward-compatible frontend name.
 *
 * The descriptor owns immutable MIR scalar code, original text and compiled
 * variable access paths. Commands and transaction state belong to the engine.
 */
using ExpressionEvaluator = ExpressionDescriptor;

}  // namespace rulesforge

#endif  // EXPRESSION_EVALUATOR_HPP
