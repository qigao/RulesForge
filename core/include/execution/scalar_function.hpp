#pragma once
#include "model/value_types.hpp"
#include <charconv>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>

namespace rulesforge {
// Locale-independent decimal conversion for bounded views on every supported
// standard library. The returned pointer always refers to the caller's view.
std::from_chars_result parse_scalar_double(std::string_view text, double &value);
// A lexical number probe, with errors distinguished from a non-number. An
// integer overflow must not be retried as an approximate floating value.
inline std::optional<ConstraintValue> parse_scalar_number(std::string_view text) {
    if (text.empty()) return std::nullopt;
    int64_t integer = 0;
    auto end = text.data() + text.size();
    auto [position, status] = std::from_chars(text.data(), end, integer);
    if (position == end) {
        if (status == std::errc{}) return ConstraintValue{integer};
        if (status == std::errc::result_out_of_range)
            throw std::out_of_range("Integer expression literal exceeds int64 range");
    }
    double number = 0;
    auto [number_end, number_status] = parse_scalar_double(text, number);
    if (number_end == end && number_status == std::errc{}) {
        if (!std::isfinite(number)) throw std::domain_error("Non-finite expression literal");
        return ConstraintValue{number};
    }
    if (number_end == end && number_status == std::errc::result_out_of_range)
        throw std::out_of_range("Floating expression literal exceeds double range");
    return std::nullopt;
}
// Shared finite numeric vocabulary for LHS and RHS. Names are recognized by
// re2c; execution dispatches on the admitted operation, not repeated text tests.
enum class ScalarFunction : uint8_t {
    Floor, Ceil, Abs, Round, Sqrt, Sin, Cos, Tan, Acos, Asin, Atan, Log, Exp,
    Min, Max, Pow, Fmod
};
ScalarFunction parse_scalar_function(std::string_view name);
constexpr size_t scalar_function_arity(ScalarFunction function) {
    return function < ScalarFunction::Min ? 1 : 2;
}
inline double apply_scalar_function(ScalarFunction function, std::span<double const> args) {
    if (args.size() != scalar_function_arity(function))
        throw std::invalid_argument("Scalar function argument count mismatch");
    for (double value : args)
        if (!std::isfinite(value)) throw std::domain_error("Non-finite scalar function argument");
    double result;
    switch (function) {
        case ScalarFunction::Floor: result = std::floor(args[0]); break;
        case ScalarFunction::Ceil: result = std::ceil(args[0]); break;
        case ScalarFunction::Abs: result = std::fabs(args[0]); break;
        case ScalarFunction::Round: result = std::round(args[0]); break;
        case ScalarFunction::Sqrt: result = std::sqrt(args[0]); break;
        case ScalarFunction::Sin: result = std::sin(args[0]); break;
        case ScalarFunction::Cos: result = std::cos(args[0]); break;
        case ScalarFunction::Tan: result = std::tan(args[0]); break;
        case ScalarFunction::Acos: result = std::acos(args[0]); break;
        case ScalarFunction::Asin: result = std::asin(args[0]); break;
        case ScalarFunction::Atan: result = std::atan(args[0]); break;
        case ScalarFunction::Log: result = std::log(args[0]); break;
        case ScalarFunction::Exp: result = std::exp(args[0]); break;
        case ScalarFunction::Min: result = std::min(args[0], args[1]); break;
        case ScalarFunction::Max: result = std::max(args[0], args[1]); break;
        case ScalarFunction::Pow: result = std::pow(args[0], args[1]); break;
        case ScalarFunction::Fmod: result = std::fmod(args[0], args[1]); break;
        default: throw std::invalid_argument("Invalid scalar function operation");
    }
    if (!std::isfinite(result)) throw std::domain_error("Non-finite scalar function result");
    return result;
}
} // namespace rulesforge
