#ifndef AGENDA_SELECTOR_HPP
#define AGENDA_SELECTOR_HPP

/**
 * @brief Agenda implementation selector
 *
 * This header automatically selects the appropriate Agenda implementation
 * based on compile-time configuration.
 *
 * Usage:
 *   #include "engine/agenda_selector.hpp"
 *
 *   // Use DefaultAgenda in your code
 *   DefaultAgenda agenda;
 *
 * Configuration:
 *   - Default: Original Agenda (precise salience, std::priority_queue)
 *   - With -DRULESFORGE_USE_AGENDA_V2: AgendaV2 (10x faster, MQTT optimized)
 *
 * CMake:
 *   cmake -DRULESFORGE_USE_AGENDA_V2=ON ..
 */

#ifdef RULESFORGE_USE_AGENDA_V2
    #include "engine/agenda_v2.hpp"
    using DefaultAgenda = AgendaV2;
    #define AGENDA_IMPLEMENTATION "AgendaV2 (high-performance)"
#else
    #include "engine/agenda.hpp"
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

#endif  // AGENDA_SELECTOR_HPP
