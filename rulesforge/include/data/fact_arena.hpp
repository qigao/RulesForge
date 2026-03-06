#ifndef FACT_ARENA_HPP
#define FACT_ARENA_HPP

#include <turbo_buffer.h>
#include "core/fact.hpp"
#include <new>
#include <vector>

namespace rulesforge {

// Arena allocator for Facts.
// Allocated facts persist until the arena is reset or destroyed.
// This is typically tied to the lifecycle of a Session.
class FactArena {
public:
    explicit FactArena(size_t size = 64 * 1024 * 1024) { // 64MB default
        turbo_pool_init(&arena_, size);
    }

    ~FactArena() {
        // We do NOT call Fact destructors.
        // Fact contains InternedKeyMap which uses string_view (trivial destructor relative to data)
        // BUT it might contain other things.
        // If Fact has non-trivial destructor that manages EXTERNAL resources, this is a leak.
        // RulesForge Facts usually contain string_views, numbers, or shared_ptrs to other facts (FactList).
        // If Fact contains shared_ptr, we MUST call destructor or we leak the control block and the object it points to.

        // Current Fact definition:
        // struct Fact {
        //     int64_t id;
        //     std::string type; // std::string has destructor!
        //     InternedKeyMap<ConstraintValue> fields; // destructors needed for values
        // };

        // Since we use turbo_pool which is linear and doesn't support individual delete,
        // we MUST iterate and destroy if we want to be clean, OR we rely on the fact that
        // pure arena usage implies we use arena-aware types (e.g. ArenaString).
        // BUT current Fact uses std::string.
        // So we MUST call destructors.

        // This linear arena doesn't track objects.
        // Optimally, we should change Fact to use Arena-allocated strings/maps.
        // For now, to solve "lot of shared_ptr", we might just accept that we need to track facts to destroy them
        // or change Fact members to be trivially destructible (or arena managed).

        // However, standard arena pattern in C++ with non-trivial types:
        // maintain a list of destructors to call, or use a "frame" system.

        // For this task, strict performance is key.
        // If we just free the arena, std::string buf pointers leak.

        // To properly support this without leaks, we need a list of allocated facts.
        for (auto* fact : facts_) {
            fact->~Fact();
        }
        turbo_pool_free(&arena_);
    }

    template<typename... Args>
    Fact* create_fact(Args&&... args) {
        void* p = turbo_pool_alloc(&arena_, sizeof(Fact));
        if (!p) throw std::bad_alloc();
        Fact* fact = new (p) Fact(std::forward<Args>(args)...);
        facts_.push_back(fact);
        return fact;
    }

    // Create a Fact from an existing one (copy)
    Fact* clone_fact(Fact const& other) {
        return create_fact(other);
    }

    void reset() {
        for (auto* fact : facts_) {
            fact->~Fact();
        }
        facts_.clear();
        turbo_pool_reset(&arena_);
    }

    size_t memory_usage() const {
        return arena_.total_used;
    }

private:
    turbo_pool_t arena_;
    std::vector<Fact*> facts_; // To track for destruction
};

} // namespace rulesforge

#endif // FACT_ARENA_HPP
