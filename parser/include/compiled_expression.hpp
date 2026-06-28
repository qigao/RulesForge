#ifndef COMPILED_EXPRESSION_HPP
#define COMPILED_EXPRESSION_HPP

#include "expression_descriptor.hpp"

namespace rulesforge {

/**
 * @brief Backward-compatible name for the expression descriptor.
 *
 * Runtime execution is owned by MIR/TurboScript backends. This alias only keeps
 * older includes and type names working during migration.
 */
using CompiledExpression = ExpressionDescriptor;

} // namespace rulesforge

#endif // COMPILED_EXPRESSION_HPP
