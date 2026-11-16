#include "expression_descriptor.hpp"
#include <tinytest.hpp>
#include <atomic>
#include <cstdint>
#include <stdexcept>

using rulesforge::ExpressionDescriptor;
namespace {
std::optional<ConstraintValue> input(void* context, ExpressionDescriptor::Variable const&) {
    return *static_cast<int64_t*>(context);
}
}
suite("MIR scalar compilation and execution") {
    bench("separates compilation from repeated invocation") {
        constexpr uint64_t iterations = 10000;
        auto program = ExpressionDescriptor::compile_required("$x * 3 + 7");
        constexpr uint64_t expected = 3 * iterations * (iterations - 1) / 2 + 7 * iterations;
        uint64_t sum = 0;
        benchmark_ops("C++ arithmetic reference", 5, iterations) {
            sum = 0;
            for (int64_t i = 0; i < static_cast<int64_t>(iterations); ++i) {
                std::atomic_signal_fence(std::memory_order_seq_cst);
                sum += i * 3 + 7;
            }
        }
        check_equal(sum, expected);
        benchmark_ops("MIR warm invocation including frame and resolver", 5, iterations) {
            sum = 0;
            for (int64_t i = 0; i < static_cast<int64_t>(iterations); ++i) {
                std::atomic_signal_fence(std::memory_order_seq_cst);
                auto value = program->evaluate(input, &i);
                if (!value) throw std::logic_error("benchmark scalar value missing");
                sum += std::get<int64_t>(*value);
            }
        }
        check_equal(sum, expected);
        benchmark_ops("MIR parse lower compile and one invocation", 5, 10) {
            for (size_t i = 0; i < 10; ++i) {
                auto fresh = ExpressionDescriptor::compile_required("$x * 3 + 7");
                int64_t value = 11;
                auto result = fresh->evaluate(input, &value);
                if (!result || std::get<int64_t>(*result) != 40)
                    throw std::logic_error("benchmark compiled result mismatch");
            }
        }
    }
}
