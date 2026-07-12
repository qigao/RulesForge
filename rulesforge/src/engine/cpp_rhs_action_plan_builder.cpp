#include "engine/cpp_rhs_action_plan_builder.hpp"

#include <sstream>

namespace rulesforge {
namespace {

constexpr char const* kCommandFnName = "rulesforge_rhs_cmd";
constexpr char const* kEvalFnName = "rulesforge_rhs_eval";
constexpr char const* kForFnName = "rulesforge_rhs_for";
constexpr char const* kWhileFnName = "rulesforge_rhs_while";
constexpr char const* kBreakFnName = "rulesforge_rhs_break";
constexpr char const* kContinueFnName = "rulesforge_rhs_continue";
constexpr std::size_t kNoSwitchCaseIndex = static_cast<std::size_t>(-1);

bool is_linear_command_action(CompiledAction const& action) {
    switch (action.type) {
        case RhsActionType::UPDATE:
        case RhsActionType::INSERT:
        case RhsActionType::INSERT_LOGICAL:
        case RhsActionType::RETRACT:
        case RhsActionType::HALT:
        case RhsActionType::SET_FOCUS:
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

} // namespace

bool CppRhsActionPlanBuilder::build(std::vector<CompiledAction> const& actions,
                                    CppRhsActionScript& out,
                                    std::string* reason_out,
                                    bool allow_loop_control) {
    out = CppRhsActionScript{};
    if (actions.empty()) {
        return true;
    }

    std::ostringstream script;
    auto append_actions = [&](auto const& self,
                              std::vector<CompiledAction> const& items,
                              std::ostringstream& target,
                              bool in_loop,
                              CppRhsActionScript& script_out) -> bool {
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
                CppRhsActionScript body_script_probe;
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
                CppRhsActionScript body_script_probe;
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
        out = CppRhsActionScript{};
        return false;
    }

    out.root_actions.reserve(actions.size());
    for (auto const& action : actions) {
        out.root_actions.push_back(&action);
    }
    out.script = script.str();
    return true;
}

} // namespace rulesforge
