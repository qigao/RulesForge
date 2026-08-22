#include "compiled_expression.hpp"
#include "tinytest.hpp"

using namespace rulesforge;

suite("CompiledExpressionCompat") {
    it("keeps the legacy alias as an expression descriptor") {
        auto expr = CompiledExpression::compile("$x + 2");
        check(expr != nullptr);
        check(expr->expression_string() == "$x + 2");
        check(expr->variables().size() == 1);
        check(expr->variables()[0] == "$x");
    }
}
