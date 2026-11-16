#include "expression_descriptor.hpp"
#include <tinytest.hpp>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <thread>

using rulesforge::ExpressionDescriptor;
namespace {
using Program = ExpressionDescriptor;
struct Input {
    std::map<std::string, ConstraintValue> values;
    size_t reads = 0;
    bool fail = false;
};
std::optional<ConstraintValue> resolve(void* opaque, Program::Variable const& access) {
    auto& input = *static_cast<Input*>(opaque);
    ++input.reads;
    if (input.fail) throw std::out_of_range("original resolver diagnostic");
    auto found = input.values.find(access.name);
    if (found == input.values.end()) return std::nullopt;
    return found->second;
}
ConstraintValue run(std::string const& text, Input& input) {
    auto program = Program::compile_required(text);
    auto result = program->evaluate(resolve, &input);
    if (!result) throw std::runtime_error("test expression has no value");
    return *result;
}
template<class T> T scalar(ConstraintValue const& value) { return std::get<T>(value); }
}

suite("MIR scalar programs") {
    it("executes arithmetic with precedence and left associativity") {
        Input input;
        check_equal(scalar<int64_t>(run("20 - 5 - 3", input)), int64_t{12});
        check_equal(scalar<int64_t>(run("2 + 3 * 4", input)), int64_t{14});
        check_equal(scalar<double>(run("(2 + 6) / 4 / 2", input)), 1.0);
        check_equal(scalar<int64_t>(run("-(3 + 4) * +2", input)), int64_t{-14});
        check_equal(scalar<double>(run("1e2 + .5", input)), 100.5);
    }
    it("preserves exact integers above double precision and at int64 boundaries") {
        Input input;
        check_equal(scalar<int64_t>(run("9007199254740993 + 2", input)), int64_t{9007199254740995});
        check_true(scalar<bool>(run("9007199254740993 > 9007199254740992", input)));
        check_equal(scalar<int64_t>(run("-9223372036854775808", input)),
                    std::numeric_limits<int64_t>::min());
        check_equal(scalar<int64_t>(run("9223372036854775807", input)),
                    std::numeric_limits<int64_t>::max());
    }
    it("checks overflow in native integer addition subtraction multiplication and negation") {
        Input input;
        check_throws_as(run("9223372036854775807 + 1", input), std::overflow_error);
        check_throws_as(run("-9223372036854775808 - 1", input), std::overflow_error);
        check_throws_as(run("3037000500 * 3037000500", input), std::overflow_error);
        check_throws_as(run("-(-9223372036854775808)", input), std::overflow_error);
        check_equal(scalar<int64_t>(run("-3037000499 * 3037000499", input)),
                    int64_t{-9223372030926249001});
    }
    it("compares signed and unsigned values without rounding integers") {
        Input input{{{"$u", std::numeric_limits<uint64_t>::max()},
                     {"$v", std::numeric_limits<uint64_t>::max() - 1},
                     {"$n", int64_t{-1}}, {"$p", int64_t{7}}}};
        check_true(scalar<bool>(run("$u > $v && $n < $u && $u != $n", input)));
        input.values["$u"] = uint64_t{7};
        check_true(scalar<bool>(run("$u == $p && $p >= $u", input)));
        input.values["$p"] = int64_t{9007199254740993};
        input.values["$d"] = double{9007199254740992};
        check_true(scalar<bool>(run("$p > $d && $d < $p && $p != $d", input)));
        input.values["$u"] = std::numeric_limits<uint64_t>::max();
        input.values["$d"] = 18446744073709551616.0;
        check_true(scalar<bool>(run("$u < $d && $d > $u", input)));
    }
    it("short circuits resolver reads and arithmetic errors") {
        Input input{{{"$x", int64_t{0}}}};
        check_false(scalar<bool>(run("$x != 0 && 100 / $x > 3", input)));
        check_equal(input.reads, size_t{1});
        input.fail = true;
        check_true(scalar<bool>(run("true || $missing", input)));
        check_false(scalar<bool>(run("false && sqrt(-1) > $missing", input)));
        check_equal(input.reads, size_t{1});
        check_true(scalar<bool>(run("!false && !(1 > 2)", input)));
    }
    it("loads a repeated variable only once per invocation") {
        Input input{{{"$x", int64_t{4}}}};
        auto program = Program::compile_required("$x * $x + $x");
        check_equal(scalar<int64_t>(*program->evaluate(resolve, &input)), int64_t{20});
        check_equal(input.reads, size_t{1});
        input.values["$x"] = int64_t{5};
        check_equal(scalar<int64_t>(*program->evaluate(resolve, &input)), int64_t{30});
        check_equal(input.reads, size_t{2});
    }
    it("propagates resolver exceptions from the host continuation and remains reusable") {
        Input input;
        input.fail = true;
        auto program = Program::compile_required("$x + 1");
        check_throws_with(program->evaluate(resolve, &input), "original resolver diagnostic");
        input.fail = false;
        check_false(program->evaluate(resolve, &input).has_value());
        input.values["$x"] = int64_t{8};
        check_equal(scalar<int64_t>(*program->evaluate(resolve, &input)), int64_t{9});
    }
    it("rejects invalid operand types divide by zero and nonfinite values") {
        Input input{{{"$x", std::numeric_limits<double>::infinity()}}};
        check_throws_as(run("1 / 0", input), std::domain_error);
        check_throws_as(run("1e308 * 10", input), std::domain_error);
        check_throws_as(run("$x + 1", input), std::domain_error);
        check_throws_as(run("true + 1", input), std::invalid_argument);
        check_throws_as(run("\"12\" * 2", input), std::invalid_argument);
        check_throws_as(run("sqrt(-1)", input), std::domain_error);
    }
    it("uses admitted math functions through the native helper ABI") {
        Input input;
        std::array<std::pair<char const*, double>, 17> cases{{
            {"floor(2.9)",2}, {"ceil(2.1)",3}, {"abs(-4)",4}, {"round(2.6)",3},
            {"sqrt(9)",3}, {"sin(0)",0}, {"cos(0)",1}, {"tan(0)",0},
            {"acos(1)",0}, {"asin(0)",0}, {"atan(0)",0}, {"log(1)",0},
            {"exp(0)",1}, {"min(3,4)",3}, {"max(3,4)",4}, {"pow(2,3)",8}, {"fmod(7,4)",3}
        }};
        for (auto [text, expected] : cases)
            check_within(scalar<double>(run(text, input)), expected, 1e-12);
    }
    it("owns string results after the program and inputs are destroyed") {
        Input input;
        auto result = run("\"a long escaped string with owning storage\\nend\"", input);
        check_equal(scalar<std::string>(result), std::string("a long escaped string with owning storage\nend"));
        check_true(scalar<bool>(run("\"alpha\" < \"beta\" && null == nil", input)));
    }
    it("prepares canonical field access paths and ignores variables inside string literals") {
        auto program = Program::compile_required("$order. items[1].value + $tax . rate");
        struct Seen { size_t calls = 0; } seen;
        auto value = program->evaluate([](void* owner, Program::Variable const& access)
            -> std::optional<ConstraintValue> {
            ++static_cast<Seen*>(owner)->calls;
            if (access.name == "$order.items[1].value") {
                if (access.binding != "$order" || access.path.size() != 2 ||
                    access.path[0].indices.size() != 1 || access.path[0].indices[0].int_index != 1)
                    throw std::logic_error("invalid compiled path");
            }
            return int64_t{3};
        }, &seen);
        check_equal(scalar<int64_t>(*value), int64_t{6});
        check_equal(seen.calls, size_t{2});
        check_empty(Program::compile_required("\"$not_a_variable\"")->variables());
    }
    it("fails unsupported syntax and resource limits during compilation") {
        for (auto text : {"unknown(1)", "pow(1)", "abs(1,2)", "1 +", "1 2", "$x.", "2 ^ 3",
                          "\"unterminated", "\"bad\\q\"", "9223372036854775808", "1e309"})
            check_throws(Program::compile_required(text));
        check_throws(Program::compile_required(std::string("1\0 + 2", 6)));
        check_throws_as(Program::compile_required(std::string(Program::max_source_bytes + 1, ' ')), std::length_error);
        check_throws_as(Program::compile_required(std::string(65, '(') + "1" + std::string(65, ')')), std::length_error);
        auto balanced = [](auto const& self, size_t begin, size_t count, bool variables) -> std::string {
            if (count == 1) return variables ? "$v" + std::to_string(begin) : "1";
            auto half = count / 2;
            return "(" + self(self, begin, half, variables) + "+" +
                         self(self, begin + half, count - half, variables) + ")";
        };
        check_throws_as(Program::compile_required(balanced(balanced, 0, 65, true)), std::length_error);
        check_throws_as(Program::compile_required(balanced(balanced, 0, 129, false)), std::length_error);
    }
    it("shares immutable native code across concurrent independent invocations") {
        auto program = Program::compile_required("$x * 3 + 7");
        std::array<std::exception_ptr, 4> errors{};
        std::array<int64_t, 4> totals{};
        std::array<std::jthread, 4> workers;
        for (size_t lane = 0; lane < workers.size(); ++lane)
            workers[lane] = std::jthread([&, lane] {
                try {
                    Input input;
                    for (int64_t i = 0; i < 1000; ++i) {
                        input.values["$x"] = i;
                        auto value = program->evaluate(resolve, &input);
                        if (!value) throw std::logic_error("missing concurrent value");
                        totals[lane] += scalar<int64_t>(*value);
                    }
                } catch (...) { errors[lane] = std::current_exception(); }
            });
        for (auto& worker : workers) worker.join();
        for (size_t lane = 0; lane < workers.size(); ++lane) {
            check_false(bool(errors[lane]));
            check_equal(totals[lane], int64_t{1505500});
        }
    }
}
