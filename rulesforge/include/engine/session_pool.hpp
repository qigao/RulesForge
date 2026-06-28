#ifndef SESSION_POOL_HPP
#define SESSION_POOL_HPP

#include "engine/stateful_session.hpp"
#include "engine/knowledge_base.hpp"

#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace rulesforge {

/**
 * @brief Pool of reusable StatefulSession instances
 *
 * For high-throughput scenarios like MQTT rule processing:
 * - Pre-allocates sessions to avoid initialization overhead
 * - Reuses sessions to reduce memory allocation
 * - Thread-safe acquire/release
 *
 * USAGE:
 * - Acquire session from pool
 * - Process facts and fire rules
 * - Release session back to pool (auto-reset)
 *
 * PERFORMANCE:
 * - Eliminates session creation overhead
 * - Bounded memory usage (fixed pool size)
 * - Suitable for stateless rule processing
 */
class SessionPool {
public:
    /**
     * @brief Create a session pool
     * @param kb Knowledge base (shared across all sessions)
     * @param pool_size Number of sessions to pre-allocate
     */
    explicit SessionPool(std::shared_ptr<KnowledgeBase const> kb, size_t pool_size = 10)
        : kb_(std::move(kb)) {

        // Pre-allocate sessions
        pool_.reserve(pool_size);
        for (size_t i = 0; i < pool_size; ++i) {
            try {
                // Need to cast away const to call create_session()
                auto* mutable_kb = const_cast<KnowledgeBase*>(kb_.get());
                auto session = mutable_kb->create_session();
                if (session) {
                    available_.push(session.get());
                    pool_.push_back(std::move(session));
                }
            } catch (...) {
                // If session creation fails, just skip it
                // Pool will have fewer sessions than requested
            }
        }

        if (pool_.empty()) {
            throw std::runtime_error("Failed to create any sessions in pool");
        }
    }

    ~SessionPool() = default;

    // Non-copyable
    SessionPool(SessionPool const&) = delete;
    SessionPool& operator=(SessionPool const&) = delete;

    /**
     * @brief Acquire a session from the pool
     * @param blocking If true, wait for available session; if false, return nullptr when empty
     * @return Session pointer, or nullptr if non-blocking and pool is empty
     */
    StatefulSession* acquire(bool blocking = true) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (blocking) {
            // Wait until a session is available
            cv_.wait(lock, [this]() { return !available_.empty(); });
        } else {
            // Non-blocking: return nullptr if empty
            if (available_.empty()) {
                return nullptr;
            }
        }

        StatefulSession* session = available_.front();
        available_.pop();
        return session;
    }

    /**
     * @brief Release a session back to the pool
     * @param session Session to release (will be reset)
     */
    void release(StatefulSession* session) {
        if (!session) return;

        // Prefer replacing with a fresh session to guarantee zero cross-request state leakage.
        std::shared_ptr<StatefulSession> replacement;
        try {
            auto* mutable_kb = const_cast<KnowledgeBase*>(kb_.get());
            auto fresh_session = mutable_kb->create_session();
            if (fresh_session) {
                replacement = std::shared_ptr<StatefulSession>(std::move(fresh_session));
            }
        } catch (...) {
            // Fall back to best-effort reset below.
        }

        StatefulSession* returned_session = session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bool replaced = false;
            if (replacement) {
                for (auto& pooled : pool_) {
                    if (pooled.get() == session) {
                        pooled = std::move(replacement);
                        returned_session = pooled.get();
                        replaced = true;
                        break;
                    }
                }
            }

            if (!replaced) {
                // Best-effort replacement path when direct replacement is not available.
                try {
                    reset_session(session);
                } catch (...) {
                    // Keep the pool operational even if reset fails.
                }
                returned_session = session;
            }

            available_.push(returned_session);
        }

        // Notify waiting threads
        cv_.notify_one();
    }

    /**
     * @brief Get pool statistics
     */
    size_t total_size() const {
        return pool_.size();
    }

    size_t available_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return available_.size();
    }

    size_t in_use_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.size() - available_.size();
    }

private:
    void reset_session(StatefulSession* session) {
        // Reset session state for reuse
        session->reset();
    }

    std::shared_ptr<KnowledgeBase const> kb_;
    std::vector<std::shared_ptr<StatefulSession>> pool_;
    std::queue<StatefulSession*> available_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

/**
 * @brief RAII wrapper for automatic session release
 *
 * USAGE:
 *   SessionGuard guard(pool);
 *   auto* session = guard.get();
 *   session->add_fact(fact);
 *   session->fire_all_rules();
 *   // Automatically released when guard goes out of scope
 */
class SessionGuard {
public:
    SessionGuard(SessionPool& pool, bool blocking = true)
        : pool_(pool), session_(pool.acquire(blocking)) {}

    ~SessionGuard() {
        if (session_) {
            pool_.release(session_);
        }
    }

    // Non-copyable
    SessionGuard(SessionGuard const&) = delete;
    SessionGuard& operator=(SessionGuard const&) = delete;

    // Movable
    SessionGuard(SessionGuard&& other) noexcept
        : pool_(other.pool_), session_(other.session_) {
        other.session_ = nullptr;
    }

    StatefulSession* get() const { return session_; }
    StatefulSession* operator->() const { return session_; }
    explicit operator bool() const { return session_ != nullptr; }

private:
    SessionPool& pool_;
    StatefulSession* session_;
};

} // namespace rulesforge

#endif // SESSION_POOL_HPP
