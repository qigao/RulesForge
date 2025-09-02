#ifndef OPTIMIZED_FACT_STORAGE_HPP
#define OPTIMIZED_FACT_STORAGE_HPP

#include "drools_rete_defs.hpp"
#include "phmap/phmap.h"
#include <string_view>
#include <xxhash.h>  // For fast string hashing

/**
 * Optimized fact storage using phmap and string interning.
 * 
 * Linus principles applied:
 * 1. Cache-friendly phmap instead of tree-based std::map
 * 2. String interning to avoid repeated string allocations
 * 3. Pre-computed hashes for string lookups
 */
class OptimizedFactStorage {
public:
    using FactId = int64_t;
    using FactPtr = std::shared_ptr<Fact>;
    
    // String interning for field names
    class StringInterner {
    public:
        using InternId = uint32_t;
        
        InternId intern(std::string_view str) {
            uint64_t hash = XXH64(str.data(), str.size(), 0);
            auto it = hash_to_id_.find(hash);
            if (it != hash_to_id_.end()) {
                return it->second;
            }
            
            InternId id = next_id_++;
            strings_.emplace_back(str);
            hash_to_id_[hash] = id;
            return id;
        }
        
        std::string_view get(InternId id) const {
            return id < strings_.size() ? strings_[id] : "";
        }
        
    private:
        std::vector<std::string> strings_;
        phmap::flat_hash_map<uint64_t, InternId> hash_to_id_;
        InternId next_id_ = 0;
    };
    
    // Optimized fact with interned field names
    struct OptimizedFact {
        FactId id;
        std::string type;  // Type names are typically few, could also intern
        phmap::flat_hash_map<StringInterner::InternId, ConstraintValue> fields;
        
        std::optional<ConstraintValue> get_field(StringInterner& interner, std::string_view name) const {
            auto id = interner.intern(name);
            auto it = fields.find(id);
            return it != fields.end() ? std::optional(it->second) : std::nullopt;
        }
    };
    
    // Main storage using phmap for better cache locality and performance
    using Storage = phmap::flat_hash_map<FactId, OptimizedFact>;
    
    OptimizedFactStorage() = default;
    
    void insert(FactPtr const& fact) {
        OptimizedFact opt_fact;
        opt_fact.id = fact->id;
        opt_fact.type = fact->type;
        
        // Convert fields to use interned IDs
        for (auto const& [name, value] : fact->fields) {
            auto id = interner_.intern(name);
            opt_fact.fields[id] = value;
        }
        
        facts_.insert_or_assign(fact->id, std::move(opt_fact));
    }
    
    void remove(FactId id) {
        facts_.erase(id);
    }
    
    OptimizedFact* get(FactId id) {
        auto it = facts_.find(id);
        return it != facts_.end() ? &it->second : nullptr;
    }
    
    size_t size() const {
        return facts_.size();
    }
    
    // Batch operations for better performance
    template<typename Iterator>
    void insert_batch(Iterator begin, Iterator end) {
        facts_.reserve(facts_.size() + std::distance(begin, end));
        for (auto it = begin; it != end; ++it) {
            insert(*it);
        }
    }
    
    Storage::iterator begin() { return facts_.begin(); }
    Storage::iterator end() { return facts_.end(); }
    Storage::const_iterator begin() const { return facts_.begin(); }
    Storage::const_iterator end() const { return facts_.end(); }
    
private:
    Storage facts_;
    StringInterner interner_;
};

#endif // OPTIMIZED_FACT_STORAGE_HPP