#ifndef SESSION_POOL_HPP
#define SESSION_POOL_HPP

#include "runtime/knowledge_base.hpp"
#include "runtime/stateful_session.hpp"


#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <vector>


namespace rulesforge {

/**
 * @brief Fixed-capacity pool of isolated StatefulSession leases
 *
 * For high-throughput scenarios like MQTT rule processing:
 * - Pre-allocates the requested number of sessions
 * - Replaces each returned session before publishing its slot
 * - Thread-safe acquire/release
 *
 * USAGE:
 * - Acquire session from pool
 * - Process facts and fire rules
 * - Release the lease; replacement failure leaves it outstanding
 *
 * CAPACITY AND LIFETIME:
 * - Bounds concurrent leases, not per-session fact memory
 * - Replacement briefly keeps both old and fresh sessions alive
 * - Borrowers and waiting threads must finish before pool destruction
 */
class SessionPool {
public:
  /**
   * @brief Create a session pool
   * @param kb Knowledge base (shared across all sessions)
   * @param pool_size Number of sessions to pre-allocate
   */
  explicit SessionPool(std::shared_ptr<KnowledgeBase> kb, size_t pool_size = 10)
      : kb_(std::move(kb)) {
    if (!kb_ || pool_size == 0)
      throw std::invalid_argument("SessionPool requires a knowledge base and a positive size");
    // Slot addresses stay fixed after construction. All requested sessions
    // must be created; constructor failure destroys the partial pool.
    pool_.reserve(pool_size);
    for (size_t i = 0; i < pool_size; ++i) {
      pool_.push_back({kb_->create_session(), SlotState::Available});
      available_.push(&pool_.back());
    }
  }

  ~SessionPool() = default;

  // Non-copyable
  SessionPool(SessionPool const &) = delete;
  SessionPool &operator=(SessionPool const &) = delete;

  /**
   * @brief Acquire a session from the pool
   * @param blocking If true, wait for available session; if false, return nullptr when empty
   * @return Session pointer, or nullptr if non-blocking and pool is empty
   */
  StatefulSession *acquire(bool blocking = true) {
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

    Slot *slot = available_.front();
    available_.pop();
    slot->state = SlotState::Borrowed;
    return slot->session.get();
  }

  /**
   * @brief Release a session back to the pool
   * @param session An outstanding lease from this pool; invalid leases fail.
   * A fresh session replaces it before publication. Creation/allocation errors
   * propagate, leaving the old lease outstanding and unavailable to others.
   */
  void release(StatefulSession *session) {
    Slot *returning = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto slot = std::find_if(pool_.begin(), pool_.end(), [session](Slot const &item) {
        return item.session.get() == session;
      });
      if (slot == pool_.end() || slot->state != SlotState::Borrowed)
        throw std::invalid_argument("SessionPool release requires an outstanding pool lease");
      returning = &*slot;
      returning->state = SlotState::Returning;
    }
    struct ReturnAttempt {
      SessionPool &owner;
      Slot *slot;
      ~ReturnAttempt() {
        if (slot) {
          std::lock_guard<std::mutex> lock(owner.mutex_);
          slot->state = SlotState::Borrowed;
        }
      }
    } attempt{*this, returning};
    // Priming may invoke application predicates. Never run it under the
    // pool mutex; the reserved slot cannot be returned a second time.
    auto replacement = kb_->create_session();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      available_.push(returning); // Allocation can fail before publication.
      returning->session = std::move(replacement);
      returning->state = SlotState::Available;
      attempt.slot = nullptr;
    }

    // Notify waiting threads
    cv_.notify_one();
  }

  /**
   * @brief Get pool statistics
   */
  size_t total_size() const { return pool_.size(); }

  size_t available_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return available_.size();
  }

  size_t in_use_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pool_.size() - available_.size();
  }

private:
  enum class SlotState { Available, Borrowed, Returning };
  struct Slot {
    std::unique_ptr<StatefulSession> session;
    SlotState state;
  };
  std::shared_ptr<KnowledgeBase> kb_;
  std::vector<Slot> pool_;
  std::queue<Slot *> available_;
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
  SessionGuard(SessionPool &pool, bool blocking = true)
      : pool_(pool), session_(pool.acquire(blocking)) {}

  ~SessionGuard() {
    if (session_) {
      pool_.release(session_);
    }
  }

  // Non-copyable
  SessionGuard(SessionGuard const &) = delete;
  SessionGuard &operator=(SessionGuard const &) = delete;

  // Movable
  SessionGuard(SessionGuard &&other) noexcept : pool_(other.pool_), session_(other.session_) {
    other.session_ = nullptr;
  }

  StatefulSession *get() const { return session_; }
  StatefulSession *operator->() const { return session_; }
  explicit operator bool() const { return session_ != nullptr; }

private:
  SessionPool &pool_;
  StatefulSession *session_;
};

} // namespace rulesforge

#endif // SESSION_POOL_HPP
