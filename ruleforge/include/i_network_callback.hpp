// FILE: i_network_callback.hpp
#ifndef I_NETWORK_CALLBACK_HPP
#define I_NETWORK_CALLBACK_HPP

#include <map>
#include <memory>
#include <optional>
#include <quickjs.h>
#include <string>
#include <functional>
// Forward declarations for types used in the interface
struct Fact;
struct Token;
struct TokenWME;

/**
 * @class INetworkCallback
 * @brief A comprehensive interface defining all callback functions that components
 * like the TMS and JavaScript Manager need from the main Rete network.
 */
class INetworkCallback {
public:
    virtual ~INetworkCallback() = default;

    // Callbacks needed by TMS
    virtual std::optional<std::shared_ptr<Fact>> get_fact_by_id(int64_t id) = 0;
    virtual void retract_fact(std::shared_ptr<Fact> fact) = 0;

    // Callbacks needed by JSScriptingManager
    virtual void add_fact(std::shared_ptr<Fact> fact) = 0;
    virtual void update_fact(std::shared_ptr<Fact> fact, std::function<void(Fact&)> modifier) = 0;
    virtual void logical_insert(Token& token, std::shared_ptr<Fact> fact) = 0;
    virtual void set_focus(std::string const& group_name) = 0;
    virtual map<std::string, JSValue> const& get_global_values() const = 0;

    // P1 FIX: rfl.halt() support
    virtual void halt() = 0;

    // P1-001 FIX: Transactional semantics for RHS execution
    /**
     * @brief Begin a transaction for tracking fact changes during RHS execution.
     * Facts inserted/retracted after this call will be tracked for potential rollback.
     */
    virtual void begin_rhs_transaction() = 0;

    /**
     * @brief End the current RHS transaction.
     * @param commit If true, changes are kept. If false, all tracked changes are rolled back.
     */
    virtual void end_rhs_transaction(bool commit) = 0;
};

#endif   // I_NETWORK_CALLBACK_HPP


