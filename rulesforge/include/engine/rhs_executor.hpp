#ifndef RHS_EXECUTOR_HPP
#define RHS_EXECUTOR_HPP

#include "core/rhs_actions.hpp"
#include "core/token.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class INetworkCallback;

namespace rulesforge {

struct RhsCompiledCommandProgram;

/**
     * @brief RHS transaction executor driven by compiled C++ action plans.
     *
     * The executor resolves facts and commits or rolls back RulesForge working-memory side effects
     * at the transaction boundary.
     */
class RhsExecutor {
public:
    explicit RhsExecutor(INetworkCallback& callback);
    ~RhsExecutor();

    /**
     * @brief Execute a precompiled RHS command program.
     * @param command_program C++ action plan built by RhsBackendPlan
     * @param token Current token with matched facts
     * @param bindings Variable name to token depth mapping
     * @param rule_name Name of the rule (for error messages)
     */
    void execute(RhsCompiledCommandProgram const& command_program,
                 ::Token& token,
                 std::map<std::string, int> const& bindings,
                 std::string const& rule_name);

private:
    void execute_actions(std::vector<CompiledAction const*> const& actions);
    void execute_actions(std::vector<CompiledAction> const& actions);
    void execute_action(CompiledAction const& action);
    void execute_update(CompiledAction const& action);
    void execute_insert(CompiledAction const& action);
    void execute_insert_logical(CompiledAction const& action);
    void execute_retract(CompiledAction const& action);
    void execute_halt(CompiledAction const& action);
    void execute_set_focus(CompiledAction const& action);
    std::vector<::Fact*> collect_for_items(CompiledAction const& action);

    std::optional<ConstraintValue> evaluate_simple_rhs_value(std::string const& expression);
    std::optional<bool> evaluate_simple_rhs_condition(rulesforge::ExpressionDescriptor const& expr);
    ConstraintValue evaluate_assignment(FieldAssignment const& assign);
    ConstraintValue resolve_variable(std::string const& var_name);
    ::Fact* get_bound_fact(std::string const& var_name);
    std::optional<ConstraintValue> get_global_value(std::string const& name) const;

    INetworkCallback& callback_;
    ::Token* current_token_ = nullptr;
    std::map<std::string, int> const* bindings_ = nullptr;
    std::map<std::string, ::Fact*> temp_bindings_;  // FOR loop iteration vars
    std::string current_rule_name_;
    bool break_requested_ = false;
    bool continue_requested_ = false;
};

namespace rhs_prof {
struct Stats {
    uint64_t evaluate_assignment_us = 0;
    uint64_t condition_eval_us = 0;
    uint64_t update_action_us = 0;
    uint64_t insert_action_us = 0;
    uint64_t retract_action_us = 0;
    uint64_t cpp_action_plan_exec_count = 0;
    uint64_t cpp_action_plan_error_count = 0;
    uint64_t expression_exec_count = 0;
    uint64_t expression_error_count = 0;
    uint64_t expression_non_scalar_error_count = 0;
    // Mutex contention timers — non-zero values indicate lock-wait overhead
    uint64_t execution_mutex_wait_us = 0;
    uint64_t api_mutex_wait_us = 0;
};

void reset_stats();
Stats get_stats();
}  // namespace rhs_prof

} // namespace rulesforge

#endif // RHS_EXECUTOR_HPP
