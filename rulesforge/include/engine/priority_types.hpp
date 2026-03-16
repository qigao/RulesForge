#ifndef PRIORITY_TYPES_HPP
#define PRIORITY_TYPES_HPP

#include <cstdint>

// Priority enum is now in TurboNet's bucket_priority_queue.h
#include "bucket_priority_queue.h"

// C++ enum class wrapper for type safety
enum class Priority : int {
    LOW = BUCKET_PRIORITY_LOW,
    NORMAL = BUCKET_PRIORITY_NORMAL,
    HIGH = BUCKET_PRIORITY_HIGH,
    CRITICAL = BUCKET_PRIORITY_CRITICAL,
    NUM_PRIORITIES = BUCKET_PRIORITY_COUNT
};

/**
 * @brief Entry stored in ring buffer priority queue (DEPRECATED)
 *
 * MIGRATION NOTE:
 * - PriorityEntry is deprecated and no longer used by PriorityRingBuffer/PriorityDisruptor
 * - New implementation stores only activation_hash (size_t) for efficiency
 * - Priority is used for ordering but not returned on dequeue
 * - This struct is kept for backward compatibility only
 *
 * Old size: 64 bytes (1 cache line)
 * New size: 8 bytes (just the hash)
 * Performance improvement: 8x less memory, better cache utilization
 */
struct PriorityEntry {
    size_t activation_hash;
    Priority priority;
    uint8_t padding[3];  // Align to 16 bytes (Priority is 4 bytes)

    // Reserved for future use (e.g., timestamp, metadata)
    uint64_t reserved[6];
};

static_assert(sizeof(PriorityEntry) == 64, "PriorityEntry must be 64 bytes (1 cache line)");

/**
 * @brief Helper to map salience to priority
 *
 * RulesForge-specific mapping from arbitrary salience values to fixed priority levels.
 */
inline Priority salience_to_priority(int salience) {
    if (salience >= 1000) return Priority::CRITICAL;
    if (salience >= 0) return Priority::HIGH;
    if (salience >= -1000) return Priority::NORMAL;
    return Priority::LOW;
}

#endif  // PRIORITY_TYPES_HPP
