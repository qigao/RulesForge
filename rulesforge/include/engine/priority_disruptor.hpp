#ifndef PRIORITY_DISRUPTOR_HPP
#define PRIORITY_DISRUPTOR_HPP

#include <bucket_priority_queue_mpmc.h>

#include "engine/priority_types.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <vector>

/**
 * @brief High-throughput priority queue using TurboNet bucket_priority_queue_mpmc
 *
 * DESIGN PHILOSOPHY (inspired by Linus & TurboNet):
 * - No locks in hot path (lock-free disruptor)
 * - Pre-allocated memory (no malloc in hot path)
 * - Cache-line aligned (prevent false sharing)
 * - Simple priority model (fixed priority levels)
 *
 * MIGRATION NOTE:
 * - Now uses TurboNet's bucket_priority_queue_mpmc internally
 * - Simplified: stores only activation_hash (size_t), not full PriorityEntry
 * - Each consumer must track their own sequence numbers
 *
 * USAGE SCENARIO:
 * - MQTT high-throughput message processing
 * - Multi-producer (multiple MQTT connections)
 * - Single or multi-consumer (worker pool)
 * - Priority-based scheduling (QoS 0/1/2)
 *
 * PERFORMANCE:
 * - 7M+ ops/sec on modern hardware (single-threaded)
 * - 1.2M+ ops/sec (multi-threaded 1P+1C)
 * - Sub-microsecond latency
 * - Zero allocation in hot path
 */
class PriorityDisruptor {
public:
    struct Config {
        uint64_t capacity_per_priority = 4096;  // Must be power of 2
        uint32_t max_consumers = 4;
    };

    explicit PriorityDisruptor(Config const& config = Config{}) {
        if (!bucket_priority_queue_mpmc_init(&queue_, config.capacity_per_priority, config.max_consumers)) {
            throw std::runtime_error("Failed to initialize bucket_priority_queue_mpmc");
        }
    }

    ~PriorityDisruptor() {
        bucket_priority_queue_mpmc_destroy(&queue_);
    }

    // Non-copyable, non-movable
    PriorityDisruptor(PriorityDisruptor const&) = delete;
    PriorityDisruptor& operator=(PriorityDisruptor const&) = delete;

    /**
     * @brief Try to enqueue (non-blocking)
     * @return true if enqueued, false if full
     */
    bool try_enqueue(Priority priority, size_t activation_hash) {
        auto mpmc_priority = static_cast<bucket_priority_mpmc_t>(static_cast<int>(priority));
        return bucket_priority_queue_mpmc_try_push(&queue_, mpmc_priority, activation_hash);
    }

    /**
     * @brief Enqueue (blocking if full)
     */
    void enqueue_blocking(Priority priority, size_t activation_hash) {
        auto mpmc_priority = static_cast<bucket_priority_mpmc_t>(static_cast<int>(priority));
        bucket_priority_queue_mpmc_push_blocking(&queue_, mpmc_priority, activation_hash);
    }

    /**
     * @brief Try to dequeue from highest priority (non-blocking)
     * @return activation_hash if available, nullopt if all queues empty
     */
    std::optional<size_t> try_dequeue() {
        size_t activation_hash;
        if (bucket_priority_queue_mpmc_try_pop(&queue_, &activation_hash)) {
            return activation_hash;
        }
        return std::nullopt;
    }

    /**
     * @brief Dequeue (blocking with timeout)
     * @param timeout_ms Timeout in milliseconds
     * @return activation_hash if available within timeout, nullopt otherwise
     */
    std::optional<size_t> dequeue_blocking(uint32_t timeout_ms = 1000) {
        size_t activation_hash;
        if (bucket_priority_queue_mpmc_pop_blocking(&queue_, &activation_hash, timeout_ms)) {
            return activation_hash;
        }
        return std::nullopt;
    }

    /**
     * @brief Get approximate size (for monitoring)
     */
    bool empty() const {
        return bucket_priority_queue_mpmc_empty(&queue_);
    }

private:
    bucket_priority_queue_mpmc_t queue_;
};

#endif  // PRIORITY_DISRUPTOR_HPP
