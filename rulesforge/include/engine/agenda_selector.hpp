#ifndef AGENDA_SELECTOR_HPP
#define AGENDA_SELECTOR_HPP

/**
 * @brief Rule execution mode selector
 *
 * This header provides both internal agenda implementations and a runtime wrapper.
 *
 * Usage:
 *   #include "engine/agenda_selector.hpp"
 *
 *   // Runtime-selected agenda.
 *   rulesforge::RuntimeAgenda agenda(rulesforge::AgendaImplementation::AgendaV2);
 *
 * User-facing modes:
 *   - v1_standard: exact salience ordering.
 *   - v2_high_performance: bucketed priority queues for higher throughput.
 *
 * RULESFORGE_USE_AGENDA_V2 only changes the default mode; callers can still
 * choose either execution mode per KnowledgeBase at runtime.
 */

#include "engine/agenda.hpp"
#include "engine/agenda_v2.hpp"

#include <cstdint>
#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#ifdef RULESFORGE_USE_AGENDA_V2
using DefaultAgenda = AgendaV2;
#define AGENDA_IMPLEMENTATION "AgendaV2 (high-performance)"
#else
using DefaultAgenda = Agenda;
#define AGENDA_IMPLEMENTATION "Agenda (original)"
#endif

// Compile-time information
namespace agenda_info {
    constexpr const char* implementation = AGENDA_IMPLEMENTATION;

    #ifdef RULESFORGE_USE_AGENDA_V2
    constexpr bool is_high_performance = true;
    constexpr bool has_fixed_priorities = true;
    constexpr bool is_lock_free = true;
    #else
    constexpr bool is_high_performance = false;
    constexpr bool has_fixed_priorities = false;
    constexpr bool is_lock_free = false;
    #endif
}

namespace rulesforge {

enum class AgendaImplementation {
    Agenda,
    AgendaV2,
};

enum class ExecutionMode {
    V1Standard,
    V2HighPerformance,
};

inline constexpr AgendaImplementation default_agenda_implementation() {
#ifdef RULESFORGE_USE_AGENDA_V2
    return AgendaImplementation::AgendaV2;
#else
    return AgendaImplementation::Agenda;
#endif
}

inline constexpr ExecutionMode default_execution_mode() {
#ifdef RULESFORGE_USE_AGENDA_V2
    return ExecutionMode::V2HighPerformance;
#else
    return ExecutionMode::V1Standard;
#endif
}

inline constexpr AgendaImplementation execution_mode_to_agenda_implementation(
    ExecutionMode mode) {
    switch (mode) {
        case ExecutionMode::V1Standard:
            return AgendaImplementation::Agenda;
        case ExecutionMode::V2HighPerformance:
            return AgendaImplementation::AgendaV2;
    }
    return AgendaImplementation::Agenda;
}

inline constexpr ExecutionMode agenda_implementation_to_execution_mode(
    AgendaImplementation implementation) {
    switch (implementation) {
        case AgendaImplementation::Agenda:
            return ExecutionMode::V1Standard;
        case AgendaImplementation::AgendaV2:
            return ExecutionMode::V2HighPerformance;
    }
    return ExecutionMode::V1Standard;
}

inline char const* agenda_implementation_name(AgendaImplementation implementation) {
    switch (implementation) {
        case AgendaImplementation::Agenda:
            return "standard";
        case AgendaImplementation::AgendaV2:
            return "high_performance";
    }
    return "standard";
}

inline char const* execution_mode_name(ExecutionMode mode) {
    switch (mode) {
        case ExecutionMode::V1Standard:
            return "v1_standard";
        case ExecutionMode::V2HighPerformance:
            return "v2_high_performance";
    }
    return "v1_standard";
}

class RuntimeAgenda {
public:
    struct PoppedActivationItem {
        int salience;
        Activation activation;
    };

    explicit RuntimeAgenda(
        AgendaImplementation implementation = default_agenda_implementation())
        : implementation_(implementation) {
        reset(implementation);
    }

    AgendaImplementation implementation() const { return implementation_; }
    std::string implementation_name() const {
        return agenda_implementation_name(implementation_);
    }

    void reset(AgendaImplementation implementation) {
        implementation_ = implementation;
        switch (implementation_) {
            case AgendaImplementation::Agenda:
                agenda_.emplace<Agenda>();
                break;
            case AgendaImplementation::AgendaV2:
                agenda_.emplace<AgendaV2>();
                break;
        }
    }

    void add_with_duration(Activation const& activation) {
        visit([&](auto& agenda) { agenda.add_with_duration(activation); });
    }

    void add_batch(std::vector<Activation> const& activations) {
        visit([&](auto& agenda) { agenda.add_batch(activations); });
    }

    void flush_ready_delayed() {
        visit([](auto& agenda) { agenda.flush_ready_delayed(); });
    }

    std::vector<PoppedActivationItem>
    pop_next_batch_activations(size_t max_items,
                               uint64_t* select_us = nullptr,
                               uint64_t* detach_us = nullptr) {
        return visit([&](auto& agenda) {
            auto raw = agenda.pop_next_batch_activations(max_items, select_us, detach_us);
            std::vector<PoppedActivationItem> out;
            out.reserve(raw.size());
            for (auto& item : raw) {
                out.push_back({item.salience, std::move(item.activation)});
            }
            return out;
        });
    }

    void requeue_batch_activations(std::vector<PoppedActivationItem> const& batch,
                                   size_t start_index = 0) {
        if (start_index >= batch.size()) return;
        for (size_t i = start_index; i < batch.size(); ++i) {
            visit([&](auto& agenda) { agenda.add(batch[i].activation); });
        }
    }

    void clear_noloop() { visit([](auto& agenda) { agenda.clear_noloop(); }); }
    void block_noloop(size_t key) { visit([&](auto& agenda) { agenda.block_noloop(key); }); }
    bool is_noloop_blocked(size_t key) const {
        return visit([&](auto const& agenda) { return agenda.is_noloop_blocked(key); });
    }

    void clear_lock_on_active() {
        visit([](auto& agenda) { agenda.clear_lock_on_active(); });
    }
    bool is_lock_on_active_blocked(ParsedRule const* rule) const {
        return visit([&](auto const& agenda) {
            return agenda.is_lock_on_active_blocked(rule);
        });
    }
    void seed_lock_on_active_for_focus() {
        visit([](auto& agenda) { agenda.seed_lock_on_active_for_focus(); });
    }

    void clear_activation_groups() {
        visit([](auto& agenda) { agenda.clear_activation_groups(); });
    }
    void cancel_activation_group(std::string const& group_name, size_t except_hash) {
        visit([&](auto& agenda) { agenda.cancel_activation_group(group_name, except_hash); });
    }

    void remove_activations_with_fact_id(int64_t fact_id) {
        visit([&](auto& agenda) { agenda.remove_activations_with_fact_id(fact_id); });
    }
    bool remove(size_t activation_hash) {
        return visit([&](auto& agenda) { return agenda.remove(activation_hash); });
    }

    void set_focus(std::string const& group_name) {
        visit([&](auto& agenda) { agenda.set_focus(group_name); });
    }
    std::string pop_focus() {
        return visit([](auto& agenda) { return agenda.pop_focus(); });
    }
    bool has_focus_override() const {
        return visit([](auto const& agenda) { return agenda.has_focus_override(); });
    }
    std::string get_focus() const {
        return visit([](auto const& agenda) { return agenda.get_focus(); });
    }

    std::size_t size() const {
        return visit([](auto const& agenda) { return agenda.size(); });
    }

private:
    template <typename Fn>
    decltype(auto) visit(Fn&& fn) {
        return std::visit(std::forward<Fn>(fn), agenda_);
    }

    template <typename Fn>
    decltype(auto) visit(Fn&& fn) const {
        return std::visit(std::forward<Fn>(fn), agenda_);
    }

    AgendaImplementation implementation_ = default_agenda_implementation();
    std::variant<Agenda, AgendaV2> agenda_;
};

} // namespace rulesforge

#endif  // AGENDA_SELECTOR_HPP
