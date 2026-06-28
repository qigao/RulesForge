#include "engine/rhs_executor.hpp"
#include "engine/i_network_callback.hpp"
#include "engine/rhs_backend_plan.hpp"
#include "engine/stateful_session.hpp"
#include "engine/turboscript_rhs_adapter.hpp"
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
#include <mutex>
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
std::atomic<uint64_t> g_eval_assignment_us{0};
std::atomic<uint64_t> g_condition_eval_us{0};
std::atomic<uint64_t> g_update_action_us{0};
std::atomic<uint64_t> g_insert_action_us{0};
std::atomic<uint64_t> g_retract_action_us{0};
std::atomic<uint64_t> g_turboscript_command_exec_count{0};
std::atomic<uint64_t> g_turboscript_command_error_count{0};
std::atomic<uint64_t> g_turboscript_expression_exec_count{0};
std::atomic<uint64_t> g_turboscript_expression_error_count{0};
std::atomic<uint64_t> g_turboscript_expression_non_scalar_error_count{0};

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
        case FT_Boolean:
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

std::string quote_turboscript_string(std::string const& value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            result.push_back('\\');
        }
        result.push_back(ch);
    }
    result.push_back('"');
    return result;
}

bool is_truthy(ConstraintValue const& value) {
    if (auto const* d = std::get_if<double>(&value)) return *d != 0.0;
    if (auto const* i = std::get_if<int64_t>(&value)) return *i != 0;
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
    g_turboscript_command_exec_count.store(0, std::memory_order_relaxed);
    g_turboscript_command_error_count.store(0, std::memory_order_relaxed);
    g_turboscript_expression_exec_count.store(0, std::memory_order_relaxed);
    g_turboscript_expression_error_count.store(0, std::memory_order_relaxed);
    g_turboscript_expression_non_scalar_error_count.store(0, std::memory_order_relaxed);
}

Stats get_stats() {
    Stats s;
    s.evaluate_assignment_us = g_eval_assignment_us.load(std::memory_order_relaxed);
    s.condition_eval_us = g_condition_eval_us.load(std::memory_order_relaxed);
    s.update_action_us = g_update_action_us.load(std::memory_order_relaxed);
    s.insert_action_us = g_insert_action_us.load(std::memory_order_relaxed);
    s.retract_action_us = g_retract_action_us.load(std::memory_order_relaxed);
    s.turboscript_command_exec_count = g_turboscript_command_exec_count.load(std::memory_order_relaxed);
    s.turboscript_command_error_count = g_turboscript_command_error_count.load(std::memory_order_relaxed);
    s.turboscript_expression_exec_count = g_turboscript_expression_exec_count.load(std::memory_order_relaxed);
    s.turboscript_expression_error_count = g_turboscript_expression_error_count.load(std::memory_order_relaxed);
    s.turboscript_expression_non_scalar_error_count =
        g_turboscript_expression_non_scalar_error_count.load(std::memory_order_relaxed);
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
        if (command_program.script.script.empty()) {
            g_turboscript_command_error_count.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error("TurboScript RHS command backend did not produce an executable program");
        }

        auto make_eval_condition = [this](TurboScriptRhsCommandScript const& script) {
            return [this, &script](std::size_t condition_index) -> bool {
                if (condition_index >= script.condition_actions.size()) {
                    return false;
                }
                CompiledAction const* condition_action = script.condition_actions[condition_index];
                if (!condition_action) {
                    return false;
                }
                std::size_t const switch_case_index =
                    condition_index < script.condition_switch_case_indices.size()
                        ? script.condition_switch_case_indices[condition_index]
                        : static_cast<std::size_t>(-1);

                auto t0 = std::chrono::steady_clock::now();
                auto resolver = [this](std::string const& var) { return resolve_variable(var); };
                if (switch_case_index != static_cast<std::size_t>(-1)) {
                    if (!condition_action->switch_expr
                        || switch_case_index >= condition_action->switch_cases.size()
                        || !condition_action->switch_cases[switch_case_index].value) {
                        return false;
                    }
                    auto switch_value = evaluate_turboscript_expression(*condition_action->switch_expr, resolver);
                    auto case_value = evaluate_turboscript_expression(
                        *condition_action->switch_cases[switch_case_index].value, resolver);
                    auto t1 = std::chrono::steady_clock::now();
                    g_condition_eval_us.fetch_add(
                        static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()),
                        std::memory_order_relaxed);
                    return switch_value && case_value && values_equal_for_switch(*switch_value, *case_value);
                }

                if (!condition_action->condition) {
                    return false;
                }
                if (auto turboscript_value = evaluate_turboscript_expression(*condition_action->condition, resolver)) {
                    auto t1 = std::chrono::steady_clock::now();
                    g_condition_eval_us.fetch_add(
                        static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()),
                        std::memory_order_relaxed);
                    return is_truthy(*turboscript_value);
                }

                auto t1 = std::chrono::steady_clock::now();
                g_condition_eval_us.fetch_add(
                    static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()),
                    std::memory_order_relaxed);
                return false;
            };
        };

        auto run_program = [&](RhsCompiledCommandProgram const& program,
                               std::vector<TurboScriptRhsCommandEvent>& events,
                               std::string* error_out) -> bool {
            events.clear();
            if (program.script.script.empty()) {
                return true;
            }
            if (!program.program) {
                if (error_out) *error_out = "turboscript_program_unavailable";
                return false;
            }

            auto eval_condition = make_eval_condition(program.script);
            std::lock_guard<std::mutex> lock(program.execution_mutex);
            return program.program->execute(events, eval_condition, error_out);
        };

        std::vector<TurboScriptRhsCommandEvent> command_events;
        std::string adapter_reason;
        if (!run_program(command_program, command_events, &adapter_reason)) {
            g_turboscript_command_error_count.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error("TurboScript RHS command execute failed: " + adapter_reason);
        }
        g_turboscript_command_exec_count.fetch_add(1, std::memory_order_relaxed);

        std::function<void(RhsCompiledCommandProgram const&, std::vector<TurboScriptRhsCommandEvent> const&)>
            execute_events = [&](RhsCompiledCommandProgram const& program,
                                 std::vector<TurboScriptRhsCommandEvent> const& events) {
            TurboScriptRhsCommandScript const& script = program.script;
            for (auto const& event : events) {
                if (break_requested_ || continue_requested_) {
                    return;
                }
                if (event.type == TurboScriptRhsCommandEventType::Break) {
                    break_requested_ = true;
                    continue;
                }
                if (event.type == TurboScriptRhsCommandEventType::Continue) {
                    continue_requested_ = true;
                    continue;
                }

                if (event.type == TurboScriptRhsCommandEventType::ForEach) {
                    if (event.action_index >= script.for_actions.size()
                        || script.for_actions[event.action_index] == nullptr) {
                        throw std::runtime_error("TurboScript RHS for index out of range");
                    }
                    if (event.action_index >= program.for_body_programs.size()
                        || !program.for_body_programs[event.action_index]) {
                        throw std::runtime_error("TurboScript RHS for body program unavailable");
                    }

                    CompiledAction const& for_action = *script.for_actions[event.action_index];
                    RhsCompiledCommandProgram const& body_program =
                        *program.for_body_programs[event.action_index];

                    auto items = collect_for_items(for_action);
                    for (auto* item : items) {
                        std::map<std::string, ::Fact*> restored_bindings;
                        std::vector<std::string> erased_bindings;
                        auto it = temp_bindings_.find(for_action.iter_var);
                        if (it != temp_bindings_.end()) {
                            restored_bindings.emplace(for_action.iter_var, it->second);
                        } else {
                            erased_bindings.push_back(for_action.iter_var);
                        }
                        temp_bindings_[for_action.iter_var] = item;

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

                        std::vector<TurboScriptRhsCommandEvent> body_events;
                        std::string body_reason;
                        if (!run_program(body_program, body_events, &body_reason)) {
                            throw std::runtime_error("TurboScript RHS for body execute failed: " + body_reason);
                        }
                        execute_events(body_program, body_events);

                        if (break_requested_) {
                            break_requested_ = false;
                            break;
                        }
                        if (continue_requested_) {
                            continue_requested_ = false;
                        }
                    }
                    continue;
                }

                if (event.type == TurboScriptRhsCommandEventType::WhileLoop) {
                    if (event.action_index >= script.while_actions.size()
                        || script.while_actions[event.action_index] == nullptr) {
                        throw std::runtime_error("TurboScript RHS while index out of range");
                    }
                    if (event.action_index >= program.while_body_programs.size()
                        || !program.while_body_programs[event.action_index]) {
                        throw std::runtime_error("TurboScript RHS while body program unavailable");
                    }

                    CompiledAction const& while_action = *script.while_actions[event.action_index];
                    if (!while_action.condition) {
                        throw std::runtime_error("TurboScript RHS while without condition");
                    }
                    RhsCompiledCommandProgram const& body_program =
                        *program.while_body_programs[event.action_index];

                    int const max_iter = while_action.max_iterations > 0 ? while_action.max_iterations : 1000;
                    int iterations = 0;
                    while (iterations < max_iter) {
                        auto t0 = std::chrono::steady_clock::now();
                        auto resolver = [this](std::string const& var) { return resolve_variable(var); };
                        auto result_val = evaluate_turboscript_expression(*while_action.condition, resolver);
                        auto t1 = std::chrono::steady_clock::now();
                        g_condition_eval_us.fetch_add(
                            static_cast<uint64_t>(
                                std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()),
                            std::memory_order_relaxed);

                        if (!result_val || !is_truthy(*result_val)) break;

                        std::vector<TurboScriptRhsCommandEvent> body_events;
                        std::string body_reason;
                        if (!run_program(body_program, body_events, &body_reason)) {
                            throw std::runtime_error("TurboScript RHS while body execute failed: " + body_reason);
                        }
                        execute_events(body_program, body_events);

                        if (break_requested_) {
                            break_requested_ = false;
                            break;
                        }
                        if (continue_requested_) {
                            continue_requested_ = false;
                        }

                        iterations++;
                    }

                    if (iterations >= max_iter) {
                        throw std::runtime_error("TurboScript RHS while loop hit max iterations");
                    }
                    continue;
                }

                std::size_t const command_index = event.action_index;
                if (command_index >= script.command_actions.size()
                    || script.command_actions[command_index] == nullptr) {
                    throw std::runtime_error("TurboScript RHS command index out of range");
                }

                std::map<std::string, ::Fact*> restored_bindings;
                std::vector<std::string> erased_bindings;
                for (auto const& binding : event.temp_bindings) {
                    auto it = temp_bindings_.find(binding.first);
                    if (it != temp_bindings_.end()) {
                        restored_bindings.emplace(binding.first, it->second);
                    } else {
                        erased_bindings.push_back(binding.first);
                    }
                    temp_bindings_[binding.first] = binding.second;
                }

                struct TempBindingRestore {
                    std::map<std::string, ::Fact*>& target;
                    std::map<std::string, ::Fact*> const& restored;
                    std::vector<std::string> const& erased;

                    ~TempBindingRestore() {
                        for (auto const& binding : restored) {
                            target[binding.first] = binding.second;
                        }
                        for (auto const& name : erased) {
                            target.erase(name);
                        }
                    }
                } restore{temp_bindings_, restored_bindings, erased_bindings};

                execute_action(*script.command_actions[command_index]);
            }
        };

        execute_events(command_program, command_events);
        callback_.end_rhs_transaction(true);
        committed = true;
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
        case RhsActionType::INVOKE:
            execute_invoke(action);
            break;
        case RhsActionType::IF:
        case RhsActionType::FOR:
        case RhsActionType::WHILE:
        case RhsActionType::SWITCH:
        case RhsActionType::BREAK:
        case RhsActionType::CONTINUE:
            throw std::runtime_error("RHS control-flow action reached command event executor");
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

void RhsExecutor::execute_invoke(CompiledAction const& action) {
    (void)invoke_native_function(action.invoke_function, action.invoke_args);
}

std::vector<::Fact*> RhsExecutor::collect_for_items(CompiledAction const& action) {
    std::vector<::Fact*> items;
    auto append_values_as_iterator_facts = [this, &items](std::vector<ConstraintValue> const& values) {
        for (auto const& cv : values) {
            Fact* iter_fact = callback_.create_fact("Iterator");
            std::visit([&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, int64_t> || std::is_same_v<T, double>) {
                    iter_fact->fields[std::string_view("value")] = v;
                }
            }, cv);
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

ConstraintValue RhsExecutor::evaluate_assignment(FieldAssignment const& assign) {
    if (assign.has_precomputed_literal) {
        return assign.precomputed_literal;
    }

    ScopedUsTimer timer(g_eval_assignment_us);
    switch (assign.type) {
        case RhsValueType::NUMERIC:
            if (assign.numeric_expr) {
                auto resolver = [this](std::string const& var) { return resolve_variable(var); };
                return *evaluate_turboscript_expression(*assign.numeric_expr, resolver);
            }
            throw std::runtime_error("RHS numeric assignment is missing a TurboScript expression program");

        case RhsValueType::STRING:
            return *evaluate_turboscript_expression(
                quote_turboscript_string(assign.string_literal),
                {},
                [](std::string const&) -> ConstraintValue { return NilValue{}; });

        case RhsValueType::BOOLEAN:
            return *evaluate_turboscript_expression(
                assign.string_literal == "true" ? "1" : "0",
                {},
                [](std::string const&) -> ConstraintValue { return NilValue{}; });

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
        case RhsValueType::NATIVE_CALL:
            return invoke_native_function(assign.native_call_name, assign.native_call_args);
    }
    return NilValue{};
}

std::optional<ConstraintValue> RhsExecutor::evaluate_turboscript_expression(
    rulesforge::ExpressionDescriptor const& expr,
    std::function<ConstraintValue(std::string const&)> const& resolver) {
    return evaluate_turboscript_expression(expr.expression_string(), expr.variables(), resolver);
}

std::optional<ConstraintValue> RhsExecutor::evaluate_turboscript_expression(
    std::string const& expression,
    std::vector<std::string> const& variables,
    std::function<ConstraintValue(std::string const&)> const& resolver) {
    std::unordered_map<std::string, ConstraintValue> resolved_values;
    resolved_values.reserve(variables.size());
    for (auto const& variable : variables) {
        ConstraintValue value = resolver(variable);
        if (!std::holds_alternative<int64_t>(value)
            && !std::holds_alternative<double>(value)
            && !std::holds_alternative<std::string>(value)) {
            g_turboscript_expression_non_scalar_error_count.fetch_add(1, std::memory_order_relaxed);
            g_turboscript_expression_error_count.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error("TurboScript expression variable is non-scalar: " + variable);
        }
        resolved_values.emplace(variable, std::move(value));
    }
    auto cached_resolver = [&resolved_values](std::string const& variable) -> ConstraintValue {
        auto it = resolved_values.find(variable);
        if (it == resolved_values.end()) {
            return NilValue{};
        }
        return it->second;
    };

    auto cache_it = turboscript_expression_cache_.find(expression);
    if (cache_it == turboscript_expression_cache_.end()) {
        std::string compile_error;
        auto program = TurboScriptExpressionProgram::compile(expression, variables, &compile_error);
        if (!program) {
            g_turboscript_expression_error_count.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error("TurboScript expression compile failed: " + compile_error);
        }
        cache_it = turboscript_expression_cache_.emplace(expression, std::move(program)).first;
    }

    ConstraintValue value;
    std::string exec_error;
    if (!cache_it->second->execute(cached_resolver, value, &exec_error)) {
        g_turboscript_expression_error_count.fetch_add(1, std::memory_order_relaxed);
        throw std::runtime_error("TurboScript expression execute failed: " + exec_error);
    }
    g_turboscript_expression_exec_count.fetch_add(1, std::memory_order_relaxed);
    return value;
}

ConstraintValue RhsExecutor::invoke_native_function(std::string const& function_name,
                                                    std::vector<FieldAssignment> const& args) {
    auto* session = dynamic_cast<StatefulSession*>(&callback_);
    if (!session) {
        throw std::runtime_error("RHS native call '" + function_name + "' requires StatefulSession callback");
    }

    auto kb = session->get_knowledge_base();
    if (!kb) {
        throw std::runtime_error("RHS native call '" + function_name + "' failed: session has no knowledge base");
    }

    auto const& native_functions = kb->get_native_functions();
    auto it = native_functions.find(function_name);
    if (it == native_functions.end()) {
        throw std::runtime_error("RHS native call '" + function_name + "' is not registered");
    }
    if (it->second.callback == nullptr) {
        throw std::runtime_error("RHS native call '" + function_name + "' has no callback");
    }

    std::vector<std::string> arg_storage;
    std::vector<char const*> argv;
    arg_storage.reserve(args.size());
    argv.reserve(args.size());
    for (auto const& arg : args) {
        arg_storage.push_back(to_string(evaluate_assignment(arg)));
        argv.push_back(arg_storage.back().c_str());
    }

    char* out_result = nullptr;
    int status = it->second.callback(
        it->second.user_data,
        static_cast<int>(argv.size()),
        argv.empty() ? nullptr : argv.data(),
        &out_result);
    if (status != 0) {
        if (out_result) std::free(out_result);
        throw std::runtime_error("RHS native call '" + function_name + "' failed with status "
                                 + std::to_string(status));
    }

    ConstraintValue parsed = parse_native_result(out_result);
    if (out_result) std::free(out_result);
    return parsed;
}

ConstraintValue RhsExecutor::parse_native_result(char const* out_result) const {
    if (!out_result) return NilValue{};

    std::string raw = trim_ascii(out_result);
    if (raw.empty()) return NilValue{};

    if (raw == "true" || raw == "TRUE") return int64_t(1);
    if (raw == "false" || raw == "FALSE") return int64_t(0);
    if (raw == "null" || raw == "nil" || raw == "NULL" || raw == "NIL") return NilValue{};

    int64_t iv = 0;
    auto [iptr, iec] = std::from_chars(raw.data(), raw.data() + raw.size(), iv);
    if (iec == std::errc() && iptr == raw.data() + raw.size()) {
        return iv;
    }

    char* end_ptr = nullptr;
    double dv = std::strtod(raw.c_str(), &end_ptr);
    if (end_ptr == raw.c_str() + raw.size()) {
        return dv;
    }

    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        return raw.substr(1, raw.size() - 2);
    }
    return raw;
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
