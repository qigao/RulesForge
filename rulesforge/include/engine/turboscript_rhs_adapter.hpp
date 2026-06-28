#ifndef TURBOSCRIPT_RHS_ADAPTER_HPP
#define TURBOSCRIPT_RHS_ADAPTER_HPP

#include "core/rhs_actions.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/value_types.hpp"

struct Fact;

namespace rulesforge {

struct TurboScriptRhsCommandScript {
    std::string script;
    std::vector<CompiledAction const*> command_actions;
    std::vector<CompiledAction const*> for_actions;
    std::vector<CompiledAction const*> while_actions;
    std::vector<CompiledAction const*> condition_actions;
    std::vector<std::size_t> condition_switch_case_indices;
};

enum class TurboScriptRhsCommandEventType : uint8_t {
    Command,
    ForEach,
    WhileLoop,
    Break,
    Continue
};

struct TurboScriptRhsCommandEvent {
    TurboScriptRhsCommandEventType type = TurboScriptRhsCommandEventType::Command;
    std::size_t action_index = 0;
    std::map<std::string, ::Fact*> temp_bindings;
};

class TurboScriptRhsAdapter {
public:
    static bool build_command_script(std::vector<CompiledAction> const& actions,
                                     TurboScriptRhsCommandScript& out,
                                     std::string* reason_out = nullptr,
                                     bool allow_loop_control = false);

    static bool execute_command_script(TurboScriptRhsCommandScript const& script,
                                       std::vector<std::size_t>& out_action_indices,
                                       std::string* error_out = nullptr);

    static bool evaluate_boolean_expression(std::string const& expression,
                                            std::vector<std::string> const& variables,
                                            std::function<ConstraintValue(std::string const&)> const& resolver,
                                            bool& out_value,
                                            std::string* error_out = nullptr);

    static bool evaluate_expression(std::string const& expression,
                                    std::vector<std::string> const& variables,
                                    std::function<ConstraintValue(std::string const&)> const& resolver,
                                    ConstraintValue& out_value,
                                    std::string* error_out = nullptr);
};

class TurboScriptRhsProgram {
public:
    TurboScriptRhsProgram(TurboScriptRhsProgram const&) = delete;
    TurboScriptRhsProgram& operator=(TurboScriptRhsProgram const&) = delete;
    TurboScriptRhsProgram(TurboScriptRhsProgram&&) = delete;
    TurboScriptRhsProgram& operator=(TurboScriptRhsProgram&&) = delete;
    ~TurboScriptRhsProgram();

    static std::unique_ptr<TurboScriptRhsProgram> compile(std::string const& script,
                                                          std::string* error_out = nullptr);

    bool execute(std::vector<TurboScriptRhsCommandEvent>& out_events,
                 std::function<bool(std::size_t)> const& eval_condition,
                 std::string* error_out = nullptr);

    bool execute(std::vector<std::size_t>& out_action_indices,
                 std::function<bool(std::size_t)> const& eval_condition,
                 std::string* error_out = nullptr);

    bool execute(std::vector<std::size_t>& out_action_indices,
                 std::string* error_out = nullptr);

private:
    struct Impl;
    explicit TurboScriptRhsProgram(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

class TurboScriptExpressionProgram {
public:
    TurboScriptExpressionProgram(TurboScriptExpressionProgram const&) = delete;
    TurboScriptExpressionProgram& operator=(TurboScriptExpressionProgram const&) = delete;
    TurboScriptExpressionProgram(TurboScriptExpressionProgram&&) = delete;
    TurboScriptExpressionProgram& operator=(TurboScriptExpressionProgram&&) = delete;
    ~TurboScriptExpressionProgram();

    static std::unique_ptr<TurboScriptExpressionProgram> compile(
        std::string const& expression,
        std::vector<std::string> const& variables,
        std::string* error_out = nullptr);

    bool execute(std::function<ConstraintValue(std::string const&)> const& resolver,
                 ConstraintValue& out_value,
                 std::string* error_out = nullptr);

private:
    struct Impl;
    explicit TurboScriptExpressionProgram(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace rulesforge

#endif // TURBOSCRIPT_RHS_ADAPTER_HPP
