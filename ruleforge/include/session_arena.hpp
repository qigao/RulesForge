#ifndef SESSION_ARENA_HPP
#define SESSION_ARENA_HPP

#include "memory.h"

#include <cstddef>
#include <string>
#include <sstream>
#include <functional>
#include <stdexcept>



/**
 * @brief Exception thrown when session memory limit is exceeded.
 *
 * P0-002 FIX: Provides clear error information when rules consume too much memory.
 */
class SessionMemoryExhaustedException : public std::runtime_error {
public:
    SessionMemoryExhaustedException(size_t requested, size_t available, size_t max_size)
        : std::runtime_error(format_message(requested, available, max_size))
        , requested_(requested)
        , available_(available)
        , max_size_(max_size) {}

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
 * @brief Per-session arena allocator for temporary allocations during rule firing
 *
 * P0-002 FIX: Added configurable maximum size and memory pressure callbacks.
 *
 * The arena is used for temporary objects that live only during a single
 * fire_all_rules() call. For objects with longer lifetimes (Facts, TokenWME),
 * we still use shared_ptr with mimalloc for now.
 *
 * Features:
 * - Configurable maximum memory limit (default 64MB)
 * - Memory pressure warnings at configurable threshold (default 80%)
 * - Clear exception with context when limit exceeded
 * - Statistics tracking (used, peak, available)
 *
 * Future optimization: migrate TokenWME to full arena allocation.
 */
class SessionArena {
public:
    // Default: 64MB max, warn at 80% usage
    static constexpr size_t DEFAULT_MAX_SIZE = 64 * 1024 * 1024;
    static constexpr int DEFAULT_WARNING_THRESHOLD_PERCENT = 80;

    explicit SessionArena(size_t max_size = DEFAULT_MAX_SIZE)
        : arena_(max_size)
        , max_size_(max_size)
        , warning_threshold_percent_(DEFAULT_WARNING_THRESHOLD_PERCENT)
        , warning_fired_(false) {}

    // Get underlying arena for direct use
    MemoryArena& get_arena() { return arena_; }

    /**
     * @brief Allocate temporary memory (freed on reset)
     *
     * @throws SessionMemoryExhaustedException if max_size exceeded
     */
    template<typename T, typename... Args>
    T* allocate(Args&&... args) {
        check_memory_pressure();

        try {
            return arena_.allocate<T>(std::forward<Args>(args)...);
        } catch (std::bad_alloc const&) {
            throw SessionMemoryExhaustedException(
                sizeof(T),
                arena_.available(),
                max_size_
            );
        }
    }

    /**
     * @brief Reset all temporary allocations (call after fire_all_rules)
     */
    void reset_temporaries() {
        arena_.reset();
        warning_fired_ = false;  // Reset warning flag for next cycle
    }

    // Configuration
    void set_max_size(size_t max_size) {
        // Note: This doesn't resize the underlying pool, just sets the limit for new pools
        max_size_ = max_size;
    }

    size_t get_max_size() const { return max_size_; }

    void set_warning_threshold(int percent) {
        warning_threshold_percent_ = std::max(0, std::min(100, percent));
    }

    void set_memory_pressure_callback(MemoryPressureCallback callback) {
        pressure_callback_ = std::move(callback);
    }

    // Statistics
    size_t memory_used() const { return arena_.used(); }
    size_t memory_available() const { return arena_.available(); }
    size_t memory_peak() const { return arena_.peak(); }

    int usage_percent() const {
        if (max_size_ == 0) return 0;
        return static_cast<int>((arena_.used() * 100) / max_size_);
    }

    std::string format_stats() const {
        std::ostringstream oss;
        oss << "SessionArena: "
            << "used=" << arena_.used() / 1024 << "KB ("
            << usage_percent() << "%), "
            << "peak=" << arena_.peak() / 1024 << "KB, "
            << "max=" << max_size_ / 1024 << "KB";
        return oss.str();
    }

private:
    void check_memory_pressure() {
        if (!pressure_callback_ || warning_fired_) return;

        int percent = usage_percent();
        if (percent >= warning_threshold_percent_) {
            warning_fired_ = true;
            pressure_callback_(arena_.used(), max_size_, percent);
        }
    }

    MemoryArena arena_;
    size_t max_size_;
    int warning_threshold_percent_;
    bool warning_fired_;
    MemoryPressureCallback pressure_callback_;
};

/**
 * @brief RAII scope guard for temporary allocations
 */
class ArenaScope {
public:
    explicit ArenaScope(SessionArena& arena)
        : arena_(arena.get_arena()), mark_(arena_.mark()) {}

    ~ArenaScope() {
        arena_.rewind(mark_);
    }

    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;

private:
    MemoryArena& arena_;
    size_t mark_;
};

#endif // SESSION_ARENA_HPP


