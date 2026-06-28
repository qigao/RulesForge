#ifndef FACT_ARENA_HPP
#define FACT_ARENA_HPP

#include "core/fact.hpp"
#include <new>
#include <memory>
#include <vector>

namespace rulesforge {

// Arena allocator for Facts.
// Allocated facts persist until the arena is reset or destroyed.
// This is typically tied to the lifecycle of a Session.
class FactArena {
public:
    explicit FactArena(size_t size = 64 * 1024 * 1024) : reserved_size_(size) { // 64MB default
        extra_facts_.reserve(4);
        owned_facts_.reserve(16);
    }

    ~FactArena() = default;

    template<typename... Args>
    Fact* create_fact(Args&&... args) {
        auto owned = std::make_unique<Fact>(std::forward<Args>(args)...);
        Fact* fact = owned.get();
        owned_facts_.push_back(std::move(owned));
        bytes_used_ += sizeof(Fact);
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
        owned_facts_.clear();
        first_fact_ = nullptr;
        extra_facts_.clear();
        bytes_used_ = 0;
    }

    Fact* get_first_fact() const { return first_fact_; }
    std::vector<Fact*> const& get_extra_facts() const { return extra_facts_; }

    size_t memory_usage() const {
        return bytes_used_;
    }

private:
    size_t reserved_size_ = 0;
    size_t bytes_used_ = 0;
    std::vector<std::unique_ptr<Fact>> owned_facts_;
    Fact* first_fact_ = nullptr;
    std::vector<Fact*> extra_facts_;
};

} // namespace rulesforge

#endif // FACT_ARENA_HPP
