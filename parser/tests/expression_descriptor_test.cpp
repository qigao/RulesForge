#include "expression_descriptor.hpp"
#include "tinytest.hpp"

using namespace rulesforge;

suite("ExpressionDescriptor") {

    group("Expression descriptor") {
        it("preserves simple arithmetic expression text") {
            auto expr = ExpressionDescriptor::compile("1 + 2");
            check(expr != nullptr);
            check(expr->expression_string() == "1 + 2");
            check(expr->variables().empty());
        }

        it("preserves complex arithmetic expression text") {
            auto expr = ExpressionDescriptor::compile("(10 + 5) * 2");
            check(expr != nullptr);
            check(expr->expression_string() == "(10 + 5) * 2");
            check(expr->variables().empty());
        }
    }

    group("Variables") {
        it("collects a single variable") {
            auto expr = ExpressionDescriptor::compile("$x * 2");
            check(expr != nullptr);
            check(expr->variables().size() == 1);
            check(expr->variables()[0] == "$x");
        }

        it("collects multiple variables in first-use order") {
            auto expr = ExpressionDescriptor::compile("$price - $discount + $price");
            check(expr != nullptr);
            check(expr->variables().size() == 2);
            check(expr->variables()[0] == "$price");
            check(expr->variables()[1] == "$discount");
        }

        it("normalizes dotted variable names with spaces around dot") {
            auto expr = ExpressionDescriptor::compile("$order. total * $tax . rate");
            check(expr != nullptr);
            check(expr->expression_string() == "$order. total * $tax . rate");
            check(expr->variables().size() == 2);
            check(expr->variables()[0] == "$order.total");
            check(expr->variables()[1] == "$tax.rate");
        }
    }

    group("Error handling") {
        it("rejects empty expression") {
            std::string error;
            auto expr = ExpressionDescriptor::compile("", &error);
            check(expr == nullptr);
            check(!error.empty());
        }

        it("keeps unsupported expression shapes for backend diagnostics") {
            std::string error;
            auto expr = ExpressionDescriptor::compile("unknownfunc(1)", &error);
            check(expr != nullptr);
            check(error.empty());
            check(expr->expression_string() == "unknownfunc(1)");
        }
    }

    group("Accessors") {
        it("returns original expression string") {
            std::string original = "$price * (1 - $discount)";
            auto expr = ExpressionDescriptor::compile(original);
            check(expr != nullptr);
            check(expr->expression_string() == original);
        }
    }
}
