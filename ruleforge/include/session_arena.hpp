#ifndef SESSION_ARENA_HPP
#define SESSION_ARENA_HPP

#include "arena_memory.h"

#include <cstddef>
#include <string>
#include <sstream>

/**
 * @brief Per-session arena allocator for temporary allocations during rule firing
 *
 * The arena is used for temporary objects that live only during a single
 * fire_all_rules() call. For objects with longer lifetimes (Facts, TokenWME),
 * we still use shared_ptr with mimalloc for now.
 *
 * Future optimization: migrate TokenWME to full arena allocation.
 */
class SessionArena {
public:
    explicit SessionArena(size_t initial_size = 1024 * 1024)  // 1MB default
        : arena_(initial_size) {}

    // Get underlying arena for direct use
    cssbox::MemoryArena& get_arena() { return arena_; }

    // Allocate temporary memory (freed on reset)
    template<typename T, typename... Args>
    T* allocate(Args&&... args) {
        return arena_.allocate<T>(std::forward<Args>(args)...);
    }

    // Reset all temporary allocations (call after fire_all_rules)
    void reset_temporaries() {
        arena_.reset();
    }

    // Statistics
    size_t memory_used() const { return arena_.used(); }
    size_t memory_available() const { return arena_.available(); }
    size_t memory_peak() const { return arena_.peak(); }

    std::string format_stats() const {
        std::ostringstream oss;
        oss << "SessionArena: "
            << "used=" << arena_.used() / 1024 << "KB, "
            << "peak=" << arena_.peak() / 1024 << "KB, "
            << "available=" << arena_.available() / 1024 << "KB";
        return oss.str();
    }

private:
    cssbox::MemoryArena arena_;
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
    cssbox::MemoryArena& arena_;
    size_t mark_;
};

#endif // SESSION_ARENA_HPP
