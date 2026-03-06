#include "engine/rhs_executor.hpp"
#include "engine/i_network_callback.hpp"
#include "engine/stateful_session.hpp"
#include "parser/expression_evaluator.hpp"
#include "core/logging_control.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace rulesforge {
namespace {
std::atomic<uint64_t> g_eval_assignment_us{0};
std::atomic<uint64_t> g_condition_eval_us{0};
std::atomic<uint64_t> g_update_action_us{0};
std::atomic<uint64_t> g_insert_action_us{0};
std::atomic<uint64_t> g_retract_action_us{0};

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

double constraint_value_to_double(ConstraintValue const& value) {
    return std::visit([](auto&& arg) -> double {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int64_t>) {
            return static_cast<double>(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            return arg;
        } else if constexpr (std::is_same_v<T, std::string>) {
            try { return std::stod(arg); } catch (...) { return 0.0; }
        }
        return 0.0;
    }, value);
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
}  // namespace

namespace rhs_prof {
void reset_stats() {
    g_eval_assignment_us.store(0, std::memory_order_relaxed);
    g_condition_eval_us.store(0, std::memory_order_relaxed);
    g_update_action_us.store(0, std::memory_order_relaxed);
    g_insert_action_us.store(0, std::memory_order_relaxed);
    g_retract_action_us.store(0, std::memory_order_relaxed);
}

Stats get_stats() {
    Stats s;
    s.evaluate_assignment_us = g_eval_assignment_us.load(std::memory_order_relaxed);
    s.condition_eval_us = g_condition_eval_us.load(std::memory_order_relaxed);
    s.update_action_us = g_update_action_us.load(std::memory_order_relaxed);
    s.insert_action_us = g_insert_action_us.load(std::memory_order_relaxed);
    s.retract_action_us = g_retract_action_us.load(std::memory_order_relaxed);
    return s;
}
}  // namespace rhs_prof

RhsExecutor::RhsExecutor(INetworkCallback& callback)
    : callback_(callback) {}

RhsExecutor::~RhsExecutor() = default;

void RhsExecutor::execute(std::vector<CompiledAction> const& actions,
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
        for (auto const& action : actions) {
            execute_action(action);
        }
        callback_.end_rhs_transaction(true);
        committed = true;
    } catch (...) {
        if (!committed) {
            callback_.end_rhs_transaction(false);
        }
        throw;
    }

    current_token_ = nullptr;
    bindings_ = nullptr;
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
            execute_if(action);
            break;
        case RhsActionType::FOR:
            execute_for(action);
            break;
        case RhsActionType::WHILE:
            execute_while(action);
            break;
        case RhsActionType::SWITCH:
            execute_switch(action);
            break;
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
        logw("RhsExecutor: UPDATE target '{}' not found", action.target_var);
        return;
    }

    bool changed = false;
    rulesforge::ModifiedFieldsHint changed_fields;
    for (auto const& assign : action.assignments) {
        std::string_view field_key = assign.field_key.empty() ? std::string_view(assign.field_name) : assign.field_key;
        rulesforge::InternedString interned_key(field_key);
        if (assign.has_precomputed_literal) {
            auto it = fact->fields.find(field_key);
            if (it != fact->fields.end()) {
                if (it->second == assign.precomputed_literal) {
                    continue;
                }
                if (auto const* lit = std::get_if<std::string>(&assign.precomputed_literal)) {
                    if (auto* cur = std::get_if<std::string>(&it->second)) {
                        *cur = *lit;
                        changed = true;
                        changed_fields.add(assign.field_name);
                        continue;
                    }
                }
                it->second = assign.precomputed_literal;
                changed = true;
                changed_fields.add(assign.field_name);
            } else {
                fact->fields[interned_key] = assign.precomputed_literal;
                changed = true;
                changed_fields.add(assign.field_name);
            }
            continue;
        }

        ConstraintValue value = evaluate_assignment(assign);
        auto it = fact->fields.find(field_key);
        if (it != fact->fields.end()) {
            if (it->second == value) {
                continue;
            }
            it->second = std::move(value);
            changed = true;
            changed_fields.add(assign.field_name);
        } else {
            fact->fields[interned_key] = std::move(value);
            changed = true;
            changed_fields.add(assign.field_name);
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
            fact->fields[interned_key] = assign.precomputed_literal;
        } else {
            fact->fields[interned_key] = evaluate_assignment(assign);
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
            new_fact->fields[interned_key] = assign.precomputed_literal;
        } else {
            new_fact->fields[interned_key] = evaluate_assignment(assign);
        }
    }

    callback_.logical_insert(*current_token_, new_fact);
}

void RhsExecutor::execute_retract(CompiledAction const& action) {
    ScopedUsTimer timer(g_retract_action_us);
    auto fact = get_bound_fact(action.target_var);
    if (!fact) {
        logw("RhsExecutor: RETRACT target '{}' not found", action.target_var);
        return;
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

void RhsExecutor::execute_if(CompiledAction const& action) {
    if (!action.condition) {
        logw("RhsExecutor: IF without condition");
        return;
    }

    auto t0 = std::chrono::steady_clock::now();
    double result = action.condition->evaluate([this](std::string const& var) { return resolve_variable(var); });
    auto t1 = std::chrono::steady_clock::now();
    g_condition_eval_us.fetch_add(
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()),
        std::memory_order_relaxed);


    if (result != 0.0) {
        for (auto const& then_action : action.then_actions) {
            execute_action(then_action);
        }
    } else {
        for (auto const& else_action : action.else_actions) {
            execute_action(else_action);
        }
    }
}

void RhsExecutor::execute_for(CompiledAction const& action) {
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
            if (fact) items.push_back(fact);
        }
    } else {
        // Field form: for $item in $var.field
        auto* source_fact = get_bound_fact(action.iter_source_var);
        if (!source_fact) {
            auto global_val = get_global_value(action.iter_source_var);
            if (!global_val) {
                logw("RhsExecutor: FOR source '{}' not found", action.iter_source_var);
                return;
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
                logw("RhsExecutor: FOR source '{}' is not iterable", action.iter_source_var);
                return;
            }
        } else {
            auto field_val = source_fact->get_field(action.iter_source_field);
            if (!field_val) {
                logw("RhsExecutor: FOR field '{}' not found on '{}'", action.iter_source_field, action.iter_source_var);
                return;
            }

            if (auto* fl = std::get_if<FactList>(&*field_val)) {
                for (auto* f : fl->facts) {
                    if (f) items.push_back(f);
                }
            } else if (auto* tl = std::get_if<std::shared_ptr<TypedList>>(&*field_val)) {
                if (*tl) append_values_as_iterator_facts((*tl)->values);
            } else {
                logw("RhsExecutor: FOR field '{}' is not iterable", action.iter_source_field);
                return;
            }
        }
    }

    for (auto* item : items) {
        temp_bindings_[action.iter_var] = item;

        for (auto const& body_action : action.body_actions) {
            execute_action(body_action);
            if (break_requested_ || continue_requested_) break;
        }

        if (break_requested_) {
            break_requested_ = false;
            break;
        }
        if (continue_requested_) {
            continue_requested_ = false;
        }
    }

    temp_bindings_.erase(action.iter_var);
}

void RhsExecutor::execute_while(CompiledAction const& action) {
    if (!action.condition) {
        logw("RhsExecutor: WHILE without condition");
        return;
    }

    int max_iter = action.max_iterations > 0 ? action.max_iterations : 1000;
    int iterations = 0;


    while (iterations < max_iter) {
        auto t0 = std::chrono::steady_clock::now();
        double result = action.condition->evaluate([this](std::string const& var) { return resolve_variable(var); });
        auto t1 = std::chrono::steady_clock::now();
        g_condition_eval_us.fetch_add(
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()),
            std::memory_order_relaxed);

        if (result == 0.0) break;

        for (auto const& body_action : action.body_actions) {
            execute_action(body_action);
            if (break_requested_ || continue_requested_) break;
        }

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
        logw("RhsExecutor: WHILE loop hit max iterations ({})", max_iter);
    }

}

void RhsExecutor::execute_switch(CompiledAction const& action) {
    if (!action.switch_expr) {
        logw("RhsExecutor: SWITCH without expression");
        return;
    }

    auto t0 = std::chrono::steady_clock::now();
    double switch_value = action.switch_expr->evaluate([this](std::string const& var) {
        return resolve_variable(var);
    });
    auto t1 = std::chrono::steady_clock::now();
    g_condition_eval_us.fetch_add(
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()),
        std::memory_order_relaxed);


    bool matched = false;
    SwitchCase const* default_case = nullptr;

    for (auto const& sc : action.switch_cases) {
        if (sc.is_default) {
            default_case = &sc;
            continue;
        }

        if (sc.value) {
            auto t2 = std::chrono::steady_clock::now();
            double case_value = sc.value->evaluate([this](std::string const& var) {
                return resolve_variable(var);
            });
            auto t3 = std::chrono::steady_clock::now();
            g_condition_eval_us.fetch_add(
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count()),
                std::memory_order_relaxed);

            if (switch_value == case_value) {
                matched = true;
                for (auto const& case_action : sc.actions) {
                    execute_action(case_action);
                    if (break_requested_) {
                        break_requested_ = false;
                        return;
                    }
                }
                break;
            }
        }
    }

    if (!matched && default_case) {
        for (auto const& case_action : default_case->actions) {
            execute_action(case_action);
            if (break_requested_) {
                break_requested_ = false;
                return;
            }
        }
    }
}

ConstraintValue RhsExecutor::evaluate_assignment(FieldAssignment const& assign) {
    if (assign.has_precomputed_literal) {
        return assign.precomputed_literal;
    }

    ScopedUsTimer timer(g_eval_assignment_us);
    switch (assign.type) {
        case RhsValueType::NUMERIC:
            if (assign.numeric_expr) {
                double result = assign.numeric_expr->evaluate([this](std::string const& var) {
                    return resolve_variable(var);
                });
                // Check if result is integer
                if (result == std::floor(result) && result >= std::numeric_limits<int64_t>::min()
                    && result <= std::numeric_limits<int64_t>::max()) {
                    return static_cast<int64_t>(result);
                }
                return result;
            }
            return 0.0;

        case RhsValueType::STRING:
            return assign.string_literal;

        case RhsValueType::BOOLEAN:
            return assign.string_literal == "true" ? int64_t(1) : int64_t(0);

        case RhsValueType::VAR_REF: {
            // Handle $var or $var.field
            std::string const& ref = assign.var_ref;
            size_t dot_pos = ref.find('.');
            if (dot_pos != std::string::npos) {
                std::string var_name = ref.substr(0, dot_pos);
                std::string field_name = ref.substr(dot_pos + 1);
                auto fact = get_bound_fact(var_name);
                if (fact) {
                    auto field_val = fact->get_field(field_name);
                    if (field_val) {
                        return *field_val;
                    }
                }
                auto global_val = get_global_value(var_name);
                if (global_val) {
                    auto resolved = resolve_global_field(*global_val, field_name);
                    if (resolved) return *resolved;
                }
            } else {
                auto fact = get_bound_fact(ref);
                if (fact) {
                    // Return the fact ID as a reference
                    return fact->id;
                }
                auto global_val = get_global_value(ref);
                if (global_val) return *global_val;
            }
            return NilValue{};
        }
        case RhsValueType::NATIVE_CALL:
            return invoke_native_function(assign.native_call_name, assign.native_call_args);
    }
    return NilValue{};
}

ConstraintValue RhsExecutor::invoke_native_function(std::string const& function_name,
                                                    std::vector<FieldAssignment> const& args) {
    auto* session = dynamic_cast<StatefulSession*>(&callback_);
    if (!session) {
        logw("RhsExecutor: native call '{}' requires StatefulSession callback", function_name);
        return NilValue{};
    }

    auto kb = session->get_knowledge_base();
    if (!kb) {
        logw("RhsExecutor: native call '{}' failed, session has no knowledge base", function_name);
        return NilValue{};
    }

    auto const& native_functions = kb->get_native_functions();
    auto it = native_functions.find(function_name);
    if (it == native_functions.end()) {
        logw("RhsExecutor: native call '{}' not registered", function_name);
        return NilValue{};
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
        logw("RhsExecutor: native call '{}' failed with status {}", function_name, status);
        if (out_result) std::free(out_result);
        return NilValue{};
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

double RhsExecutor::resolve_variable(std::string const& var_name) {
    // Handle $var.field format
    size_t dot_pos = var_name.find('.');
    std::string base_var = (dot_pos != std::string::npos) ? var_name.substr(0, dot_pos) : var_name;
    std::string field_name = (dot_pos != std::string::npos) ? var_name.substr(dot_pos + 1) : "this";

    auto fact = get_bound_fact(base_var);
    if (fact) {
        if (field_name == "this" || field_name == "id") {
            return static_cast<double>(fact->id);
        }

        auto field_val = fact->get_field(field_name);
        if (!field_val) {
            logw("RhsExecutor: Field '{}' not found on '{}'", field_name, base_var);
            return 0.0;
        }

        return constraint_value_to_double(*field_val);
    }

    auto global_val = get_global_value(base_var);
    if (!global_val) {
        logw("RhsExecutor: Variable '{}' not found", base_var);
        return 0.0;
    }

    auto resolved = resolve_global_field(*global_val, field_name);
    if (!resolved) {
        logw("RhsExecutor: Global field '{}.{}' not found", base_var, field_name);
        return 0.0;
    }

    return constraint_value_to_double(*resolved);
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
