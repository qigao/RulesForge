#include "execution/rhs_executor.hpp"
#include "script/script_invoker.hpp"
#include "runtime/i_network_callback.hpp"
#include "execution/rhs_backend_plan.hpp"
#include "runtime/stateful_session.hpp"
#include "execution/scalar_function.hpp"
#include "text/scalar_text.hpp"
#include "expression_descriptor.hpp"
#include "diagnostics/logging_control.hpp"
#include "diagnostics/exceptions.hpp"

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
            constexpr double max_i64_exclusive = -min_i64;
            double integer_part = 0.0;
            double const fractional_part = std::modf(*new_double, &integer_part);
            if (std::isfinite(*new_double) &&
                std::fpclassify(fractional_part) == FP_ZERO &&
                *new_double >= min_i64 &&
                *new_double < max_i64_exclusive) {
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
                constexpr double max_i64_exclusive = -min_i64;
                double integer_part = 0.0;
                double const fractional_part = std::modf(*new_double, &integer_part);
                if (std::isfinite(*new_double) &&
                    std::fpclassify(fractional_part) == FP_ZERO &&
                    *new_double >= min_i64 &&
                    *new_double < max_i64_exclusive) {
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

bool exact_double_equal(double lhs, double rhs) {
    return !std::isnan(lhs) && !std::isnan(rhs) && !std::islessgreater(lhs, rhs);
}

bool values_equal_for_switch(ConstraintValue const& lhs, ConstraintValue const& rhs) {
    if (std::holds_alternative<double>(lhs) && std::holds_alternative<int64_t>(rhs)) {
        return exact_double_equal(std::get<double>(lhs), static_cast<double>(std::get<int64_t>(rhs)));
    }
    if (std::holds_alternative<int64_t>(lhs) && std::holds_alternative<double>(rhs)) {
        return exact_double_equal(static_cast<double>(std::get<int64_t>(lhs)), std::get<double>(rhs));
    }
    ScalarTextView const lhs_text(lhs);
    ScalarTextView const rhs_text(rhs);
    if (lhs_text && rhs_text) return *lhs_text == *rhs_text;
    return lhs == rhs;
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
    script_results_.clear();
    if (scripts_) scripts_->begin_rhs();
    struct ScriptFrame {
        ScriptInvoker *scripts;
        std::map<std::string, ConstraintValue>& results;
        ~ScriptFrame() { results.clear(); if (scripts) scripts->end_rhs(); }
    } script_frame{scripts_, script_results_};
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
    struct ResultScope {
        std::map<std::string, ConstraintValue>& values;
        std::map<std::string, ConstraintValue> outer;
        ~ResultScope() { values.swap(outer); }
    } result_scope{script_results_, script_results_};
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
        case RhsActionType::CALL: {
            if (!scripts_ || action.script_binding_index == SIZE_MAX)
                throw std::runtime_error("Script binding unavailable: " + action.script_binding);
            std::vector<ConstraintValue> arguments;
            arguments.reserve(action.script_arguments.size());
            for (auto const& arg : action.script_arguments) arguments.push_back(evaluate_assignment(arg));
            auto value = scripts_->invoke(action.script_binding_index, arguments);
            if (!script_results_.emplace(action.script_result_var, std::move(value)).second)
                throw std::runtime_error("Duplicate script result variable");
            break;
        }
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
                throw std::runtime_error("RHS if condition has no scalar value: "
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
            auto switch_value = evaluate_scalar_value(*action.switch_expr);
            if (!switch_value) {
                g_expression_error_count.fetch_add(1, std::memory_order_relaxed);
                throw std::runtime_error("RHS switch expression has no scalar value: "
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
                auto case_value = evaluate_scalar_value(*switch_case.value);
                if (!case_value) {
                    g_expression_error_count.fetch_add(1, std::memory_order_relaxed);
                    throw std::runtime_error("RHS switch case expression has no scalar value: "
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
            if (action.max_iterations <= 0) {
                throw std::runtime_error("RHS while loop requires positive max iterations");
            }
            int iterations = 0;
            while (true) {
                auto t0 = std::chrono::steady_clock::now();
                auto simple_value = evaluate_simple_rhs_condition(*action.condition);
                if (!simple_value) {
                    g_expression_error_count.fetch_add(1, std::memory_order_relaxed);
                    throw std::runtime_error("RHS while condition has no scalar value: "
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
                if (iterations >= action.max_iterations) {
                    throw std::runtime_error("RHS while loop hit max iterations");
                }
                ++iterations;
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


std::optional<ConstraintValue> RhsExecutor::evaluate_scalar_value(ExpressionDescriptor const& expression) {
    try {
        return expression.evaluate([](void* owner, ExpressionDescriptor::Variable const& access)
            -> std::optional<ConstraintValue> {
            return static_cast<RhsExecutor*>(owner)->resolve_variable(access);
        }, this);
    } catch (std::logic_error const& error) {
        // Internal scalar type/domain errors become the existing RHS runtime
        // error contract. The transaction owner still performs the rollback.
        throw ReteExecutionException(error.what(), current_rule_name_);
    }
}

std::optional<bool> RhsExecutor::evaluate_simple_rhs_condition(ExpressionDescriptor const& expression) {
    auto value = evaluate_scalar_value(expression);
    if (!value) return std::nullopt;
    return ExpressionDescriptor::truthy(*value);
}

ConstraintValue RhsExecutor::evaluate_assignment(FieldAssignment const& assign) {
    if (assign.has_precomputed_literal) {
        return assign.precomputed_literal;
    }

    ScopedUsTimer timer(g_eval_assignment_us);
    switch (assign.type) {
        case RhsValueType::NUMERIC:
            if (assign.numeric_expr) {
                if (auto simple_value = evaluate_scalar_value(*assign.numeric_expr)) {
                    return *simple_value;
                }
                g_expression_error_count.fetch_add(1, std::memory_order_relaxed);
                throw std::runtime_error("RHS numeric expression has no scalar value: "
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
                if (auto it = script_results_.find(ref); it != script_results_.end()) return it->second;
                throw std::runtime_error("RHS variable '" + ref + "' not found");
            }
        }
    }
    return NilValue{};
}

ConstraintValue RhsExecutor::resolve_variable(ExpressionDescriptor::Variable const& access) {
    auto const& base = access.binding;
    if (auto it = script_results_.find(base); it != script_results_.end()) {
        if (access.field == "this") return it->second;
        auto value = resolve_global_field(it->second, access.field);
        if (!value) throw std::runtime_error("Script result field not found: " + access.name);
        return *value;
    }
    if (auto* fact = get_bound_fact(base)) {
        if (access.field == "this") return NilValue{};
        auto value = fact->get_field(access.path);
        if (!value) throw std::runtime_error("RHS variable field not found: " + access.name);
        return *value;
    }
    auto global = get_global_value(base);
    if (!global) throw std::runtime_error("RHS variable not found: " + base);
    auto value = resolve_global_field(*global, access.field);
    if (!value) throw std::runtime_error("RHS global field not found: " + access.name);
    return *value;
}

ConstraintValue RhsExecutor::resolve_variable(std::string const& var_name) {
    // Handle $var.field format
    size_t dot_pos = var_name.find('.');
    std::string base_var = (dot_pos != std::string::npos) ? var_name.substr(0, dot_pos) : var_name;
    std::string field_name = (dot_pos != std::string::npos) ? var_name.substr(dot_pos + 1) : "this";

    if (auto it = script_results_.find(base_var); it != script_results_.end()) {
        if (dot_pos == std::string::npos) return it->second;
        auto value = resolve_global_field(it->second, field_name);
        if (!value) throw std::runtime_error("Script result field not found: " + var_name);
        return *value;
    }

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
