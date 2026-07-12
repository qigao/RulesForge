#ifndef RHS_BACKEND_PLAN_HPP
#define RHS_BACKEND_PLAN_HPP

#include "engine/cpp_rhs_action_plan_builder.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

struct ParsedRule;

namespace rulesforge {

enum class RhsBackendKind {
    None,
    CppActionPlan,
    CompileError
};

struct RhsBackendCoverage {
    std::size_t rule_index = 0;
    std::string rule_name;
    RhsBackendKind backend = RhsBackendKind::None;
    std::size_t action_count = 0;
    std::size_t command_count = 0;
    std::size_t condition_count = 0;
    bool cpp_action_plan_available = false;
    bool command_adapter_ready = false;
    std::vector<std::string> lowering_errors;
};

struct RhsBackendPlanSummary {
    bool cpp_action_plan_available = false;
    std::size_t rule_coverage_count = 0;
    std::size_t cpp_action_plan_rule_count = 0;
    std::size_t compile_error_rule_count = 0;
    std::size_t command_count = 0;
    std::size_t condition_count = 0;
    std::vector<std::string> lowering_errors;
};

struct RhsCompiledCommandProgram {
    CppRhsActionScript script;
};

class RhsBackendPlan {
public:
    RhsBackendPlan(RhsBackendPlan const&) = delete;
    RhsBackendPlan& operator=(RhsBackendPlan const&) = delete;
    RhsBackendPlan(RhsBackendPlan&&) = delete;
    RhsBackendPlan& operator=(RhsBackendPlan&&) = delete;
    ~RhsBackendPlan();

    static std::unique_ptr<RhsBackendPlan> compile(std::vector<ParsedRule> const& rules);

    RhsBackendPlanSummary summary() const;
    std::string format_summary() const;
    std::string format_debug_dump() const;
    std::vector<RhsBackendCoverage> const& coverages() const { return coverages_; }
    RhsCompiledCommandProgram const* command_program_for_rule(std::size_t rule_index) const;

private:
    RhsBackendPlan() = default;

    bool cpp_action_plan_available_ = true;
    std::vector<RhsBackendCoverage> coverages_;
    std::vector<std::unique_ptr<RhsCompiledCommandProgram>> command_programs_;
};

char const* rhs_backend_kind_name(RhsBackendKind kind);

} // namespace rulesforge

#endif // RHS_BACKEND_PLAN_HPP
