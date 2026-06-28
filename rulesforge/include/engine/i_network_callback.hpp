// FILE: i_network_callback.hpp
#ifndef I_NETWORK_CALLBACK_HPP
#define I_NETWORK_CALLBACK_HPP

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <functional>

#include "core/constraint_types.hpp"

namespace rulesforge {
struct ModifiedFieldsHint {
    static constexpr uint8_t kMaxInline = 4;
    std::array<std::string_view, kMaxInline> fields{};
    uint8_t count = 0;
    bool overflow = false;

    void add(std::string_view field) {
        if (overflow) return;
        for (uint8_t i = 0; i < count; ++i) {
            if (fields[i] == field) return;
        }
        if (count < kMaxInline) {
            fields[count++] = field;
        } else {
            overflow = true;
        }
    }

    bool contains(std::string_view field) const {
        if (overflow) return true;  // conservative dirty-field match
        for (uint8_t i = 0; i < count; ++i) {
            if (fields[i] == field) return true;
        }
        return false;
    }
};
}  // namespace rulesforge

// Forward declarations for types used in the interface
struct Fact;
struct Token;
struct TokenWME;

/**
 * @class INetworkCallback
 * @brief A comprehensive interface defining all callback functions that components
 * like the TMS and RHS Executor need from the main Rete network.
 */
class INetworkCallback {
public:
    virtual ~INetworkCallback() = default;

    // Callbacks needed by TMS
    virtual Fact* get_fact_by_id(int64_t id) = 0;
    virtual void retract_fact(Fact* fact) = 0;

    // Callbacks needed by RhsExecutor
    virtual void add_fact(Fact* fact) = 0;
    virtual void update_fact(Fact* fact, std::function<void(Fact&)> modifier) = 0;
    virtual void propagate_modify(Fact* fact,
                                  rulesforge::ModifiedFieldsHint const* changed_fields = nullptr) = 0;
    // Capture pre-update snapshot for transactional rollback (first update per fact).
    virtual void track_rhs_update_snapshot(Fact const& fact) = 0;
    virtual void logical_insert(Token& token, Fact* fact) = 0;
    virtual Fact* logical_insert(Fact const& fact) = 0;
    virtual void set_focus(std::string const& group_name) = 0;

    // P1-002: Factory for facts to be used in RHS
    virtual Fact* create_fact(std::string const& type) = 0;
    virtual std::optional<FieldType> get_declared_field_type(std::string const& fact_type,
                                                             std::string_view field_name) const = 0;

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
