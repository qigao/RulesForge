#ifndef EXPRESSION_EVALUATOR_HPP
#define EXPRESSION_EVALUATOR_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "core/value_types.hpp"

namespace rulesforge {

using VariableResolver = std::function<ConstraintValue(std::string const&)>;

class ExpressionEvaluator {
public:
    static std::shared_ptr<ExpressionEvaluator> compile(
        std::string const& expr,
        std::string* error_out = nullptr);

    ConstraintValue evaluate(VariableResolver const& resolver) const;
    std::vector<std::string> const& variables() const;
    std::string const& expression_string() const;

private:
    struct Impl;
    explicit ExpressionEvaluator(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
};

}  // namespace rulesforge

#endif  // EXPRESSION_EVALUATOR_HPP
