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
        mem_init(&arena_, size);
        extra_facts_.reserve(4);
    }

    ~FactArena() {
        destroy_tracked_facts();
        mem_destroy(&arena_);
    }

    template<typename... Args>
    Fact* create_fact(Args&&... args) {
        void* p = mem_alloc(&arena_, sizeof(Fact));
        if (!p) throw std::bad_alloc();
        Fact* fact = new (p) Fact(std::forward<Args>(args)...);
        if (first_fact_ == nullptr) {
            first_fact_ = fact;
        } else {
            extra_facts_.push_back(fact);
        }
        return fact;
    }

    // Create a Fact from an existing one (copy)
    Fact* clone_fact(Fact const& other) {
        return create_fact(other);
    }

    void reset() {
        destroy_tracked_facts();
        mem_reset(&arena_);
    }

    Fact* get_first_fact() const { return first_fact_; }
    std::vector<Fact*> const& get_extra_facts() const { return extra_facts_; }

    size_t memory_usage() const {
        return arena_.total_used.load(std::memory_order_relaxed);
    }

private:
    void destroy_tracked_facts() {
        if (first_fact_ != nullptr) {
            first_fact_->~Fact();
            first_fact_ = nullptr;
        }
        for (auto* fact : extra_facts_) {
            fact->~Fact();
        }
        extra_facts_.clear();
    }

    mem_pool_t arena_;
    Fact* first_fact_ = nullptr;
    std::vector<Fact*> extra_facts_;
};

} // namespace rulesforge

#endif // FACT_ARENA_HPP
