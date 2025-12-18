#ifndef I_ENGINE_LISTENER_HPP
#define I_ENGINE_LISTENER_HPP

#include "rfl_rete_defs.hpp"   // For Fact

#include <memory>
#include <string>
#include <vector>

/**
 * @class IEngineListener
 * @brief An interface for monitoring and auditing the rule engine's lifecycle events.
 *
 * Implement this interface to create custom loggers, debuggers, or auditing tools.
 */
class IEngineListener {
public:
    virtual ~IEngineListener() = default;

    /**
     * @brief Called when a rule's conditions are met and it is placed on the agenda.
     * @param ruleName The name of the rule that was activated.
     * @param facts The complete set of facts that satisfied the rule's conditions.
     */
    virtual void on_activation_created(std::string const& ruleName, std::vector<std::shared_ptr<Fact>> const& facts) {}

    /**
     * @brief Called when a change in facts causes a previously-activated rule to no longer be valid.
     * @param ruleName The name of the rule that was retracted from the agenda.
     * @param facts The original set of facts that are no longer a valid match.
     */
    virtual void on_activation_retracted(std::string const& ruleName, std::vector<std::shared_ptr<Fact>> const& facts) {
    }

    /**
     * @brief Called immediately before a rule's 'then' block is executed.
     * @param ruleName The name of the rule about to fire.
     */
    virtual void before_rule_fired(std::string const& ruleName) {}

    /**
     * @brief Called immediately after a rule's 'then' block has finished executing.
     * @param ruleName The name of the rule that just fired.
     */
    virtual void after_rule_fired(std::string const& ruleName) {}
};

#endif   // I_ENGINE_LISTENER_HPP


