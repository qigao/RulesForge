#ifndef BUCKET_PRIORITY_QUEUE_HPP
#define BUCKET_PRIORITY_QUEUE_HPP

// BucketPriorityQueue has been moved to TurboUtils
// Include from TurboUtils shared utils
#include "bucket_priority_queue.h"

// Priority enum is now defined in priority_types.hpp
#include "engine/priority_types.hpp"

#include <cstring>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

/**
 * @brief C++ wrapper for TurboUtils's bucket_priority_queue (single-threaded)
 *
 * This is a thin wrapper around TurboUtils's C API for backward compatibility.
 * For new code, consider using PriorityRingBuffer (SPSC) or PriorityDisruptor (MPMC).
 *
 * PERFORMANCE:
 * - 9M+ ops/sec (single-threaded)
 * - Auto-grows capacity (unlike SPSC version)
 * - Not thread-safe
 */
class BucketPriorityQueue {
public:
    explicit BucketPriorityQueue(size_t initial_capacity = 1024) {
        if (!bucket_priority_queue_init(&queue_, initial_capacity)) {
            throw std::runtime_error("Failed to initialize bucket_priority_queue");
        }
    }

    ~BucketPriorityQueue() {
        bucket_priority_queue_destroy(&queue_);
    }

    // Non-copyable
    BucketPriorityQueue(BucketPriorityQueue const&) = delete;
    BucketPriorityQueue& operator=(BucketPriorityQueue const&) = delete;

    // Movable
    BucketPriorityQueue(BucketPriorityQueue&& other) noexcept
        : queue_(other.queue_) {
        // Zero out the moved-from queue to prevent double-free
        std::memset(&other.queue_, 0, sizeof(other.queue_));
    }

    BucketPriorityQueue& operator=(BucketPriorityQueue&& other) noexcept {
        if (this != &other) {
            // Destroy current queue
            bucket_priority_queue_destroy(&queue_);
            // Move from other
            queue_ = other.queue_;
            // Zero out the moved-from queue
            std::memset(&other.queue_, 0, sizeof(other.queue_));
        }
        return *this;
    }

    /**
     * @brief Push a value with given priority
     */
    void push(Priority priority, size_t value) {
        auto c_priority = static_cast<bucket_priority_t>(static_cast<int>(priority));
        if (!bucket_priority_queue_push(&queue_, c_priority, value)) {
            throw std::runtime_error("Failed to push to bucket_priority_queue");
        }
    }

    /**
     * @brief Pop highest priority value
     * @return value if available, nullopt if empty
     */
    std::optional<size_t> pop() {
        size_t value;
        if (bucket_priority_queue_pop(&queue_, &value)) {
            return value;
        }
        return std::nullopt;
    }

    /**
     * @brief Pop multiple values in priority order
     * @param max_items Maximum number of items to pop
     * @return Vector of values (may be less than max_items)
     */
    std::vector<size_t> pop_batch(size_t max_items) {
        std::vector<size_t> result;
        result.resize(max_items);

        size_t count = bucket_priority_queue_pop_batch(&queue_, max_items, result.data());
        result.resize(count);

        return result;
    }

    /**
     * @brief Peek at highest priority value without removing
     * @return value if available, nullopt if empty
     */
    std::optional<size_t> peek() const {
        size_t value;
        if (bucket_priority_queue_peek(&queue_, &value)) {
            return value;
        }
        return std::nullopt;
    }

    /**
     * @brief Check if queue is empty
     */
    bool empty() const {
        return bucket_priority_queue_empty(&queue_);
    }

    /**
     * @brief Get total size
     */
    size_t size() const {
        return bucket_priority_queue_size(&queue_);
    }

    /**
     * @brief Get size of specific priority bucket
     */
    size_t size(Priority priority) const {
        auto c_priority = static_cast<bucket_priority_t>(static_cast<int>(priority));
        return bucket_priority_queue_size_at(&queue_, c_priority);
    }

    /**
     * @brief Get capacity of specific priority bucket
     */
    size_t capacity(Priority priority) const {
        auto c_priority = static_cast<bucket_priority_t>(static_cast<int>(priority));
        return bucket_priority_queue_capacity_at(&queue_, c_priority);
    }

    /**
     * @brief Reserve capacity for all priority buckets
     */
    void reserve(size_t capacity_per_bucket) {
        if (!bucket_priority_queue_reserve(&queue_, capacity_per_bucket)) {
            throw std::runtime_error("Failed to reserve capacity");
        }
    }

    /**
     * @brief Shrink to fit (no-op for compatibility)
     *
     * Note: TurboUtils's bucket_priority_queue doesn't support shrinking.
     * This method is provided for API compatibility but does nothing.
     */
    void shrink_to_fit() {
        // No-op: TurboUtils doesn't support shrinking
    }

    /**
     * @brief Swap contents with another queue
     */
    void swap(BucketPriorityQueue& other) noexcept {
        std::swap(queue_, other.queue_);
    }

    /**
     * @brief Clear all items
     */
    void clear() {
        bucket_priority_queue_clear(&queue_);
    }

private:
    bucket_priority_queue_t queue_;
};

#endif  // BUCKET_PRIORITY_QUEUE_HPP
