#include "parser/expression_evaluator.hpp"
#include "tinytest.h"

#include <cmath>

using namespace rulesforge;

namespace {
    bool approx_equal(double a, double b, double epsilon = 1e-9) {
        return std::abs(a - b) < epsilon;
    }
}

suite("ExpressionEvaluator") {

    group("Basic arithmetic") {
        it("evaluates simple addition") {
            auto expr = ExpressionEvaluator::compile("1 + 2");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const&) { return 0.0; });
            check(approx_equal(result, 3.0));
        }

        it("evaluates all basic operators") {
            auto expr = ExpressionEvaluator::compile("10 + 5 - 3 * 2 / 2");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const&) { return 0.0; });
            // 10 + 5 - (3 * 2 / 2) = 10 + 5 - 3 = 12
            check(approx_equal(result, 12.0));
        }

        it("respects parentheses") {
            auto expr = ExpressionEvaluator::compile("(10 + 5) * 2");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const&) { return 0.0; });
            check(approx_equal(result, 30.0));
        }
    }

    group("Variables") {
        it("handles single variable") {
            auto expr = ExpressionEvaluator::compile("$x * 2");
            check(expr != nullptr);
            check(expr->variables().size() == 1);
            check(expr->variables()[0] == "$x");

            double result = expr->evaluate([](std::string const& var) {
                if (var == "$x") return 5.0;
                return 0.0;
            });
            check(approx_equal(result, 10.0));
        }

        it("handles multiple variables") {
            auto expr = ExpressionEvaluator::compile("$price - $discount");
            check(expr != nullptr);
            check(expr->variables().size() == 2);

            double result = expr->evaluate([](std::string const& var) {
                if (var == "$price") return 100.0;
                if (var == "$discount") return 15.0;
                return 0.0;
            });
            check(approx_equal(result, 85.0));
        }

        it("handles dotted variable names") {
            auto expr = ExpressionEvaluator::compile("$order.total * $tax.rate");
            check(expr != nullptr);
            check(expr->variables().size() == 2);

            double result = expr->evaluate([](std::string const& var) {
                if (var == "$order.total") return 100.0;
                if (var == "$tax.rate") return 0.08;
                return 0.0;
            });
            check(approx_equal(result, 8.0));
        }

        it("handles dotted variable names with spaces around dot") {
            auto expr = ExpressionEvaluator::compile("$order. total * $tax . rate");
            check(expr != nullptr);
            check(expr->variables().size() == 2);
            check(expr->variables()[0] == "$order.total");
            check(expr->variables()[1] == "$tax.rate");

            double result = expr->evaluate([](std::string const& var) {
                if (var == "$order.total") return 100.0;
                if (var == "$tax.rate") return 0.08;
                return 0.0;
            });
            check(approx_equal(result, 8.0));
        }

        it("handles same variable used multiple times") {
            auto expr = ExpressionEvaluator::compile("$x + $x * $x");
            check(expr != nullptr);
            check(expr->variables().size() == 1);

            double result = expr->evaluate([](std::string const& var) {
                if (var == "$x") return 3.0;
                return 0.0;
            });
            // 3 + 3 * 3 = 3 + 9 = 12
            check(approx_equal(result, 12.0));
        }
    }

    group("Math functions") {
        it("evaluates sin and cos") {
            auto expr = ExpressionEvaluator::compile("sin(0) + cos(0)");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const&) { return 0.0; });
            // sin(0) = 0, cos(0) = 1
            check(approx_equal(result, 1.0));
        }

        it("evaluates sqrt") {
            auto expr = ExpressionEvaluator::compile("sqrt($x)");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const& var) {
                if (var == "$x") return 16.0;
                return 0.0;
            });
            check(approx_equal(result, 4.0));
        }

        it("evaluates exp and log") {
            auto expr = ExpressionEvaluator::compile("log(exp(2))");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const&) { return 0.0; });
            check(approx_equal(result, 2.0));
        }

        it("evaluates pow / exponentiation") {
            auto expr = ExpressionEvaluator::compile("$base ^ $exp");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const& var) {
                if (var == "$base") return 2.0;
                if (var == "$exp") return 10.0;
                return 0.0;
            });
            check(approx_equal(result, 1024.0));
        }

        it("evaluates abs") {
            auto expr = ExpressionEvaluator::compile("abs($x)");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const& var) {
                if (var == "$x") return -42.0;
                return 0.0;
            });
            check(approx_equal(result, 42.0));
        }

        it("evaluates min and max") {
            auto expr = ExpressionEvaluator::compile("min($a, $b) + max($a, $b)");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const& var) {
                if (var == "$a") return 3.0;
                if (var == "$b") return 7.0;
                return 0.0;
            });
            // min(3,7) + max(3,7) = 3 + 7 = 10
            check(approx_equal(result, 10.0));
        }

        it("evaluates floor and ceil") {
            auto expr = ExpressionEvaluator::compile("floor(3.7) + ceil(3.2)");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const&) { return 0.0; });
            // floor(3.7) = 3, ceil(3.2) = 4
            check(approx_equal(result, 7.0));
        }

        it("evaluates bounded expression via min/max") {
            auto expr = ExpressionEvaluator::compile("min(max($x, -1), 1)");
            check(expr != nullptr);

            // Value within range
            double result1 = expr->evaluate([](std::string const& var) {
                if (var == "$x") return 0.5;
                return 0.0;
            });
            check(approx_equal(result1, 0.5));

            // Value below range
            double result2 = expr->evaluate([](std::string const& var) {
                if (var == "$x") return -5.0;
                return 0.0;
            });
            check(approx_equal(result2, -1.0));

            // Value above range
            double result3 = expr->evaluate([](std::string const& var) {
                if (var == "$x") return 5.0;
                return 0.0;
            });
            check(approx_equal(result3, 1.0));
        }
    }

    group("Constants") {
        it("provides pi constant") {
            auto expr = ExpressionEvaluator::compile("pi");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const&) { return 0.0; });
            check(approx_equal(result, 3.14159265358979, 1e-9));
        }

        it("provides infinity constant") {
            auto expr = ExpressionEvaluator::compile("inf");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const&) { return 0.0; });
            check(std::isinf(result));
        }

        it("can compute e using exp(1)") {
            auto expr = ExpressionEvaluator::compile("exp(1)");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const&) { return 0.0; });
            check(approx_equal(result, 2.71828182845905, 1e-5));
        }
    }

    group("Complex expressions") {
        it("evaluates Pythagorean theorem") {
            auto expr = ExpressionEvaluator::compile("sqrt($a^2 + $b^2)");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const& var) {
                if (var == "$a") return 3.0;
                if (var == "$b") return 4.0;
                return 0.0;
            });
            check(approx_equal(result, 5.0));
        }

        it("evaluates compound interest formula") {
            // A = P * (1 + r)^t
            auto expr = ExpressionEvaluator::compile("$principal * (1 + $rate)^$years");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const& var) {
                if (var == "$principal") return 1000.0;
                if (var == "$rate") return 0.05;
                if (var == "$years") return 10.0;
                return 0.0;
            });
            // 1000 * 1.05^10 ≈ 1628.89
            check(approx_equal(result, 1628.89, 0.01));
        }

        it("verifies trigonometric identity sin^2 + cos^2 = 1") {
            auto expr = ExpressionEvaluator::compile("sin($x)^2 + cos($x)^2");
            check(expr != nullptr);
            double result = expr->evaluate([](std::string const& var) {
                if (var == "$x") return 1.234;  // arbitrary angle
                return 0.0;
            });
            check(approx_equal(result, 1.0));
        }
    }

    group("Error handling") {
        it("rejects empty expression") {
            std::string error;
            auto expr = ExpressionEvaluator::compile("", &error);
            check(expr == nullptr);
            check(!error.empty());
        }

        it("rejects invalid syntax") {
            std::string error;
            auto expr = ExpressionEvaluator::compile("1 + * 2", &error);
            check(expr == nullptr);
            check(!error.empty());
        }

        it("rejects unbalanced parentheses") {
            std::string error;
            auto expr = ExpressionEvaluator::compile("(1 + 2", &error);
            check(expr == nullptr);
            check(!error.empty());
        }

        it("rejects unknown function") {
            std::string error;
            auto expr = ExpressionEvaluator::compile("unknownfunc(1)", &error);
            check(expr == nullptr);
            check(!error.empty());
        }
    }

    group("Accessors") {
        it("returns original expression string") {
            std::string original = "$price * (1 - $discount)";
            auto expr = ExpressionEvaluator::compile(original);
            check(expr != nullptr);
            check(expr->expression_string() == original);
        }
    }

    group("Compilation") {
        it("compiles and evaluates expression") {
            std::string error;
            auto expr = ExpressionEvaluator::compile("$a * 3 + 1", &error);
            check(expr != nullptr);
            check(error.empty());

            double result = expr->evaluate([](std::string const& var) {
                if (var == "$a") return 4.0;
                return 0.0;
            });
            check(approx_equal(result, 13.0));
        }
    }

}

