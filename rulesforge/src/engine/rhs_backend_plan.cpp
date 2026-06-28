#include "engine/rhs_backend_plan.hpp"

#include "core/parsed_rule.hpp"
#include "engine/turboscript_rhs_adapter.hpp"
#include "turboscript_runtime_loader.hpp"

#include <algorithm>
#include <mutex>
#include <sstream>
#include <string>

namespace rulesforge {
namespace {

void add_unique_reason(std::vector<std::string>& reasons, char const* reason) {
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
        reasons.emplace_back(reason);
    }
}

bool probe_turboscript_backend(std::string* error_out) {
    namespace ts = turboscript_runtime;

    std::string runtime_error;
    auto const* api = ts::load(&runtime_error);
    if (!api) {
        if (error_out) *error_out = runtime_error;
        return false;
    }

    std::lock_guard<std::recursive_mutex> runtime_lock(ts::api_mutex());
    ts::Context* ctx = api->init(ts::kInitBare);
    if (ctx == nullptr) {
        if (error_out) *error_out = "init_failed";
        return false;
    }

    ts::Compiled* compiled = api->compile(ctx, "0");
    if (compiled == nullptr) {
        char const* error = api->get_error(ctx);
        if (error_out) *error_out = error && *error ? error : "compile_probe_failed";
        api->free(ctx);
        return false;
    }

    api->compiled_free(compiled);
    api->free(ctx);
    return true;
}

void add_unique(std::vector<std::string>& values, std::string const& value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

std::size_t collect_external_side_effects(CompiledAction const& action,
                                          std::vector<std::string>& names) {
    std::size_t result = 0;
    if (action.type == RhsActionType::INVOKE) {
        ++result;
        add_unique(names, action.invoke_function);
    }
    for (auto const& nested : action.then_actions) {
        result += collect_external_side_effects(nested, names);
    }
    for (auto const& nested : action.else_actions) {
        result += collect_external_side_effects(nested, names);
    }
    for (auto const& nested : action.body_actions) {
        result += collect_external_side_effects(nested, names);
    }
    for (auto const& switch_case : action.switch_cases) {
        for (auto const& nested : switch_case.actions) {
            result += collect_external_side_effects(nested, names);
        }
    }
    return result;
}

std::size_t collect_external_side_effects(std::vector<CompiledAction> const& actions,
                                          std::vector<std::string>& names) {
    std::size_t result = 0;
    for (auto const& action : actions) {
        result += collect_external_side_effects(action, names);
    }
    return result;
}

RhsBackendCoverage build_coverage(std::size_t rule_index,
                                  ParsedRule const& rule,
                                  bool turboscript_available,
                                  std::unique_ptr<RhsCompiledCommandProgram>& command_program) {
    RhsBackendCoverage coverage;
    coverage.rule_index = rule_index;
    coverage.rule_name = rule.name;
    coverage.action_count = rule.compiled_actions.size();
    coverage.external_side_effect_count =
        collect_external_side_effects(rule.compiled_actions, coverage.external_side_effect_names);
    coverage.turboscript_available = turboscript_available;

    if (rule.rhs_code.empty() || rule.compiled_actions.empty()) {
        coverage.backend = RhsBackendKind::None;
        coverage.command_adapter_ready = true;
        return coverage;
    }

    if (!turboscript_available) {
        coverage.backend = RhsBackendKind::CompileError;
        add_unique_reason(coverage.lowering_errors, "turboscript_unavailable");
        return coverage;
    }

    std::string build_reason;
    auto compiled_program = std::unique_ptr<RhsCompiledCommandProgram>{};

    auto compile_command_program =
        [&](auto const& self,
            std::vector<CompiledAction> const& actions,
            bool allow_loop_control,
            std::string* reason_out) -> std::unique_ptr<RhsCompiledCommandProgram> {
        auto program = std::make_unique<RhsCompiledCommandProgram>();
        if (!TurboScriptRhsAdapter::build_command_script(
                actions, program->script, reason_out, allow_loop_control)) {
            return nullptr;
        }

        if (!program->script.script.empty()) {
            std::string compile_error;
            program->program = TurboScriptRhsProgram::compile(program->script.script, &compile_error);
            if (!program->program) {
                if (reason_out) {
                    *reason_out = "turboscript_compile_failed";
                    if (!compile_error.empty()) {
                        *reason_out += ": ";
                        *reason_out += compile_error;
                    }
                }
                return nullptr;
            }
        }

        program->for_body_programs.reserve(program->script.for_actions.size());
        for (auto const* for_action : program->script.for_actions) {
            if (!for_action) {
                if (reason_out) *reason_out = "for_body_missing";
                return nullptr;
            }
            std::string body_reason;
            auto body_program = self(self, for_action->body_actions, true, &body_reason);
            if (!body_program) {
                if (reason_out) {
                    *reason_out = "for_body_compile_failed";
                    if (!body_reason.empty()) {
                        *reason_out += ": ";
                        *reason_out += body_reason;
                    }
                }
                return nullptr;
            }
            program->for_body_programs.push_back(std::move(body_program));
        }

        program->while_body_programs.reserve(program->script.while_actions.size());
        for (auto const* while_action : program->script.while_actions) {
            if (!while_action) {
                if (reason_out) *reason_out = "while_body_missing";
                return nullptr;
            }
            std::string body_reason;
            auto body_program = self(self, while_action->body_actions, true, &body_reason);
            if (!body_program) {
                if (reason_out) {
                    *reason_out = "while_body_compile_failed";
                    if (!body_reason.empty()) {
                        *reason_out += ": ";
                        *reason_out += body_reason;
                    }
                }
                return nullptr;
            }
            program->while_body_programs.push_back(std::move(body_program));
        }

        return program;
    };

    compiled_program = compile_command_program(
        compile_command_program, rule.compiled_actions, false, &build_reason);
    if (!compiled_program) {
        coverage.backend = RhsBackendKind::CompileError;
        if (!build_reason.empty()) {
            add_unique_reason(coverage.lowering_errors, build_reason.c_str());
        }
        add_unique_reason(coverage.lowering_errors, "rhs_lowering_failed");
        return coverage;
    }

    coverage.backend = RhsBackendKind::TurboScriptMir;
    coverage.command_adapter_ready = true;
    coverage.command_count = compiled_program->script.command_actions.size()
        + compiled_program->script.for_actions.size()
        + compiled_program->script.while_actions.size();
    coverage.condition_count = compiled_program->script.condition_actions.size();
    command_program = std::move(compiled_program);
    return coverage;
}

} // namespace

RhsBackendPlan::~RhsBackendPlan() = default;

std::unique_ptr<RhsBackendPlan> RhsBackendPlan::compile(std::vector<ParsedRule> const& rules) {
    auto plan = std::unique_ptr<RhsBackendPlan>(new RhsBackendPlan());

    std::string probe_error;
    plan->turboscript_available_ = probe_turboscript_backend(&probe_error);

    plan->coverages_.reserve(rules.size());
    plan->command_programs_.reserve(rules.size());
    for (std::size_t index = 0; index < rules.size(); ++index) {
        std::unique_ptr<RhsCompiledCommandProgram> command_program;
        auto coverage = build_coverage(index, rules[index], plan->turboscript_available_, command_program);
        if (!plan->turboscript_available_ && !probe_error.empty()) {
            add_unique_reason(coverage.lowering_errors, probe_error.c_str());
        }
        plan->coverages_.push_back(std::move(coverage));
        plan->command_programs_.push_back(std::move(command_program));
    }

    return plan;
}

RhsCompiledCommandProgram const* RhsBackendPlan::command_program_for_rule(std::size_t rule_index) const {
    if (rule_index >= command_programs_.size()) {
        return nullptr;
    }
    return command_programs_[rule_index].get();
}

RhsBackendPlanSummary RhsBackendPlan::summary() const {
    RhsBackendPlanSummary result;
    result.turboscript_available = turboscript_available_;
    result.rule_coverage_count = coverages_.size();

    for (auto const& coverage : coverages_) {
        if (coverage.backend == RhsBackendKind::TurboScriptMir) {
            ++result.turboscript_mir_rule_count;
        } else if (coverage.backend == RhsBackendKind::CompileError) {
            ++result.compile_error_rule_count;
        }
        result.command_count += coverage.command_count;
        result.condition_count += coverage.condition_count;
        result.external_side_effect_count += coverage.external_side_effect_count;
        for (auto const& name : coverage.external_side_effect_names) {
            add_unique(result.external_side_effect_names, name);
        }
        for (auto const& reason : coverage.lowering_errors) {
            if (std::find(result.lowering_errors.begin(), result.lowering_errors.end(), reason)
                == result.lowering_errors.end()) {
                result.lowering_errors.push_back(reason);
            }
        }
    }

    return result;
}

std::string RhsBackendPlan::format_summary() const {
    auto const s = summary();
    std::ostringstream out;
    out << "RHS backend summary: turboscript_available="
        << (s.turboscript_available ? "true" : "false")
        << ", rule_coverage=" << s.rule_coverage_count
        << ", turboscript_mir_rules=" << s.turboscript_mir_rule_count
        << ", compile_error_rules=" << s.compile_error_rule_count
        << ", commands=" << s.command_count
        << ", conditions=" << s.condition_count
        << ", external_side_effects=" << s.external_side_effect_count
        << ", external_side_effect_names=";
    if (s.external_side_effect_names.empty()) {
        out << "none";
    } else {
        for (std::size_t index = 0; index < s.external_side_effect_names.size(); ++index) {
            if (index != 0) out << "|";
            out << s.external_side_effect_names[index];
        }
    }
    out
        << ", lowering_errors=";
    if (s.lowering_errors.empty()) {
        out << "none";
    } else {
        for (std::size_t index = 0; index < s.lowering_errors.size(); ++index) {
            if (index != 0) out << "|";
            out << s.lowering_errors[index];
        }
    }
    return out.str();
}

std::string RhsBackendPlan::format_debug_dump() const {
    std::ostringstream out;
    out << format_summary() << "\n";
    out << "RHS backend coverage:\n";
    if (coverages_.empty()) {
        out << "  none\n";
        return out.str();
    }

    for (auto const& coverage : coverages_) {
        out << "  rule[" << coverage.rule_index << "] \"" << coverage.rule_name << "\""
            << ": backend=" << rhs_backend_kind_name(coverage.backend)
            << ", actions=" << coverage.action_count
            << ", commands=" << coverage.command_count
            << ", conditions=" << coverage.condition_count
            << ", external_side_effects=" << coverage.external_side_effect_count
            << ", external_side_effect_names=";
        if (coverage.external_side_effect_names.empty()) {
            out << "none";
        } else {
            for (std::size_t index = 0; index < coverage.external_side_effect_names.size(); ++index) {
                if (index != 0) out << "|";
                out << coverage.external_side_effect_names[index];
            }
        }
        out
            << ", turboscript_available=" << (coverage.turboscript_available ? "true" : "false")
            << ", command_adapter_ready=" << (coverage.command_adapter_ready ? "true" : "false")
            << ", lowering_errors=";
        if (coverage.lowering_errors.empty()) {
            out << "none";
        } else {
            for (std::size_t index = 0; index < coverage.lowering_errors.size(); ++index) {
                if (index != 0) out << "|";
                out << coverage.lowering_errors[index];
            }
        }
        out << "\n";
    }

    return out.str();
}

char const* rhs_backend_kind_name(RhsBackendKind kind) {
    switch (kind) {
        case RhsBackendKind::None: return "none";
        case RhsBackendKind::TurboScriptMir: return "turboscript_mir";
        case RhsBackendKind::CompileError: return "rhs_lowering_failed";
    }
    return "unknown";
}

} // namespace rulesforge
