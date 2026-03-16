#ifndef WORKING_MEMORY_HPP
#define WORKING_MEMORY_HPP

#include <cstdint>
#include <vector>

#include "core/fact.hpp"


class WorkingMemory {
public:
    Fact* insert(Fact* fact) {
        if (!fact) return nullptr;
        assign_id(*fact);
        facts_[fact->id] = fact;
        return fact;
    }

    void insert_batch(std::vector<Fact*> const& facts) {
        reserve_for_additional(facts.size());
        for (auto* fact : facts) {
            insert(fact);
        }
    }

    void remove(int64_t fact_id) {
        facts_.erase(fact_id);
    }

    Fact* get(int64_t fact_id) const {
        auto it = facts_.find(fact_id);
        return (it != facts_.end()) ? it->second : nullptr;
    }

    bool contains(int64_t fact_id) const {
        return facts_.find(fact_id) != facts_.end();
    }

    std::size_t count() const {
        return facts_.size();
    }

    void clear(bool reset_next_id = true) {
        facts_.clear();
        if (reset_next_id) {
            next_id_ = 1;
        }
    }

    void reserve_for_additional(std::size_t count) {
        facts_.reserve(facts_.size() + count);
    }

    int64_t reserve_next_id() {
        return next_id_++;
    }

    int64_t next_id() const {
        return next_id_;
    }

    void assign_nested_ids(Fact& fact) {
        assign_id(fact);
        for (auto& [key, val] : fact.fields) {
            (void)key;
            if (std::holds_alternative<FactList>(val)) {
                for (auto* nested_fact : std::get<FactList>(val).facts) {
                    if (nested_fact) {
                        assign_nested_ids(*nested_fact);
                    }
                }
            }
        }
    }

private:
    void assign_id(Fact& fact) {
        if (fact.id == 0) {
            fact.id = next_id_++;
        } else if (fact.id >= next_id_) {
            next_id_ = fact.id + 1;
        }
    }

    int64_t next_id_ = 1;
    std::unordered_map<int64_t, Fact*> facts_;
};

#endif  // WORKING_MEMORY_HPP
