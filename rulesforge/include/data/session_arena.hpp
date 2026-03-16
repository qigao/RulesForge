#ifndef SESSION_ARENA_HPP
#define SESSION_ARENA_HPP

#include <turbo_buffer.h>

#include <cstddef>
#include <functional>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>

/**
 * @brief Exception thrown when session memory limit is exceeded.
 */
class SessionMemoryExhaustedException : public std::runtime_error {
public:
  SessionMemoryExhaustedException(size_t requested, size_t available, size_t max_size)
      : std::runtime_error(format_message(requested, available, max_size)), requested_(requested),
        available_(available), max_size_(max_size) {}

  size_t requested() const { return requested_; }
  size_t available() const { return available_; }
  size_t max_size() const { return max_size_; }

private:
  static std::string format_message(size_t requested, size_t available, size_t max_size) {
    std::ostringstream oss;
    oss << "Session memory exhausted: requested " << requested / 1024 << "KB, "
        << "available " << available / 1024 << "KB, "
        << "max " << max_size / 1024 << "KB. "
        << "Consider increasing session memory limit or optimizing rules.";
    return oss.str();
  }

  size_t requested_;
  size_t available_;
  size_t max_size_;
};

/**
 * @brief Callback type for memory pressure warnings.
 *
 * Called when memory usage exceeds the warning threshold.
 * @param used Current bytes used
 * @param max_size Maximum allowed bytes
 * @param usage_percent Current usage as percentage (0-100)
 */
using MemoryPressureCallback = std::function<void(size_t used, size_t max_size, int usage_percent)>;

/**
 * @brief Per-session arena allocator using TurboNet's mem_pool_t
 *
 * The arena is used for temporary objects that live only during a single
 * fire_all_rules() call.
 *
 * Features:
 * - Configurable maximum memory limit (default 64MB)
 * - Memory pressure warnings at configurable threshold (default 80%)
 * - Clear exception with context when limit exceeded
 * - Statistics tracking (used, peak, available)
 */
class SessionArena {
public:
  static constexpr size_t DEFAULT_MAX_SIZE = 64 * 1024 * 1024;
  static constexpr int DEFAULT_WARNING_THRESHOLD_PERCENT = 80;

  explicit SessionArena(size_t max_size = DEFAULT_MAX_SIZE)
      : max_size_(max_size),
        warning_threshold_percent_(DEFAULT_WARNING_THRESHOLD_PERCENT),
        warning_fired_(false),
        peak_used_(0) {
    mem_init(&arena_, max_size);
  }

  ~SessionArena() {
    mem_destroy(&arena_);
  }

  // Non-copyable, non-movable
  SessionArena(SessionArena const&) = delete;
  SessionArena& operator=(SessionArena const&) = delete;
  SessionArena(SessionArena&&) = delete;
  SessionArena& operator=(SessionArena&&) = delete;

  /**
   * @brief Allocate and construct object (freed on reset)
   *
   * @throws SessionMemoryExhaustedException if max_size exceeded
   */
  template <typename T, typename... Args>
  T* allocate(Args&&... args) {
    check_memory_pressure();

    void* ptr = mem_alloc(&arena_, sizeof(T));
    if (!ptr) {
      throw SessionMemoryExhaustedException(sizeof(T), memory_available(), max_size_);
    }
    update_peak();
    return new (ptr) T(std::forward<Args>(args)...);
  }

  /**
   * @brief Reset all temporary allocations (call after fire_all_rules)
   */
  void reset_temporaries() {
    mem_reset(&arena_);
    warning_fired_ = false;
  }

  // Configuration
  void set_max_size(size_t max_size) {
    max_size_ = max_size;
  }

  size_t get_max_size() const { return max_size_; }

  void set_warning_threshold(int percent) {
    warning_threshold_percent_ = (std::max)(0, (std::min)(100, percent));
  }

  void set_memory_pressure_callback(MemoryPressureCallback callback) {
    pressure_callback_ = std::move(callback);
  }

  // Statistics
  size_t memory_used() const {
    return t_atomic_load_size_relaxed((t_atomic_size_t*)&arena_.total_used);
  }
  size_t memory_available() const {
    size_t used = memory_used();
    return max_size_ > used ? max_size_ - used : 0;
  }
  size_t memory_peak() const { return peak_used_; }

  int usage_percent() const {
    if (max_size_ == 0) return 0;
    return static_cast<int>((memory_used() * 100) / max_size_);
  }

  std::string format_stats() const {
    std::ostringstream oss;
    oss << "SessionArena: "
        << "used=" << memory_used() / 1024 << "KB (" << usage_percent() << "%), "
        << "peak=" << peak_used_ / 1024 << "KB, "
        << "max=" << max_size_ / 1024 << "KB";
    return oss.str();
  }

private:
  void check_memory_pressure() {
    if (!pressure_callback_ || warning_fired_) return;

    int percent = usage_percent();
    if (percent >= warning_threshold_percent_) {
      warning_fired_ = true;
      pressure_callback_(memory_used(), max_size_, percent);
    }
  }

  void update_peak() {
    size_t used = memory_used();
    if (used > peak_used_) {
      peak_used_ = used;
    }
  }

  mem_pool_t arena_;
  size_t max_size_;
  int warning_threshold_percent_;
  bool warning_fired_;
  size_t peak_used_;
  MemoryPressureCallback pressure_callback_;
};

/**
 * @brief RAII scope guard for temporary allocations
 *
 * Note: mem_pool_t doesn't support mark/rewind, so this scope
 * guard is a no-op placeholder for API compatibility.
 */
class ArenaScope {
public:
  explicit ArenaScope(SessionArena& /*arena*/) {}
  ~ArenaScope() = default;

  ArenaScope(ArenaScope const&) = delete;
  ArenaScope& operator=(ArenaScope const&) = delete;
};

#endif // SESSION_ARENA_HPP
