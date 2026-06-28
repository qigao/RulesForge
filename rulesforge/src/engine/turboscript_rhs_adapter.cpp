#include "engine/turboscript_rhs_adapter.hpp"

#include "turboscript_runtime_loader.hpp"

#include <cmath>
#include <algorithm>
#include <cctype>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>

namespace rulesforge {
namespace {

namespace ts = turboscript_runtime;

constexpr char const* kCommandFnName = "__rulesforge_rhs_cmd";
constexpr char const* kEvalFnName = "__rulesforge_rhs_eval";
constexpr char const* kForFnName = "__rulesforge_rhs_for";
constexpr char const* kWhileFnName = "__rulesforge_rhs_while";
constexpr char const* kBreakFnName = "__rulesforge_rhs_break";
constexpr char const* kContinueFnName = "__rulesforge_rhs_continue";
constexpr std::size_t kNoSwitchCaseIndex = static_cast<std::size_t>(-1);

bool is_linear_command_action(CompiledAction const& action) {
    switch (action.type) {
        case RhsActionType::UPDATE:
        case RhsActionType::INSERT:
        case RhsActionType::INSERT_LOGICAL:
        case RhsActionType::RETRACT:
        case RhsActionType::HALT:
        case RhsActionType::SET_FOCUS:
        case RhsActionType::INVOKE:
            return true;
        case RhsActionType::IF:
        case RhsActionType::SWITCH:
        case RhsActionType::FOR:
        case RhsActionType::WHILE:
        case RhsActionType::BREAK:
        case RhsActionType::CONTINUE:
            return false;
    }
    return false;
}

std::string sanitize_variable_name(std::string const& name) {
    std::string result = name;
    if (!result.empty() && result[0] == '$') {
        result[0] = '_';
    }
    std::replace(result.begin(), result.end(), '.', '_');
    return result;
}

bool is_identifier_boundary(std::string const& text, std::size_t pos) {
    if (pos >= text.size()) return true;
    unsigned char const c = static_cast<unsigned char>(text[pos]);
    return !std::isalnum(c) && c != '_' && c != '.';
}

std::string replace_expression_variables(std::string expression,
                                         std::vector<std::string> const& variables) {
    std::vector<std::string> ordered = variables;
    std::sort(ordered.begin(), ordered.end(), [](std::string const& lhs, std::string const& rhs) {
        return lhs.size() > rhs.size();
    });

    for (auto const& variable : ordered) {
        if (variable.empty()) continue;
        std::string const sanitized = sanitize_variable_name(variable);
        std::size_t pos = 0;
        while ((pos = expression.find(variable, pos)) != std::string::npos) {
            if (is_identifier_boundary(expression, pos + variable.size())) {
                expression.replace(pos, variable.size(), sanitized);
                pos += sanitized.size();
            } else {
                pos += variable.size();
            }
        }
    }

    return expression;
}

void bind_constraint_value(ts::Api const& api,
                           ts::Context* ctx,
                           std::string const& name,
                           ConstraintValue const& value) {
    if (std::holds_alternative<int64_t>(value)) {
        api.bind_num(ctx, name.c_str(), static_cast<double>(std::get<int64_t>(value)));
    } else if (std::holds_alternative<double>(value)) {
        api.bind_num(ctx, name.c_str(), std::get<double>(value));
    } else if (std::holds_alternative<std::string>(value)) {
        api.bind_str(ctx, name.c_str(), std::get<std::string>(value).c_str());
    } else {
        api.bind_num(ctx, name.c_str(), 0.0);
    }
}

struct CommandCollector {
    std::vector<TurboScriptRhsCommandEvent>* events = nullptr;
    std::function<bool(std::size_t)> const* eval_condition = nullptr;
};

ts::Value collect_command(size_t argc, ts::Value* args, void* user_data) {
    auto* collector = static_cast<CommandCollector*>(user_data);
    if (!collector || !collector->events || argc < 1 || args == nullptr) {
        return ts::value_num(0);
    }

    std::size_t index = 0;
    if (args[0].type == ts::ValueType::Integer) {
        if (args[0].data.integer < 0) return ts::value_num(0);
        index = static_cast<std::size_t>(args[0].data.integer);
    } else if (args[0].type == ts::ValueType::Number) {
        double const value = args[0].data.number;
        if (!std::isfinite(value) || value < 0.0
            || value > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
            return ts::value_num(0);
        }
        index = static_cast<std::size_t>(value);
    } else {
        return ts::value_num(0);
    }

    TurboScriptRhsCommandEvent event;
    event.action_index = index;
    collector->events->push_back(event);
    return ts::value_num(0);
}

ts::Value collect_for_each(size_t argc, ts::Value* args, void* user_data) {
    auto* collector = static_cast<CommandCollector*>(user_data);
    if (!collector || !collector->events || argc < 1 || args == nullptr) {
        return ts::value_num(0);
    }

    std::size_t index = 0;
    if (args[0].type == ts::ValueType::Integer) {
        if (args[0].data.integer < 0) return ts::value_num(0);
        index = static_cast<std::size_t>(args[0].data.integer);
    } else if (args[0].type == ts::ValueType::Number) {
        double const value = args[0].data.number;
        if (!std::isfinite(value) || value < 0.0
            || value > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
            return ts::value_num(0);
        }
        index = static_cast<std::size_t>(value);
    } else {
        return ts::value_num(0);
    }

    TurboScriptRhsCommandEvent event;
    event.type = TurboScriptRhsCommandEventType::ForEach;
    event.action_index = index;
    collector->events->push_back(event);
    return ts::value_num(0);
}

ts::Value collect_while_loop(size_t argc, ts::Value* args, void* user_data) {
    auto* collector = static_cast<CommandCollector*>(user_data);
    if (!collector || !collector->events || argc < 1 || args == nullptr) {
        return ts::value_num(0);
    }

    std::size_t index = 0;
    if (args[0].type == ts::ValueType::Integer) {
        if (args[0].data.integer < 0) return ts::value_num(0);
        index = static_cast<std::size_t>(args[0].data.integer);
    } else if (args[0].type == ts::ValueType::Number) {
        double const value = args[0].data.number;
        if (!std::isfinite(value) || value < 0.0
            || value > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
            return ts::value_num(0);
        }
        index = static_cast<std::size_t>(value);
    } else {
        return ts::value_num(0);
    }

    TurboScriptRhsCommandEvent event;
    event.type = TurboScriptRhsCommandEventType::WhileLoop;
    event.action_index = index;
    collector->events->push_back(event);
    return ts::value_num(0);
}

ts::Value collect_break(size_t, ts::Value*, void* user_data) {
    auto* collector = static_cast<CommandCollector*>(user_data);
    if (!collector || !collector->events) {
        return ts::value_num(0);
    }

    TurboScriptRhsCommandEvent event;
    event.type = TurboScriptRhsCommandEventType::Break;
    collector->events->push_back(event);
    return ts::value_num(0);
}

ts::Value collect_continue(size_t, ts::Value*, void* user_data) {
    auto* collector = static_cast<CommandCollector*>(user_data);
    if (!collector || !collector->events) {
        return ts::value_num(0);
    }

    TurboScriptRhsCommandEvent event;
    event.type = TurboScriptRhsCommandEventType::Continue;
    collector->events->push_back(event);
    return ts::value_num(0);
}

ts::Value evaluate_condition(size_t argc, ts::Value* args, void* user_data) {
    auto* collector = static_cast<CommandCollector*>(user_data);
    if (!collector || !collector->eval_condition || !(*collector->eval_condition)
        || argc < 1 || args == nullptr) {
        return ts::value_num(0);
    }

    std::size_t index = 0;
    if (args[0].type == ts::ValueType::Integer) {
        if (args[0].data.integer < 0) return ts::value_num(0);
        index = static_cast<std::size_t>(args[0].data.integer);
    } else if (args[0].type == ts::ValueType::Number) {
        double const value = args[0].data.number;
        if (!std::isfinite(value) || value < 0.0
            || value > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
            return ts::value_num(0);
        }
        index = static_cast<std::size_t>(value);
    } else {
        return ts::value_num(0);
    }

    return ts::value_num((*collector->eval_condition)(index) ? 1.0 : 0.0);
}

class TurboScriptContext {
public:
    explicit TurboScriptContext(ts::Api const& api)
        : api_(&api), ctx_(api.init(ts::kInitBare)) {}
    ~TurboScriptContext() {
        if (ctx_) {
            std::lock_guard<std::recursive_mutex> runtime_lock(ts::api_mutex());
            api_->free(ctx_);
        }
    }

    TurboScriptContext(TurboScriptContext const&) = delete;
    TurboScriptContext& operator=(TurboScriptContext const&) = delete;
    TurboScriptContext(TurboScriptContext&& other) noexcept
        : api_(other.api_), ctx_(other.ctx_) {
        other.ctx_ = nullptr;
    }
    TurboScriptContext& operator=(TurboScriptContext&& other) noexcept {
        if (this != &other) {
            if (ctx_) {
                std::lock_guard<std::recursive_mutex> runtime_lock(ts::api_mutex());
                api_->free(ctx_);
            }
            api_ = other.api_;
            ctx_ = other.ctx_;
            other.ctx_ = nullptr;
        }
        return *this;
    }

    ts::Context* get() const { return ctx_; }

private:
    ts::Api const* api_ = nullptr;
    ts::Context* ctx_ = nullptr;
};

class TurboScriptCompiled {
public:
    TurboScriptCompiled(ts::Api const& api, ts::Compiled* compiled)
        : api_(&api), compiled_(compiled) {}
    ~TurboScriptCompiled() {
        if (compiled_) {
            std::lock_guard<std::recursive_mutex> runtime_lock(ts::api_mutex());
            api_->compiled_free(compiled_);
        }
    }

    TurboScriptCompiled(TurboScriptCompiled const&) = delete;
    TurboScriptCompiled& operator=(TurboScriptCompiled const&) = delete;
    TurboScriptCompiled(TurboScriptCompiled&& other) noexcept
        : api_(other.api_), compiled_(other.compiled_) {
        other.compiled_ = nullptr;
    }
    TurboScriptCompiled& operator=(TurboScriptCompiled&& other) noexcept {
        if (this != &other) {
            if (compiled_) {
                std::lock_guard<std::recursive_mutex> runtime_lock(ts::api_mutex());
                api_->compiled_free(compiled_);
            }
            api_ = other.api_;
            compiled_ = other.compiled_;
            other.compiled_ = nullptr;
        }
        return *this;
    }

    ts::Compiled* get() const { return compiled_; }

private:
    ts::Api const* api_ = nullptr;
    ts::Compiled* compiled_ = nullptr;
};

} // namespace

struct TurboScriptRhsProgram::Impl {
    TurboScriptContext ctx;
    TurboScriptCompiled compiled;
    CommandCollector collector;

    Impl(TurboScriptContext&& context, TurboScriptCompiled&& compiled_program)
        : ctx(std::move(context)), compiled(std::move(compiled_program)) {}
};

struct TurboScriptExpressionProgram::Impl {
    TurboScriptContext ctx;
    TurboScriptCompiled compiled;
    std::vector<std::string> variables;
    std::vector<std::string> sanitized_variables;

    Impl(TurboScriptContext&& context,
         TurboScriptCompiled&& compiled_program,
         std::vector<std::string> original_variables,
         std::vector<std::string> safe_variables)
        : ctx(std::move(context)),
          compiled(std::move(compiled_program)),
          variables(std::move(original_variables)),
          sanitized_variables(std::move(safe_variables)) {}
};

bool TurboScriptRhsAdapter::build_command_script(std::vector<CompiledAction> const& actions,
                                                 TurboScriptRhsCommandScript& out,
                                                 std::string* reason_out,
                                                 bool allow_loop_control) {
    out = TurboScriptRhsCommandScript{};
    if (actions.empty()) {
        return true;
    }

    std::ostringstream script;
    auto append_actions = [&](auto const& self,
                              std::vector<CompiledAction> const& items,
                              std::ostringstream& target,
                              bool in_loop,
                              TurboScriptRhsCommandScript& script_out) -> bool {
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (index != 0) target << "; ";
            CompiledAction const& action = items[index];
            if (is_linear_command_action(action)) {
                std::size_t const command_index = script_out.command_actions.size();
                script_out.command_actions.push_back(&action);
                target << kCommandFnName << "(" << command_index << ")";
                continue;
            }

            if (action.type == RhsActionType::IF) {
                if (!action.condition) {
                    if (reason_out) *reason_out = "condition_missing";
                    return false;
                }
                std::size_t const condition_index = script_out.condition_actions.size();
                script_out.condition_actions.push_back(&action);
                script_out.condition_switch_case_indices.push_back(kNoSwitchCaseIndex);
                target << "if (" << kEvalFnName << "(" << condition_index << ")) { ";
                if (!self(self, action.then_actions, target, in_loop, script_out)) return false;
                target << " } else { ";
                if (!self(self, action.else_actions, target, in_loop, script_out)) return false;
                target << " }";
                continue;
            }

            if (action.type == RhsActionType::SWITCH) {
                if (!action.switch_expr) {
                    if (reason_out) *reason_out = "condition_missing";
                    return false;
                }

                bool emitted_case = false;
                SwitchCase const* default_case = nullptr;
                for (std::size_t case_index = 0; case_index < action.switch_cases.size(); ++case_index) {
                    auto const& switch_case = action.switch_cases[case_index];
                    if (switch_case.is_default) {
                        default_case = &switch_case;
                        continue;
                    }
                    if (!switch_case.value) {
                        if (reason_out) *reason_out = "condition_missing";
                        return false;
                    }
                    std::size_t const condition_index = script_out.condition_actions.size();
                    script_out.condition_actions.push_back(&action);
                    script_out.condition_switch_case_indices.push_back(case_index);
                    target << (emitted_case ? " else if (" : "if (")
                           << kEvalFnName << "(" << condition_index << ")) { ";
                    if (!self(self, switch_case.actions, target, in_loop, script_out)) return false;
                    target << " }";
                    emitted_case = true;
                }

                if (default_case) {
                    if (emitted_case) {
                        target << " else { ";
                        if (!self(self, default_case->actions, target, in_loop, script_out)) return false;
                        target << " }";
                    } else {
                        if (!self(self, default_case->actions, target, in_loop, script_out)) return false;
                    }
                } else if (!emitted_case) {
                    if (reason_out) *reason_out = "condition_missing";
                    return false;
                }
                continue;
            }

            if (action.type == RhsActionType::FOR) {
                TurboScriptRhsCommandScript body_script_probe;
                std::ostringstream body_probe;
                if (!self(self, action.body_actions, body_probe, true, body_script_probe)) {
                    return false;
                }
                std::size_t const for_index = script_out.for_actions.size();
                script_out.for_actions.push_back(&action);
                target << kForFnName << "(" << for_index << ")";
                continue;
            }

            if (action.type == RhsActionType::WHILE) {
                if (!action.condition) {
                    if (reason_out) *reason_out = "condition_missing";
                    return false;
                }
                TurboScriptRhsCommandScript body_script_probe;
                std::ostringstream body_probe;
                if (!self(self, action.body_actions, body_probe, true, body_script_probe)) {
                    return false;
                }
                std::size_t const while_index = script_out.while_actions.size();
                script_out.while_actions.push_back(&action);
                target << kWhileFnName << "(" << while_index << ")";
                continue;
            }

            if (action.type == RhsActionType::BREAK) {
                if (!in_loop) {
                    if (reason_out) *reason_out = "control_flow_unsupported";
                    return false;
                }
                target << kBreakFnName << "()";
                continue;
            }

            if (action.type == RhsActionType::CONTINUE) {
                if (!in_loop) {
                    if (reason_out) *reason_out = "control_flow_unsupported";
                    return false;
                }
                target << kContinueFnName << "()";
                continue;
            }

            if (reason_out) *reason_out = "control_flow_unsupported";
            return false;
        }
        return true;
    };

    if (!append_actions(append_actions, actions, script, allow_loop_control, out)) {
        out = TurboScriptRhsCommandScript{};
        return false;
    }

    out.script = script.str();
    return true;
}

bool TurboScriptRhsAdapter::execute_command_script(TurboScriptRhsCommandScript const& script,
                                                   std::vector<std::size_t>& out_action_indices,
                                                   std::string* error_out) {
    out_action_indices.clear();
    if (script.script.empty()) {
        return true;
    }

    auto program = TurboScriptRhsProgram::compile(script.script, error_out);
    return program && program->execute(out_action_indices, error_out);
}

bool TurboScriptRhsAdapter::evaluate_boolean_expression(
    std::string const& expression,
    std::vector<std::string> const& variables,
    std::function<ConstraintValue(std::string const&)> const& resolver,
    bool& out_value,
    std::string* error_out) {
    out_value = false;
    ConstraintValue value;
    if (!evaluate_expression(expression, variables, resolver, value, error_out)) {
        return false;
    }

    if (auto const* d = std::get_if<double>(&value)) {
        out_value = *d != 0.0;
    } else if (auto const* i = std::get_if<int64_t>(&value)) {
        out_value = *i != 0;
    } else if (auto const* s = std::get_if<std::string>(&value)) {
        out_value = !s->empty() && *s != "false" && *s != "0";
    } else {
        out_value = !std::holds_alternative<NilValue>(value);
    }
    return true;
}

bool TurboScriptRhsAdapter::evaluate_expression(
    std::string const& expression,
    std::vector<std::string> const& variables,
    std::function<ConstraintValue(std::string const&)> const& resolver,
    ConstraintValue& out_value,
    std::string* error_out) {
    out_value = NilValue{};
    auto program = TurboScriptExpressionProgram::compile(expression, variables, error_out);
    return program && program->execute(resolver, out_value, error_out);
}

TurboScriptExpressionProgram::TurboScriptExpressionProgram(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

TurboScriptExpressionProgram::~TurboScriptExpressionProgram() = default;

std::unique_ptr<TurboScriptExpressionProgram> TurboScriptExpressionProgram::compile(
    std::string const& expression,
    std::vector<std::string> const& variables,
    std::string* error_out) {
    std::string runtime_error;
    auto const* api = ts::load(&runtime_error);
    if (!api) {
        if (error_out) *error_out = runtime_error;
        return nullptr;
    }

    std::lock_guard<std::recursive_mutex> runtime_lock(ts::api_mutex());
    TurboScriptContext ctx(*api);
    if (!ctx.get()) {
        if (error_out) *error_out = "turboscript_init_failed";
        return nullptr;
    }

    std::string const script = "result = " + replace_expression_variables(expression, variables);
    TurboScriptCompiled compiled(*api, api->compile(ctx.get(), script.c_str()));
    if (!compiled.get()) {
        char const* error = api->get_error(ctx.get());
        if (error_out) *error_out = error && *error ? error : "turboscript_compile_failed";
        return nullptr;
    }

    std::vector<std::string> sanitized;
    sanitized.reserve(variables.size());
    for (auto const& variable : variables) {
        sanitized.push_back(sanitize_variable_name(variable));
    }

    auto impl = std::make_unique<Impl>(std::move(ctx), std::move(compiled), variables, std::move(sanitized));
    return std::unique_ptr<TurboScriptExpressionProgram>(
        new TurboScriptExpressionProgram(std::move(impl)));
}

bool TurboScriptExpressionProgram::execute(
    std::function<ConstraintValue(std::string const&)> const& resolver,
    ConstraintValue& out_value,
    std::string* error_out) {
    out_value = NilValue{};
    if (!impl_ || !impl_->ctx.get() || !impl_->compiled.get()) {
        if (error_out) *error_out = "turboscript_expression_program_unavailable";
        return false;
    }

    std::string runtime_error;
    auto const* api = ts::load(&runtime_error);
    if (!api) {
        if (error_out) *error_out = runtime_error;
        return false;
    }

    std::lock_guard<std::recursive_mutex> runtime_lock(ts::api_mutex());
    for (std::size_t index = 0; index < impl_->variables.size(); ++index) {
        bind_constraint_value(
            *api, impl_->ctx.get(), impl_->sanitized_variables[index], resolver(impl_->variables[index]));
    }

    int const status = api->exec(impl_->ctx.get(), impl_->compiled.get());
    if (status != 0) {
        char const* error = api->get_error(impl_->ctx.get());
        if (error_out) *error_out = error && *error ? error : "turboscript_exec_failed";
        return false;
    }

    if (char const* result_string = api->get_str(impl_->ctx.get(), "result")) {
        out_value = std::string(result_string);
    } else {
        out_value = api->get_num(impl_->ctx.get(), "result");
    }
    return true;
}

TurboScriptRhsProgram::TurboScriptRhsProgram(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

TurboScriptRhsProgram::~TurboScriptRhsProgram() = default;

std::unique_ptr<TurboScriptRhsProgram> TurboScriptRhsProgram::compile(std::string const& script,
                                                                      std::string* error_out) {
    std::string runtime_error;
    auto const* api = ts::load(&runtime_error);
    if (!api) {
        if (error_out) *error_out = runtime_error;
        return nullptr;
    }

    std::lock_guard<std::recursive_mutex> runtime_lock(ts::api_mutex());
    TurboScriptContext ctx(*api);
    if (!ctx.get()) {
        if (error_out) *error_out = "turboscript_init_failed";
        return nullptr;
    }

    TurboScriptCompiled compiled(*api, api->compile(ctx.get(), script.c_str()));
    if (!compiled.get()) {
        char const* error = api->get_error(ctx.get());
        if (error_out) *error_out = error && *error ? error : "turboscript_compile_failed";
        return nullptr;
    }

    auto impl = std::make_unique<Impl>(std::move(ctx), std::move(compiled));
    return std::unique_ptr<TurboScriptRhsProgram>(new TurboScriptRhsProgram(std::move(impl)));
}

bool TurboScriptRhsProgram::execute(std::vector<TurboScriptRhsCommandEvent>& out_events,
                                    std::function<bool(std::size_t)> const& eval_condition,
                                    std::string* error_out) {
    out_events.clear();
    if (!impl_ || !impl_->ctx.get() || !impl_->compiled.get()) {
        if (error_out) *error_out = "turboscript_program_unavailable";
        return false;
    }

    std::string runtime_error;
    auto const* api = ts::load(&runtime_error);
    if (!api) {
        if (error_out) *error_out = runtime_error;
        return false;
    }

    std::lock_guard<std::recursive_mutex> runtime_lock(ts::api_mutex());
    impl_->collector.events = &out_events;
    impl_->collector.eval_condition = &eval_condition;
    api->bind_func(impl_->ctx.get(), kCommandFnName, collect_command, &impl_->collector);
    api->bind_func(impl_->ctx.get(), kEvalFnName, evaluate_condition, &impl_->collector);
    api->bind_func(impl_->ctx.get(), kForFnName, collect_for_each, &impl_->collector);
    api->bind_func(impl_->ctx.get(), kWhileFnName, collect_while_loop, &impl_->collector);
    api->bind_func(impl_->ctx.get(), kBreakFnName, collect_break, &impl_->collector);
    api->bind_func(impl_->ctx.get(), kContinueFnName, collect_continue, &impl_->collector);

    int const status = api->exec(impl_->ctx.get(), impl_->compiled.get());
    impl_->collector.events = nullptr;
    impl_->collector.eval_condition = nullptr;
    if (status != 0) {
        char const* error = api->get_error(impl_->ctx.get());
        if (error_out) *error_out = error && *error ? error : "turboscript_exec_failed";
        out_events.clear();
        return false;
    }

    return true;
}

bool TurboScriptRhsProgram::execute(std::vector<std::size_t>& out_action_indices,
                                    std::function<bool(std::size_t)> const& eval_condition,
                                    std::string* error_out) {
    out_action_indices.clear();

    std::vector<TurboScriptRhsCommandEvent> events;
    if (!execute(events, eval_condition, error_out)) {
        return false;
    }

    out_action_indices.reserve(events.size());
    for (auto const& event : events) {
        if (event.type == TurboScriptRhsCommandEventType::Command) {
            out_action_indices.push_back(event.action_index);
        }
    }
    return true;
}

bool TurboScriptRhsProgram::execute(std::vector<std::size_t>& out_action_indices,
                                    std::string* error_out) {
    std::function<bool(std::size_t)> no_eval;
    return execute(out_action_indices, no_eval, error_out);
}

} // namespace rulesforge
