#ifndef CPP_RHS_ACTION_PLAN_BUILDER_HPP
#define CPP_RHS_ACTION_PLAN_BUILDER_HPP

#include "core/rhs_actions.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/value_types.hpp"

struct Fact;

namespace rulesforge {

struct CppRhsActionScript {
    std::string script;
    std::vector<CompiledAction const*> root_actions;
    std::vector<CompiledAction const*> command_actions;
    std::vector<CompiledAction const*> for_actions;
    std::vector<CompiledAction const*> while_actions;
    std::vector<CompiledAction const*> condition_actions;
    std::vector<std::size_t> condition_switch_case_indices;
};

class CppRhsActionPlanBuilder {
public:
    static bool build(std::vector<CompiledAction> const& actions,
                      CppRhsActionScript& out,
                      std::string* reason_out = nullptr,
                      bool allow_loop_control = false);
};

} // namespace rulesforge

#endif // CPP_RHS_ACTION_PLAN_BUILDER_HPP
