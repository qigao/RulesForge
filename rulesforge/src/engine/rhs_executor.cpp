#include "engine/rhs_executor.hpp"
#include "engine/i_network_callback.hpp"
#include "engine/rhs_backend_plan.hpp"
#include "engine/stateful_session.hpp"
#include "expression_descriptor.hpp"
#include "core/logging_control.hpp"

#include <atomic>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <stdexcept>
#include <variant>
#include <optional>
#include <unordered_map>
#include <vector>
#include <string>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace rulesforge {
namespace {
static std::atomic<uint64_t> g_eval_assignment_us{0};
static std::atomic<uint64_t> g_condition_eval_us{0};
static std::atomic<uint64_t> g_update_action_us{0};
static std::atomic<uint64_t> g_insert_action_us{0};
static std::atomic<uint64_t> g_retract_action_us{0};
static std::atomic<uint64_t> g_cpp_action_plan_exec_count{0};
static std::atomic<uint64_t> g_cpp_action_plan_error_count{0};
static std::atomic<uint64_t> g_expression_exec_count{0};
static std::atomic<uint64_t> g_expression_error_count{0};
static std::atomic<uint64_t> g_expression_non_scalar_error_count{0};
static std::atomic<uint64_t> g_execution_mutex_wait_us{0};
static std::atomic<uint64_t> g_api_mutex_wait_us{0};

class ScopedUsTimer {
public:
    explicit ScopedUsTimer(std::atomic<uint64_t>& target)
        : target_(target), start_(std::chrono::steady_clock::now()) {}
    ~ScopedUsTimer() {
        auto end = std::chrono::steady_clock::now();
        target_.fetch_add(
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count()),
            std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t>& target_;
    std::chrono::steady_clock::time_point start_;
};

ConstraintValue coerce_numeric_assignment_to_existing_type(ConstraintValue const& current_value,
                                                           ConstraintValue new_value) {
    if (auto const* current_int = std::get_if<int64_t>(&current_value)) {
        (void)current_int;
        if (auto const* new_double = std::get_if<double>(&new_value)) {
            constexpr double min_i64 = static_cast<double>(std::numeric_limits<int64_t>::min());
            constexpr double max_i64 = static_cast<double>(std::numeric_limits<int64_t>::max());
            if (std::isfinite(*new_double) &&
                std::trunc(*new_double) == *new_double &&
                *new_double >= min_i64 &&
                *new_double <= max_i64) {
                return static_cast<int64_t>(*new_double);
            }
        }
        return new_value;
    }

    if (auto const* current_double = std::get_if<double>(&current_value)) {
        (void)current_double;
        if (auto const* new_int = std::get_if<int64_t>(&new_value)) {
            return static_cast<double>(*new_int);
        }
    }

    if (std::holds_alternative<uint64_t>(current_value)) {
        if (auto const* new_int = std::get_if<int64_t>(&new_value); new_int && *new_int >= 0) {
            return static_cast<uint64_t>(*new_int);
        }
    }

    if (std::holds_alternative<DurationValue>(current_value)) {
        if (auto const* new_int = std::get_if<int64_t>(&new_value)) {
            return DurationValue{*new_int};
        }
    }

    return new_value;
}

ConstraintValue coerce_assignment_to_declared_type(std::optional<FieldType> declared_type,
                                                   ConstraintValue new_value) {
    if (!declared_type.has_value()) {
        return new_value;
    }

    switch (*declared_type) {
        case FT_Int:
        case FT_Long:
            if (auto const* new_double = std::get_if<double>(&new_value)) {
                constexpr double min_i64 = static_cast<double>(std::numeric_limits<int64_t>::min());
                constexpr double max_i64 = static_cast<double>(std::numeric_limits<int64_t>::max());
                if (std::isfinite(*new_double) &&
                    std::trunc(*new_double) == *new_double &&
                    *new_double >= min_i64 &&
                    *new_double <= max_i64) {
                    return static_cast<int64_t>(*new_double);
                }
            }
            return new_value;

        case FT_Boolean:
            return new_value;

        case FT_UInt64:
            if (auto const* new_int = std::get_if<int64_t>(&new_value); new_int && *new_int >= 0) {
                return static_cast<uint64_t>(*new_int);
            }
            return new_value;

        case FT_Duration:
            if (auto const* new_int = std::get_if<int64_t>(&new_value)) {
                return DurationValue{*new_int};
            }
            return new_value;

        case FT_Double:
        case FT_Float:
            if (auto const* new_int = std::get_if<int64_t>(&new_value)) {
                return static_cast<double>(*new_int);
            }
            return new_value;

        default:
            return new_value;
    }
}

std::optional<ConstraintValue> resolve_global_field(ConstraintValue const& value, std::string const& field_name) {
    if (field_name == "this") return value;

    if (auto const* tl = std::get_if<std::shared_ptr<TypedList>>(&value)) {
        if (*tl && field_name == "size") return static_cast<int64_t>((*tl)->values.size());
        return std::nullopt;
    }

    if (auto const* vs = std::get_if<std::shared_ptr<ValueSet>>(&value)) {
        if (*vs && field_name == "size") return static_cast<int64_t>((*vs)->values.size());
        return std::nullopt;
    }

    if (auto const* vm = std::get_if<std::shared_ptr<ValueMap>>(&value)) {
        if (!*vm) return std::nullopt;
        if (field_name == "size") return static_cast<int64_t>((*vm)->entries.size());
        auto it = (*vm)->entries.find(ConstraintValue{field_name});
        if (it != (*vm)->entries.end()) return it->second;
        return std::nullopt;
    }

    return std::nullopt;
}

std::string trim_ascii(std::string const& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool is_truthy(ConstraintValue const& value) {
    if (auto const* boolean = std::get_if<bool>(&value)) return *boolean;
    if (auto const* d = std::get_if<double>(&value)) return *d != 0.0;
    if (auto const* i = std::get_if<int64_t>(&value)) return *i != 0;
    if (auto const* u = std::get_if<uint64_t>(&value)) return *u != 0;
    if (auto const* duration = std::get_if<DurationValue>(&value)) {
        return duration->milliseconds != 0;
    }
    if (auto const* s = std::get_if<std::string>(&value)) {
        return !s->empty() && *s != "false" && *s != "0";
    }
    return !std::holds_alternative<NilValue>(value);
}

bool values_equal_for_switch(ConstraintValue const& lhs, ConstraintValue const& rhs) {
    if (std::holds_alternative<double>(lhs) && std::holds_alternative<int64_t>(rhs)) {
        return std::get<double>(lhs) == static_cast<double>(std::get<int64_t>(rhs));
    }
    if (std::holds_alternative<int64_t>(lhs) && std::holds_alternative<double>(rhs)) {
        return static_cast<double>(std::get<int64_t>(lhs)) == std::get<double>(rhs);
    }
    auto const lhs_text = scalar_text(lhs);
    auto const rhs_text = scalar_text(rhs);
    if (lhs_text && rhs_text) return *lhs_text == *rhs_text;
    return lhs == rhs;
}

std::optional<double> scalar_number(ConstraintValue const& value) {
    if (auto const* integer = std::get_if<int64_t>(&value)) return static_cast<double>(*integer);
    if (auto const* number = std::get_if<double>(&value)) return *number;
    if (auto const* unsigned_integer = std::get_if<uint64_t>(&value)) {
        return static_cast<double>(*unsigned_integer);
    }
    if (auto const* duration = std::get_if<DurationValue>(&value)) {
        return static_cast<double>(duration->milliseconds);
    }
    if (auto const* enumeration = std::get_if<EnumValue>(&value)) {
        return std::visit([](auto numeric) { return static_cast<double>(numeric); },
                          enumeration->value);
    }
    return std::nullopt;
}

bool compare_scalar_values(std::string_view op, ConstraintValue const& lhs, ConstraintValue const& rhs) {
    if (auto lhs_number = scalar_number(lhs)) {
        if (auto rhs_number = scalar_number(rhs)) {
            if (op == "==") return *lhs_number == *rhs_number;
            if (op == "!=") return *lhs_number != *rhs_number;
            if (op == ">=") return *lhs_number >= *rhs_number;
            if (op == "<=") return *lhs_number <= *rhs_number;
            if (op == ">") return *lhs_number > *rhs_number;
            if (op == "<") return *lhs_number < *rhs_number;
            return false;
        }
    }

    auto const lhs_string = scalar_text(lhs);
    auto const rhs_string = scalar_text(rhs);
    if (lhs_string && rhs_string) {
        if (op == "==") return *lhs_string == *rhs_string;
        if (op == "!=") return *lhs_string != *rhs_string;
        if (op == ">=") return *lhs_string >= *rhs_string;
        if (op == "<=") return *lhs_string <= *rhs_string;
        if (op == ">") return *lhs_string > *rhs_string;
        if (op == "<") return *lhs_string < *rhs_string;
        return false;
    }

    if (op == "==" || op == "!=") {
        bool const equal = lhs == rhs;
        return op == "==" ? equal : !equal;
    }
    return false;
}

std::optional<std::pair<std::size_t, std::string_view>> find_top_level_compare(std::string const& expression) {
    bool in_string = false;
    char quote = '\0';
    for (std::size_t i = 0; i < expression.size(); ++i) {
        char const ch = expression[i];
        if (in_string) {
            if (ch == '\\' && i + 1 < expression.size()) {
                ++i;
                continue;
            }
            if (ch == quote) {
                in_string = false;
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            in_string = true;
            quote = ch;
            continue;
        }
        if (i + 1 < expression.size()) {
            std::string_view const two(expression.data() + i, 2);
            if (two == "==" || two == "!=" || two == ">=" || two == "<=") {
                return std::pair<std::size_t, std::string_view>{i, two};
            }
        }
        if (ch == '>' || ch == '<') {
            return std::pair<std::size_t, std::string_view>{i, std::string_view(expression.data() + i, 1)};
        }
    }
    return std::nullopt;
}

std::optional<std::pair<std::size_t, char>> find_top_level_arithmetic(std::string const& expression) {
    bool in_string = false;
    char quote = '\0';
    for (int pass = 0; pass < 2; ++pass) {
        in_string = false;
        int depth = 0;
        for (std::size_t i = 0; i < expression.size(); ++i) {
            char const ch = expression[i];
            if (in_string) {
                if (ch == '\\' && i + 1 < expression.size()) {
                    ++i;
                    continue;
                }
                if (ch == quote) {
                    in_string = false;
                }
                continue;
            }
            if (ch == '"' || ch == '\'') {
                in_string = true;
                quote = ch;
                continue;
            }
            if (ch == '(') {
                ++depth;
                continue;
            }
            if (ch == ')') {
                --depth;
                continue;
            }
            if (depth != 0) {
                continue;
            }
            bool const wanted = pass == 0 ? (ch == '+' || ch == '-') : (ch == '*' || ch == '/');
            if (!wanted) {
                continue;
            }
            if ((ch == '+' || ch == '-') && (i == 0 || expression[i - 1] == 'e' || expression[i - 1] == 'E')) {
                continue;
            }
            return std::pair<std::size_t, char>{i, ch};
        }
    }
    return std::nullopt;
}

bool has_wrapping_parentheses(std::string const& expression) {
    if (expression.size() < 2 || expression.front() != '(' || expression.back() != ')') {
        return false;
    }

    bool in_string = false;
    char quote = '\0';
    int depth = 0;
    for (std::size_t index = 0; index < expression.size(); ++index) {
        char const ch = expression[index];
        if (in_string) {
            if (ch == '\\' && index + 1 < expression.size()) {
                ++index;
                continue;
            }
            if (ch == quote) {
                in_string = false;
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            in_string = true;
            quote = ch;
            continue;
        }
        if (ch == '(') {
            ++depth;
            continue;
        }
        if (ch == ')') {
            --depth;
            if (depth == 0 && index + 1 != expression.size()) {
                return false;
            }
        }
    }
    return depth == 0;
}

std::vector<std::string> split_top_level_arguments(std::string const& text) {
    std::vector<std::string> args;
    bool in_string = false;
    char quote = '\0';
    int depth = 0;
    std::size_t begin = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        char const ch = text[index];
        if (in_string) {
            if (ch == '\\' && index + 1 < text.size()) {
                ++index;
                continue;
            }
            if (ch == quote) {
                in_string = false;
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            in_string = true;
            quote = ch;
            continue;
        }
        if (ch == '(') {
            ++depth;
            continue;
        }
        if (ch == ')') {
            --depth;
            continue;
        }
        if (ch == ',' && depth == 0) {
            args.push_back(trim_ascii(text.substr(begin, index - begin)));
            begin = index + 1;
        }
    }
    args.push_back(trim_ascii(text.substr(begin)));
    return args;
}

std::optional<std::pair<std::string, std::vector<std::string>>> parse_function_call(std::string const& text) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        unsigned char const ch = static_cast<unsigned char>(text[pos]);
        if (!std::isalpha(ch) && text[pos] != '_') {
            break;
        }
        ++pos;
        while (pos < text.size()) {
            unsigned char const next = static_cast<unsigned char>(text[pos]);
            if (!std::isalnum(next) && text[pos] != '_') {
                break;
            }
            ++pos;
        }
        break;
    }
    if (pos == 0 || pos >= text.size() || text[pos] != '(' || text.back() != ')') {
        return std::nullopt;
    }
    std::string const wrapped = text.substr(pos);
    if (!has_wrapping_parentheses(wrapped)) {
        return std::nullopt;
    }
    return std::pair<std::string, std::vector<std::string>>{
        text.substr(0, pos),
        split_top_level_arguments(text.substr(pos + 1, text.size() - pos - 2))};
}
}  // namespace

namespace rhs_prof {
void reset_stats() {
    g_eval_assignment_us.store(0, std::memory_order_relaxed);
    g_condition_eval_us.store(0, std::memory_order_relaxed);
    g_update_action_us.store(0, std::memory_order_relaxed);
    g_insert_action_us.store(0, std::memory_order_relaxed);
    g_retract_action_us.store(0, std::memory_order_relaxed);
    g_cpp_action_plan_exec_count.store(0, std::memory_order_relaxed);
    g_cpp_action_plan_error_count.store(0, std::memory_order_relaxed);
    g_expression_exec_count.store(0, std::memory_order_relaxed);
    g_expression_error_count.store(0, std::memory_order_relaxed);
    g_expression_non_scalar_error_count.store(0, std::memory_order_relaxed);
    g_execution_mutex_wait_us.store(0, std::memory_order_relaxed);
    g_api_mutex_wait_us.store(0, std::memory_order_relaxed);
}

Stats get_stats() {
    Stats s;
    s.evaluate_assignment_us = g_eval_assignment_us.load(std::memory_order_relaxed);
    s.condition_eval_us = g_condition_eval_us.load(std::memory_order_relaxed);
    s.update_action_us = g_update_action_us.load(std::memory_order_relaxed);
    s.insert_action_us = g_insert_action_us.load(std::memory_order_relaxed);
    s.retract_action_us = g_retract_action_us.load(std::memory_order_relaxed);
    s.cpp_action_plan_exec_count = g_cpp_action_plan_exec_count.load(std::memory_order_relaxed);
    s.cpp_action_plan_error_count = g_cpp_action_plan_error_count.load(std::memory_order_relaxed);
    s.expression_exec_count = g_expression_exec_count.load(std::memory_order_relaxed);
    s.expression_error_count = g_expression_error_count.load(std::memory_order_relaxed);
    s.expression_non_scalar_error_count =
        g_expression_non_scalar_error_count.load(std::memory_order_relaxed);
    s.execution_mutex_wait_us = g_execution_mutex_wait_us.load(std::memory_order_relaxed);
    s.api_mutex_wait_us = g_api_mutex_wait_us.load(std::memory_order_relaxed);
    return s;
}
}  // namespace rhs_prof

RhsExecutor::RhsExecutor(INetworkCallback& callback)
    : callback_(callback) {}

RhsExecutor::~RhsExecutor() = default;

void RhsExecutor::execute(RhsCompiledCommandProgram const& command_program,
                          ::Token& token,
                          std::map<std::string, int> const& bindings,
                          std::string const& rule_name) {
    current_token_ = &token;
    bindings_ = &bindings;
    current_rule_name_ = rule_name;
    temp_bindings_.clear();
    break_requested_ = false;
    continue_requested_ = false;

    callback_.begin_rhs_transaction();
    bool committed = false;

    try {
        if (command_program.script.root_actions.empty()) {
            g_cpp_action_plan_error_count.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error("RHS C++ action plan did not produce executable actions");
        }

        execute_actions(command_program.script.root_actions);
        callback_.end_rhs_transaction(true);
        committed = true;
        g_cpp_action_plan_exec_count.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
        if (!committed) {
            callback_.end_rhs_transaction(false);
        }
        current_token_ = nullptr;
        bindings_ = nullptr;
        current_rule_name_.clear();
        temp_bindings_.clear();
        throw;
    }

    current_token_ = nullptr;
    bindings_ = nullptr;
    current_rule_name_.clear();
    temp_bindings_.clear();
}

void RhsExecutor::execute_actions(std::vector<CompiledAction const*> const& actions) {
    for (auto const* action : actions) {
        if (action == nullptr || break_requested_ || continue_requested_) {
            return;
        }
        execute_action(*action);
    }
}

void RhsExecutor::execute_actions(std::vector<CompiledAction> const& actions) {
    for (auto const& action : actions) {
        if (break_requested_ || continue_requested_) {
            return;
        }
        execute_action(action);
    }
}

void RhsExecutor::execute_action(CompiledAction const& action) {
    if (break_requested_ || continue_requested_) return;

    switch (action.type) {
        case RhsActionType::UPDATE:
            execute_update(action);
            break;
        case RhsActionType::INSERT:
            execute_insert(action);
            break;
        case RhsActionType::INSERT_LOGICAL:
            execute_insert_logical(action);
            break;
        case RhsActionType::RETRACT:
            execute_retract(action);
            break;
        case RhsActionType::HALT:
            execute_halt(action);
            break;
        case RhsActionType::SET_FOCUS:
            execute_set_focus(action);
            break;
        case RhsActionType::IF: {
            if (!action.condition) {
                throw std::runtime_error("RHS if action is missing a condition");
            }
            auto t0 = std::chrono::steady_clock::now();
            auto simple_value = evaluate_simple_rhs_condition(*action.condition);
            if (!simple_value) {
                g_expression_error_count.fetch_add(1, std::memory_order_relaxed);
                throw std::runtime_error("RHS if condition is not supported by C++ expression evaluator: "
                                         + action.condition->expression_string());
            }
            auto t1 = std::chrono::steady_clock::now();
            g_condition_eval_us.fetch_add(
                static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()),
                std::memory_order_relaxed);
            g_expression_exec_count.fetch_add(1, std::memory_order_relaxed);
            execute_actions(*simple_value ? action.then_actions : action.else_actions);
            break;
        }
        case RhsActionType::SWITCH: {
            if (!action.switch_expr) {
                throw std::runtime_error("RHS switch action is missing an expression");
            }
            auto switch_value = evaluate_simple_rhs_value(action.switch_expr->expression_string());
            if (!switch_value) {
                g_expression_error_count.fetch_add(1, std::memory_order_relaxed);
                throw std::runtime_error("RHS switch expression is not supported by C++ expression evaluator: "
                                         + action.switch_expr->expression_string());
            }
            bool matched = false;
            SwitchCase const* default_case = nullptr;
            for (auto const& switch_case : action.switch_cases) {
                if (switch_case.is_default) {
                    default_case = &switch_case;
                    continue;
                }
                if (!switch_case.value) {
                    continue;
                }
                auto case_value = evaluate_simple_rhs_value(switch_case.value->expression_string());
                if (!case_value) {
                    g_expression_error_count.fetch_add(1, std::memory_order_relaxed);
                    throw std::runtime_error("RHS switch case expression is not supported by C++ expression evaluator: "
                                             + switch_case.value->expression_string());
                }
                if (switch_value && case_value && values_equal_for_switch(*switch_value, *case_value)) {
                    execute_actions(switch_case.actions);
                    matched = true;
                    break;
                }
            }
            if (!matched && default_case != nullptr) {
                execute_actions(default_case->actions);
            }
            break;
        }
        case RhsActionType::FOR:
        {
            auto items = collect_for_items(action);
            for (auto* item : items) {
                std::map<std::string, ::Fact*> restored_bindings;
                std::vector<std::string> erased_bindings;
                auto it = temp_bindings_.find(action.iter_var);
                if (it != temp_bindings_.end()) {
                    restored_bindings.emplace(action.iter_var, it->second);
                } else {
                    erased_bindings.push_back(action.iter_var);
                }
                temp_bindings_[action.iter_var] = item;

                struct ForTempBindingRestore {
                    std::map<std::string, ::Fact*>& target;
                    std::map<std::string, ::Fact*> const& restored;
                    std::vector<std::string> const& erased;

                    ~ForTempBindingRestore() {
                        for (auto const& binding : restored) {
                            target[binding.first] = binding.second;
                        }
                        for (auto const& name : erased) {
                            target.erase(name);
                        }
                    }
                } restore{temp_bindings_, restored_bindings, erased_bindings};

                execute_actions(action.body_actions);
                if (break_requested_) {
                    break_requested_ = false;
                    break;
                }
                if (continue_requested_) {
                    continue_requested_ = false;
                }
            }
            break;
        }
        case RhsActionType::WHILE:
        {
            if (!action.condition) {
                throw std::runtime_error("RHS while action is missing a condition");
            }
            int const max_iter = action.max_iterations > 0 ? action.max_iterations : 1000;
            int iterations = 0;
            while (iterations < max_iter) {
                auto t0 = std::chrono::steady_clock::now();
                auto simple_value = evaluate_simple_rhs_condition(*action.condition);
                if (!simple_value) {
                    g_expression_error_count.fetch_add(1, std::memory_order_relaxed);
                    throw std::runtime_error("RHS while condition is not supported by C++ expression evaluator: "
                                             + action.condition->expression_string());
                }
                auto t1 = std::chrono::steady_clock::now();
                g_condition_eval_us.fetch_add(
                    static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()),
                    std::memory_order_relaxed);
                g_expression_exec_count.fetch_add(1, std::memory_order_relaxed);
                if (!*simple_value) {
                    break;
                }
                execute_actions(action.body_actions);
                if (break_requested_) {
                    break_requested_ = false;
                    break;
                }
                if (continue_requested_) {
                    continue_requested_ = false;
                }
                ++iterations;
            }
            if (iterations >= max_iter) {
                throw std::runtime_error("RHS while loop hit max iterations");
            }
            break;
        }
        case RhsActionType::BREAK:
            break_requested_ = true;
            break;
        case RhsActionType::CONTINUE:
            continue_requested_ = true;
            break;
    }
}

void RhsExecutor::execute_update(CompiledAction const& action) {
    ScopedUsTimer timer(g_update_action_us);
    auto fact = get_bound_fact(action.target_var);
    if (!fact) {
        throw std::runtime_error("RHS update target '" + action.target_var + "' not found");
    }

    bool changed = false;
    bool snapshot_taken = false;
    rulesforge::ModifiedFieldsHint changed_fields;
    auto ensure_snapshot = [&]() {
        if (!snapshot_taken) {
            callback_.track_rhs_update_snapshot(*fact);
            snapshot_taken = true;
        }
    };
    auto mark_changed = [&](std::string_view field_name) {
        changed = true;
        changed_fields.add(field_name);
    };

    for (auto const& assign : action.assignments) {
        std::string_view field_key = assign.field_key.empty() ? std::string_view(assign.field_name) : assign.field_key;
        rulesforge::InternedString interned_key(field_key);
        if (assign.has_precomputed_literal) {
            auto it = fact->fields.find(field_key);
            ConstraintValue new_value = it != fact->fields.end()
                ? coerce_numeric_assignment_to_existing_type(it->second, assign.precomputed_literal)
                : assign.precomputed_literal;
            new_value = coerce_assignment_to_declared_type(
                callback_.get_declared_field_type(fact->type, field_key),
                std::move(new_value));
            if (it != fact->fields.end()) {
                if (it->second == new_value) {
                    continue;
                }
                if (auto const* lit = std::get_if<std::string>(&new_value)) {
                    if (auto* cur = std::get_if<std::string>(&it->second)) {
                        ensure_snapshot();
                        *cur = *lit;
                        mark_changed(assign.field_name);
                        continue;
                    }
                }
                ensure_snapshot();
                it->second = std::move(new_value);
                mark_changed(assign.field_name);
            } else {
                ensure_snapshot();
                fact->fields[interned_key] = std::move(new_value);
                mark_changed(assign.field_name);
            }
            continue;
        }

        ConstraintValue value = evaluate_assignment(assign);
        auto it = fact->fields.find(field_key);
        value = coerce_assignment_to_declared_type(
            callback_.get_declared_field_type(fact->type, field_key),
            std::move(value));
        if (it != fact->fields.end()) {
            value = coerce_numeric_assignment_to_existing_type(it->second, std::move(value));
            if (it->second == value) {
                continue;
            }
            ensure_snapshot();
            it->second = std::move(value);
            mark_changed(assign.field_name);
        } else {
            ensure_snapshot();
            fact->fields[interned_key] = std::move(value);
            mark_changed(assign.field_name);
        }
    }

    if (changed) {
        callback_.propagate_modify(fact, changed_fields.overflow ? nullptr : &changed_fields);
    }
}

void RhsExecutor::execute_insert(CompiledAction const& action) {
    ScopedUsTimer timer(g_insert_action_us);
    Fact* fact = callback_.create_fact(action.target_type); // Creates via fact builder or session-managed builder
    for (auto const& assign : action.assignments) {
        std::string_view field_key = assign.field_key.empty() ? std::string_view(assign.field_name) : assign.field_key;
        rulesforge::InternedString interned_key(field_key);
        if (assign.has_precomputed_literal) {
            fact->fields[interned_key] = coerce_assignment_to_declared_type(
                callback_.get_declared_field_type(fact->type, field_key),
                assign.precomputed_literal);
        } else {
            fact->fields[interned_key] = coerce_assignment_to_declared_type(
                callback_.get_declared_field_type(fact->type, field_key),
                evaluate_assignment(assign));
        }
    }
    callback_.add_fact(fact);
}

void RhsExecutor::execute_insert_logical(CompiledAction const& action) {
    Fact* new_fact = callback_.create_fact(action.target_type);


    for (auto const& assign : action.assignments) {
        std::string_view field_key = assign.field_key.empty() ? std::string_view(assign.field_name) : assign.field_key;
        rulesforge::InternedString interned_key(field_key);
        if (assign.has_precomputed_literal) {
            new_fact->fields[interned_key] = coerce_assignment_to_declared_type(
                callback_.get_declared_field_type(new_fact->type, field_key),
                assign.precomputed_literal);
        } else {
            new_fact->fields[interned_key] = coerce_assignment_to_declared_type(
                callback_.get_declared_field_type(new_fact->type, field_key),
                evaluate_assignment(assign));
        }
    }

    callback_.logical_insert(*current_token_, new_fact);
}

void RhsExecutor::execute_retract(CompiledAction const& action) {
    ScopedUsTimer timer(g_retract_action_us);
    auto fact = get_bound_fact(action.target_var);
    if (!fact) {
        throw std::runtime_error("RHS retract target '" + action.target_var + "' not found");
    }

    callback_.retract_fact(fact);
}

void RhsExecutor::execute_halt(CompiledAction const& action) {
    callback_.halt();
}

void RhsExecutor::execute_set_focus(CompiledAction const& action) {
    callback_.set_focus(action.focus_group);
}

std::vector<::Fact*> RhsExecutor::collect_for_items(CompiledAction const& action) {
    std::vector<::Fact*> items;
    auto append_values_as_iterator_facts = [this, &items](std::vector<ConstraintValue> const& values) {
        for (auto const& cv : values) {
            Fact* iter_fact = callback_.create_fact("Iterator");
            iter_fact->fields[std::string_view("value")] = cv;
            items.push_back(iter_fact);
        }
    };

    if (!action.iter_source_list.empty()) {
        // Value list form: for $item in ($a, $b, $c)
        for (auto const& var : action.iter_source_list) {
            auto* fact = get_bound_fact(var);
            if (!fact) {
                throw std::runtime_error("RHS for source list variable '" + var + "' not found");
            }
            items.push_back(fact);
        }
    } else {
        // Field form: for $item in $var.field
        auto* source_fact = get_bound_fact(action.iter_source_var);
        if (!source_fact) {
            auto global_val = get_global_value(action.iter_source_var);
            if (!global_val) {
                throw std::runtime_error("RHS for source '" + action.iter_source_var + "' not found");
            }

            if (auto* fl = std::get_if<FactList>(&*global_val)) {
                for (auto* f : fl->facts) {
                    if (f) items.push_back(f);
                }
            } else if (auto* tl = std::get_if<std::shared_ptr<TypedList>>(&*global_val)) {
                if (*tl) append_values_as_iterator_facts((*tl)->values);
            } else if (auto* vs = std::get_if<std::shared_ptr<ValueSet>>(&*global_val)) {
                if (*vs) {
                    std::vector<ConstraintValue> values;
                    values.reserve((*vs)->values.size());
                    for (auto const& v : (*vs)->values) values.push_back(v);
                    append_values_as_iterator_facts(values);
                }
            } else {
                throw std::runtime_error("RHS for source '" + action.iter_source_var + "' is not iterable");
            }
        } else {
            auto field_val = source_fact->get_field(action.iter_source_field);
            if (!field_val) {
                throw std::runtime_error("RHS for field '" + action.iter_source_var + "."
                                         + action.iter_source_field + "' not found");
            }

            if (auto* fl = std::get_if<FactList>(&*field_val)) {
                for (auto* f : fl->facts) {
                    if (f) items.push_back(f);
                }
            } else if (auto* tl = std::get_if<std::shared_ptr<TypedList>>(&*field_val)) {
                if (*tl) append_values_as_iterator_facts((*tl)->values);
            } else {
                throw std::runtime_error("RHS for field '" + action.iter_source_var + "."
                                         + action.iter_source_field + "' is not iterable");
            }
        }
    }

    return items;
}

std::optional<ConstraintValue> RhsExecutor::evaluate_simple_rhs_value(std::string const& expression) {
    std::string value = trim_ascii(expression);
    if (value.empty()) {
        return std::nullopt;
    }
    if (has_wrapping_parentheses(value)) {
        return evaluate_simple_rhs_value(value.substr(1, value.size() - 2));
    }
    if (value == "pi") {
        return 3.14159265358979323846;
    }
    if (auto function = parse_function_call(value)) {
        std::vector<double> args;
        args.reserve(function->second.size());
        for (auto const& arg_text : function->second) {
            auto arg_value = evaluate_simple_rhs_value(arg_text);
            if (!arg_value) {
                return std::nullopt;
            }
            auto number = scalar_number(*arg_value);
            if (!number) {
                return std::nullopt;
            }
            args.push_back(*number);
        }

        auto const& name = function->first;
        if (name == "floor" && args.size() == 1) return std::floor(args[0]);
        if (name == "ceil" && args.size() == 1) return std::ceil(args[0]);
        if (name == "abs" && args.size() == 1) return std::fabs(args[0]);
        if (name == "round" && args.size() == 1) return std::round(args[0]);
        if (name == "sqrt" && args.size() == 1) return std::sqrt(args[0]);
        if (name == "sin" && args.size() == 1) return std::sin(args[0]);
        if (name == "cos" && args.size() == 1) return std::cos(args[0]);
        if (name == "tan" && args.size() == 1) return std::tan(args[0]);
        if (name == "acos" && args.size() == 1) return std::acos(args[0]);
        if (name == "asin" && args.size() == 1) return std::asin(args[0]);
        if (name == "atan" && args.size() == 1) return std::atan(args[0]);
        if (name == "log" && args.size() == 1) return std::log(args[0]);
        if (name == "exp" && args.size() == 1) return std::exp(args[0]);
        if (name == "min" && args.size() == 2) return std::min(args[0], args[1]);
        if (name == "max" && args.size() == 2) return std::max(args[0], args[1]);
        if (name == "pow" && args.size() == 2) return std::pow(args[0], args[1]);
        if (name == "fmod" && args.size() == 2) return std::fmod(args[0], args[1]);
        return std::nullopt;
    }
    if (auto arithmetic = find_top_level_arithmetic(value)) {
        auto lhs = evaluate_simple_rhs_value(value.substr(0, arithmetic->first));
        auto rhs = evaluate_simple_rhs_value(value.substr(arithmetic->first + 1));
        if (!lhs || !rhs) {
            return std::nullopt;
        }
        auto lhs_number = scalar_number(*lhs);
        auto rhs_number = scalar_number(*rhs);
        if (!lhs_number || !rhs_number) {
            return std::nullopt;
        }
        double result = 0.0;
        switch (arithmetic->second) {
            case '+': result = *lhs_number + *rhs_number; break;
            case '-': result = *lhs_number - *rhs_number; break;
            case '*': result = *lhs_number * *rhs_number; break;
            case '/':
                if (*rhs_number == 0.0) return std::nullopt;
                result = *lhs_number / *rhs_number;
                break;
            default: return std::nullopt;
        }
        if (std::holds_alternative<int64_t>(*lhs)
            && std::holds_alternative<int64_t>(*rhs)
            && arithmetic->second != '/') {
            return static_cast<int64_t>(result);
        }
        return result;
    }
    if ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')) {
        return value.substr(1, value.size() - 2);
    }
    if (value == "true") return true;
    if (value == "false") return false;
    if (value == "nil" || value == "null") return NilValue{};
    if (value.front() == '$') {
        return resolve_variable(value);
    }

    int64_t int_value = 0;
    auto const* begin = value.data();
    auto const* end = value.data() + value.size();
    auto [int_ptr, int_ec] = std::from_chars(begin, end, int_value);
    if (int_ec == std::errc{} && int_ptr == end) {
        return int_value;
    }

    char* parse_end = nullptr;
    double double_value = std::strtod(value.c_str(), &parse_end);
    if (parse_end != nullptr && *parse_end == '\0') {
        return double_value;
    }
    return std::nullopt;
}

std::optional<bool> RhsExecutor::evaluate_simple_rhs_condition(rulesforge::ExpressionDescriptor const& expr) {
    std::string const& expression = expr.expression_string();
    auto comparison = find_top_level_compare(expression);
    if (!comparison) {
        auto value = evaluate_simple_rhs_value(expression);
        if (!value) return std::nullopt;
        return is_truthy(*value);
    }

    auto const [op_pos, op] = *comparison;
    auto lhs = evaluate_simple_rhs_value(expression.substr(0, op_pos));
    auto rhs = evaluate_simple_rhs_value(expression.substr(op_pos + op.size()));
    if (!lhs || !rhs) {
        return std::nullopt;
    }
    return compare_scalar_values(op, *lhs, *rhs);
}

ConstraintValue RhsExecutor::evaluate_assignment(FieldAssignment const& assign) {
    if (assign.has_precomputed_literal) {
        return assign.precomputed_literal;
    }

    ScopedUsTimer timer(g_eval_assignment_us);
    switch (assign.type) {
        case RhsValueType::NUMERIC:
            if (assign.numeric_expr) {
                if (auto simple_value = evaluate_simple_rhs_value(assign.numeric_expr->expression_string())) {
                    return *simple_value;
                }
                g_expression_error_count.fetch_add(1, std::memory_order_relaxed);
                throw std::runtime_error("RHS numeric expression is not supported by C++ expression evaluator: "
                                         + assign.numeric_expr->expression_string());
            }
            throw std::runtime_error("RHS numeric assignment is missing an expression");

        case RhsValueType::STRING:
            return assign.string_literal;

        case RhsValueType::BOOLEAN:
            return assign.string_literal == "true";

        case RhsValueType::VAR_REF: {
            // Handle $var or $var.field
            std::string const& ref = assign.var_ref;
            size_t dot_pos = ref.find('.');
            if (dot_pos != std::string::npos) {
                return resolve_variable(ref);
            } else {
                if (auto fact = get_bound_fact(ref)) {
                    return static_cast<int64_t>(fact->id);
                }
                if (auto global_val = get_global_value(ref)) {
                    return *global_val;
                }
                throw std::runtime_error("RHS variable '" + ref + "' not found");
            }
        }
    }
    return NilValue{};
}

ConstraintValue RhsExecutor::resolve_variable(std::string const& var_name) {
    // Handle $var.field format
    size_t dot_pos = var_name.find('.');
    std::string base_var = (dot_pos != std::string::npos) ? var_name.substr(0, dot_pos) : var_name;
    std::string field_name = (dot_pos != std::string::npos) ? var_name.substr(dot_pos + 1) : "this";

    auto fact = get_bound_fact(base_var);
    if (fact) {
        if (field_name == "this") {
            return NilValue{}; // Handle properly if needed
        }

        auto field_val = fact->get_field(field_name);
        if (!field_val) {
            throw std::runtime_error("RHS variable field '" + base_var + "." + field_name + "' not found");
        }

        return *field_val;
    }

    auto global_val = get_global_value(base_var);
    if (!global_val) {
        throw std::runtime_error("RHS variable '" + base_var + "' not found");
    }

    auto resolved = resolve_global_field(*global_val, field_name);
    if (!resolved) {
        throw std::runtime_error("RHS global field '" + base_var + "." + field_name + "' not found");
    }

    return *resolved;
}

::Fact* RhsExecutor::get_bound_fact(std::string const& var_name) {
    if (var_name.empty()) return nullptr;

    // Check temporary bindings (FOR loop variables) first
    auto it_temp = temp_bindings_.find(var_name);
    if (it_temp != temp_bindings_.end()) {
        return it_temp->second;
    }

    // Check regular bindings ($var)
    if (var_name[0] == '$' && bindings_ && current_token_) {
        auto it = bindings_->find(var_name);
        if (it != bindings_->end()) {
            return current_token_->get_fact_at_depth(it->second);
        }
    }

    return nullptr;
}

std::optional<ConstraintValue> RhsExecutor::get_global_value(std::string const& name) const {
    auto const* session = dynamic_cast<StatefulSession const*>(&callback_);
    if (!session) return std::nullopt;
    if (!name.empty() && name[0] == '$') {
        return session->get_global(name.substr(1));
    }
    return session->get_global(name);
}

} // namespace rulesforge
