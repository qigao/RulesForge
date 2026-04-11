#ifndef RHS_EXECUTOR_HPP
#define RHS_EXECUTOR_HPP

#include "core/rhs_actions.hpp"
#include "core/token.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

class INetworkCallback;

namespace rulesforge {

/**
     * @brief Native RHS executor using ExpressionEvaluator-based conditions/assignments.
     *
     * Supports: update, insert, retract, halt, setFocus, invoke, if, for, while, switch, break, continue
     */
class RhsExecutor {
public:
    explicit RhsExecutor(INetworkCallback& callback);
    ~RhsExecutor();

    /**
     * @brief Execute compiled RHS actions.
     * @param actions List of compiled actions from ParsedRule
     * @param token Current token with matched facts
     * @param bindings Variable name to token depth mapping
     * @param rule_name Name of the rule (for error messages)
     */
    void execute(std::vector<CompiledAction> const& actions,
                 ::Token& token,
                 std::map<std::string, int> const& bindings,
                 std::string const& rule_name);

private:
    void execute_action(CompiledAction const& action);
    void execute_update(CompiledAction const& action);
    void execute_insert(CompiledAction const& action);
    void execute_insert_logical(CompiledAction const& action);
    void execute_retract(CompiledAction const& action);
    void execute_halt(CompiledAction const& action);
    void execute_set_focus(CompiledAction const& action);
    void execute_invoke(CompiledAction const& action);
    void execute_if(CompiledAction const& action);
    void execute_for(CompiledAction const& action);
    void execute_while(CompiledAction const& action);
    void execute_switch(CompiledAction const& action);

    ConstraintValue evaluate_assignment(FieldAssignment const& assign);
    ConstraintValue invoke_native_function(std::string const& function_name,
                                           std::vector<FieldAssignment> const& args);
    ConstraintValue parse_native_result(char const* out_result) const;
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
};

void reset_stats();
Stats get_stats();
}  // namespace rhs_prof

} // namespace rulesforge

#endif // RHS_EXECUTOR_HPP
