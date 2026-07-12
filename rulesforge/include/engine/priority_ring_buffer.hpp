#ifndef PRIORITY_RING_BUFFER_HPP
#define PRIORITY_RING_BUFFER_HPP

#include <bucket_priority_queue_spsc.h>

#include "engine/priority_types.hpp"

#include <optional>
#include <stdexcept>
#include <vector>

/**
 * @brief High-performance priority queue using TurboUtils bucket_priority_queue_spsc
 *
 * DESIGN PHILOSOPHY (Linus-style "good taste"):
 * - Lock-free SPSC (faster than MPMC disruptor for single producer)
 * - Pre-allocated (no runtime expansion for thread-safety)
 * - Simple priority model (fixed levels)
 *
 * MIGRATION NOTE:
 * - Now uses TurboUtils's bucket_priority_queue_spsc internally
 * - Simplified: stores only activation_hash (size_t), not full PriorityEntry
 * - Priority is used for ordering, but not returned on dequeue
 *
 * USAGE SCENARIO:
 * - Single MQTT connection thread (producer)
 * - Single rule engine thread (consumer)
 * - Lower overhead than disruptor when SPSC is sufficient
 *
 * PERFORMANCE:
 * - 9M+ ops/sec (measured in TurboUtils benchmarks)
 * - ~100ns latency
 * - Zero allocation in hot path, zero locks
 *
 * TRADE-OFFS vs disruptor:
 * - Pro: Faster for SPSC (no CAS loops)
 * - Pro: Lower memory overhead (8 bytes vs 64 bytes per entry)
 * - Con: Only supports single producer/consumer
 * - Con: No runtime capacity expansion (must pre-allocate)
 */
class PriorityRingBuffer {
public:
    struct Config {
        size_t capacity_per_priority = 4096;  // Must be power of 2
    };

    PriorityRingBuffer()
        : PriorityRingBuffer(Config{}) {}

    explicit PriorityRingBuffer(Config const& config) {
        if (!bucket_priority_queue_spsc_init(&queue_, config.capacity_per_priority)) {
            throw std::runtime_error("Failed to initialize bucket_priority_queue_spsc");
        }
    }

    ~PriorityRingBuffer() {
        bucket_priority_queue_spsc_destroy(&queue_);
    }

    // Non-copyable, non-movable
    PriorityRingBuffer(PriorityRingBuffer const&) = delete;
    PriorityRingBuffer& operator=(PriorityRingBuffer const&) = delete;

    /**
     * @brief Try to enqueue (non-blocking)
     * @return true if enqueued, false if full
     *
     * NOTE: Returns false if queue is full. No runtime expansion.
     * Ensure capacity_per_priority is large enough for your workload.
     */
    bool try_enqueue(Priority priority, size_t activation_hash) {
        auto spsc_priority = static_cast<bucket_priority_spsc_t>(static_cast<int>(priority));
        return bucket_priority_queue_spsc_push(&queue_, spsc_priority, activation_hash);
    }

    /**
     * @brief Enqueue with retry (blocking if full)
     *
     * Retries until successful. Use with caution - may spin indefinitely if
     * consumer is not keeping up.
     */
    void enqueue_blocking(Priority priority, size_t activation_hash) {
        while (!try_enqueue(priority, activation_hash)) {
            // Spin - consumer will make space
        }
    }

    /**
     * @brief Try to dequeue from highest priority (non-blocking)
     * @return activation_hash if available, nullopt if all queues empty
     *
     * NOTE: Priority information is not returned. It was used for ordering,
     * but is not needed after dequeue.
     */
    std::optional<size_t> try_dequeue() {
        size_t activation_hash;
        if (bucket_priority_queue_spsc_pop(&queue_, &activation_hash)) {
            return activation_hash;
        }
        return std::nullopt;
    }

    /**
     * @brief Dequeue from highest priority (blocking)
     */
    size_t dequeue_blocking() {
        for (;;) {
            auto hash = try_dequeue();
            if (hash) {
                return *hash;
            }
            // Spin - producer will add items
        }
    }

    /**
     * @brief Batch dequeue (more efficient)
     * @param max_items Maximum items to dequeue
     * @return Vector of activation hashes (may be less than max_items)
     */
    std::vector<size_t> try_dequeue_batch(size_t max_items) {
        if (max_items == 0) {
            return {};
        }

        std::vector<size_t> result;
        result.resize(max_items);

        size_t count = bucket_priority_queue_spsc_pop_batch(&queue_, max_items, result.data());
        result.resize(count);

        return result;
    }

    /**
     * @brief Check if empty
     */
    bool empty() const {
        return bucket_priority_queue_spsc_empty(&queue_);
    }

    /**
     * @brief Get total size (approximate)
     */
    size_t size() const {
        return bucket_priority_queue_spsc_size(&queue_);
    }

    /**
     * @brief Get available space for writing (approximate)
     */
    size_t available_space(Priority priority) const {
        auto spsc_priority = static_cast<bucket_priority_spsc_t>(static_cast<int>(priority));
        return bucket_priority_queue_spsc_capacity_at(&queue_, spsc_priority) -
               bucket_priority_queue_spsc_size_at(&queue_, spsc_priority);
    }

private:
    bucket_priority_queue_spsc_t queue_;
};

#endif  // PRIORITY_RING_BUFFER_HPP
