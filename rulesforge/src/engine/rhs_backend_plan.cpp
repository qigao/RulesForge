#include "engine/rhs_backend_plan.hpp"

#include "core/parsed_rule.hpp"
#include <algorithm>
#include <sstream>
#include <string>

namespace rulesforge {
namespace {

void add_unique_reason(std::vector<std::string>& reasons, char const* reason) {
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
        reasons.emplace_back(reason);
    }
}

RhsBackendCoverage build_coverage(std::size_t rule_index,
                                  ParsedRule const& rule,
                                  std::unique_ptr<RhsCompiledCommandProgram>& command_program) {
    RhsBackendCoverage coverage;
    coverage.rule_index = rule_index;
    coverage.rule_name = rule.name;
    coverage.action_count = rule.compiled_actions.size();
    coverage.cpp_action_plan_available = true;

    if (rule.rhs_code.empty() || rule.compiled_actions.empty()) {
        coverage.backend = RhsBackendKind::None;
        coverage.command_adapter_ready = true;
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
        if (!CppRhsActionPlanBuilder::build(
                actions, program->script, reason_out, allow_loop_control)) {
            return nullptr;
        }

        (void)self;

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

    coverage.backend = RhsBackendKind::CppActionPlan;
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

    plan->coverages_.reserve(rules.size());
    plan->command_programs_.reserve(rules.size());
    for (std::size_t index = 0; index < rules.size(); ++index) {
        std::unique_ptr<RhsCompiledCommandProgram> command_program;
        auto coverage = build_coverage(index, rules[index], command_program);
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
    result.cpp_action_plan_available = cpp_action_plan_available_;
    result.rule_coverage_count = coverages_.size();

    for (auto const& coverage : coverages_) {
        if (coverage.backend == RhsBackendKind::CppActionPlan) {
            ++result.cpp_action_plan_rule_count;
        } else if (coverage.backend == RhsBackendKind::CompileError) {
            ++result.compile_error_rule_count;
        }
        result.command_count += coverage.command_count;
        result.condition_count += coverage.condition_count;
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
    out << "RHS backend summary: cpp_action_plan_available="
        << (s.cpp_action_plan_available ? "true" : "false")
        << ", rule_coverage=" << s.rule_coverage_count
        << ", cpp_action_plan_rules=" << s.cpp_action_plan_rule_count
        << ", compile_error_rules=" << s.compile_error_rule_count
        << ", commands=" << s.command_count
        << ", conditions=" << s.condition_count
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
            << ", cpp_action_plan_available=" << (coverage.cpp_action_plan_available ? "true" : "false")
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
        case RhsBackendKind::CppActionPlan: return "cpp_action_plan";
        case RhsBackendKind::CompileError: return "rhs_lowering_failed";
    }
    return "unknown";
}

} // namespace rulesforge
