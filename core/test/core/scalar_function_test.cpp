#include "execution/scalar_function.hpp"
#include "text/scalar_text.hpp"
#include "model/constraint_types.hpp"
#include "rhs_builtins.hpp"
#include <array>
#include <tinytest.hpp>

using namespace rulesforge;
suite("Native scalar vocabulary") {
    it("parses bounded numeric views and rejects overflow without changing numeric types") {
        auto integer = parse_scalar_number("9223372036854775807");
        check_true(integer.has_value());
        check_equal(std::get<int64_t>(*integer), std::numeric_limits<int64_t>::max());
        check_throws_as(parse_scalar_number("9223372036854775808"), std::out_of_range);
        check_throws_as(parse_scalar_number("1e999"), std::out_of_range);
        check_throws_as(parse_scalar_number("nan"), std::domain_error);
        check_false(parse_scalar_number("reading").has_value());
        std::array<char, 4> value{'1', '.', '2', '5'};
        check_equal(std::get<double>(*parse_scalar_number({value.data(), value.size()})), 1.25);
    }
    it("dispatches the shared numeric vocabulary with exact arities") {
        struct Case { char const* name; std::array<double, 2> args; double expected; };
        for (auto const& item : std::array<Case, 17>{{
            {"floor", {1.8, 0}, 1}, {"ceil", {1.2, 0}, 2}, {"abs", {-2, 0}, 2},
            {"round", {1.8, 0}, 2}, {"sqrt", {4, 0}, 2}, {"sin", {0, 0}, 0},
            {"cos", {0, 0}, 1}, {"tan", {0, 0}, 0}, {"acos", {1, 0}, 0},
            {"asin", {0, 0}, 0}, {"atan", {0, 0}, 0}, {"log", {1, 0}, 0},
            {"exp", {0, 0}, 1}, {"min", {1, 2}, 1}, {"max", {1, 2}, 2},
            {"pow", {2, 3}, 8}, {"fmod", {5, 2}, 1}
        }}) {
            auto operation = parse_scalar_function(item.name);
            check_true(is_rhs_builtin(item.name));
            check_equal(apply_scalar_function(operation,
                {item.args.data(), scalar_function_arity(operation)}), item.expected);
        }
    }
    it("accepts bounded non-terminated words and rejects unknown whole words") {
        const std::array<char, 3> word{'m', 'a', 'x'};
        check_equal(static_cast<int>(parse_scalar_function({word.data(), word.size()})),
                    static_cast<int>(ScalarFunction::Max));
        check_false(is_rhs_builtin("sqrt_extra"));
        check_throws_as(parse_scalar_function("sqrt_extra"), std::invalid_argument);
        check_throws_as(parse_compare_op("not containsKey_extra"), std::invalid_argument);
        check_equal(static_cast<int>(parse_compare_op("not containskey")),
                    static_cast<int>(CompareOp::NotContainsKey));
        check_equal(static_cast<int>(parse_compare_op("")), static_cast<int>(CompareOp::None));
        check_throws_as(parse_temporal_op("after_extra"), std::invalid_argument);
    }
    it("reports arity domain and overflow errors instead of producing a non-match") {
        std::array<double, 2> args{-1, 0};
        check_throws_as(apply_scalar_function(ScalarFunction::Sqrt, args), std::invalid_argument);
        check_throws_as(apply_scalar_function(ScalarFunction::Sqrt, {args.data(), 1}), std::domain_error);
        args[0] = 1000;
        check_throws_as(apply_scalar_function(ScalarFunction::Exp, {args.data(), 1}), std::domain_error);
    }
    it("borrows existing text without copying and owns non-string formatting") {
        ConstraintValue value = std::string(1000, 'x');
        ScalarTextView borrowed(value);
        check_true(static_cast<bool>(borrowed));
        check_equal(static_cast<void const*>((*borrowed).data()),
                    static_cast<void const*>(std::get<std::string>(value).data()));
        ScalarTextView formatted(ConstraintValue{DateValue{2026, 11, 16}});
        check_true(static_cast<bool>(formatted));
        check_equal(*formatted, std::string_view("2026-11-16"));
    }
}
