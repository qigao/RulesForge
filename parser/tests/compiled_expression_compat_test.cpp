#include "compiled_expression.hpp"
#include "tinytest.h"

#include <cmath>

using namespace rulesforge;

namespace {
    bool approx_equal(double a, double b, double epsilon = 1e-9) {
        return std::abs(a - b) < epsilon;
    }

    double as_double(ConstraintValue const& value) {
        if (std::holds_alternative<double>(value)) {
            return std::get<double>(value);
        }
        if (std::holds_alternative<int64_t>(value)) {
            return static_cast<double>(std::get<int64_t>(value));
        }
        return 0.0;
    }
}

suite("CompiledExpressionCompat") {
    it("delegates to expression evaluator") {
        auto expr = CompiledExpression::compile("$x + 2");
        check(expr != nullptr);

        double result = as_double(expr->evaluate([](std::string const& var) {
            if (var == "$x") return 3.0;
            return 0.0;
        }));

        check(approx_equal(result, 5.0));
        check(expr->variables().size() == 1);
        check(expr->variables()[0] == "$x");
    }
}
