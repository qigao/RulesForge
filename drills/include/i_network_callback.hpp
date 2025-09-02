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
    virtual std::map<std::string, JSValue> const& get_global_values() const = 0;
};

#endif   // I_NETWORK_CALLBACK_HPP
