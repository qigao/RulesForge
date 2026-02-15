#ifndef COMPILED_EXPRESSION_HPP
#define COMPILED_EXPRESSION_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ruleforge {

/**
 * @brief Variable resolver function type.
 * Given a variable name (e.g., "$price", "$order.total"), returns its numeric value.
 */
using VariableResolver = std::function<double(std::string const&)>;

/**
 * @brief Compiled mathematical expression using exprtk.
 *
 * Expressions are compiled once at rule load time, then evaluated many times
 * at runtime with different variable values. Supports all standard math functions
 * (sin, cos, exp, log, pow, sqrt, etc.) plus basic arithmetic.
 *
 * Uses PIMPL to isolate exprtk's heavy header from the rest of the codebase.
 */
class CompiledExpression {
public:
    ~CompiledExpression();
    CompiledExpression(CompiledExpression const&) = delete;
    CompiledExpression& operator=(CompiledExpression const&) = delete;
    CompiledExpression(CompiledExpression&&) noexcept;
    CompiledExpression& operator=(CompiledExpression&&) noexcept;

    /**
     * @brief Compile an expression string.
     * @param expr Expression like "sin($x) + $y * 2" or "$price - $discount"
     * @param error_out If compilation fails, error message is written here
     * @return Compiled expression, or nullptr on failure
     */
    static std::shared_ptr<CompiledExpression> compile(
        std::string const& expr,
        std::string* error_out = nullptr);

    /**
     * @brief Evaluate the expression with given variable resolver.
     * @param resolver Function that maps variable names to their current values
     * @return Result of the expression evaluation
     */
    double evaluate(VariableResolver const& resolver) const;

    /**
     * @brief Get list of variables used in this expression.
     * @return Vector of variable names (e.g., {"$price", "$qty"})
     */
    std::vector<std::string> const& variables() const;

    /**
     * @brief Get the original expression string.
     */
    std::string const& expression_string() const;

private:
    CompiledExpression();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ruleforge

#endif // COMPILED_EXPRESSION_HPP
