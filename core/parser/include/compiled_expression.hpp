#ifndef COMPILED_EXPRESSION_HPP
#define COMPILED_EXPRESSION_HPP

#include "expression_descriptor.hpp"

namespace rulesforge {

/**
 * @brief Backward-compatible name for the expression descriptor.
 *
 * The descriptor now owns the MIR scalar program. This alias preserves older
 * internal includes and names without introducing another execution backend.
 */
using CompiledExpression = ExpressionDescriptor;

} // namespace rulesforge

#endif // COMPILED_EXPRESSION_HPP
