#ifndef MEMORY_OPTIMIZED_TYPES_HPP
#define MEMORY_OPTIMIZED_TYPES_HPP

#include "rfl_rete_defs.hpp"

#include <string_view>
#include <memory>
#include <phmap.h>
#include <optional>
#include <sstream>
#include <unordered_set>

// String interning for constant strings (rule names, type names, etc.)
class StringInterner {
public:
    static StringInterner& instance() {
        static StringInterner instance;
        return instance;
    }
    
    std::string_view intern(std::string const& str) {
        auto it = interned_strings_.find(str);
        if (it != interned_strings_.end()) {
            return std::string_view(*it);
        }
        
        auto [inserted_it, success] = interned_strings_.insert(str);
        return std::string_view(*inserted_it);
    }
    
    std::string_view intern(std::string_view sv) {
        return intern(std::string(sv));
    }
    
    // For rule names, type names, field names that are known at parse time
    std::string_view intern_persistent(std::string const& str) {
        return intern(str);
    }
    
    void clear() {
        interned_strings_.clear();
    }
    
    size_t size() const {
        return interned_strings_.size();
    }

private:
    // MUST use std::unordered_set (node-based) for pointer stability.
    // phmap::flat_hash_set moves elements on rehash, invalidating string_views.
    std::unordered_set<std::string> interned_strings_;
};

// Fast string lookup using string_view keys
template<typename Value>
class StringViewMap {
public:
    using key_type = std::string_view;
    using mapped_type = Value;
    using value_type = std::pair<const std::string_view, Value>;
    
private:
    struct StringViewHash {
        std::size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
    };
    
    ruleforge::unordered_map<std::string_view, Value, StringViewHash> map_;
    
public:
    auto begin() const { return map_.begin(); }
    auto end() const { return map_.end(); }
    auto find(std::string_view key) const { return map_.find(key); }
    auto count(std::string_view key) const { return map_.count(key); }
    auto size() const { return map_.size(); }
    auto empty() const { return map_.empty(); }
    
    Value& operator[](std::string_view key) { return map_[key]; }
    const Value& at(std::string_view key) const { return map_.at(key); }
    
    auto insert(const value_type& value) { return map_.insert(value); }
    auto emplace(std::string_view key, Value&& value) { 
        return map_.emplace(key, std::forward<Value>(value)); 
    }
    
    void erase(std::string_view key) { map_.erase(key); }
    void clear() { map_.clear(); }
};

// Memory-optimized Fact with string_view keys and object pool allocation
struct FastFact {
    int64_t id = 0;
    std::string_view type;  // Interned string
    StringViewMap<ConstraintValue> fields;
    
    // Constructor with interned type name
    explicit FastFact(std::string_view type_name) 
        : type(StringInterner::instance().intern_persistent(std::string(type_name))) {}
    
    std::optional<ConstraintValue> get_field(std::string_view name) const {
        auto it = fields.find(name);
        return (it != fields.end()) ? std::make_optional(it->second) : std::nullopt;
    }
    
    // Convenience method to set field with interned key
    void set_field(std::string_view key, ConstraintValue value) {
        auto interned_key = StringInterner::instance().intern_persistent(std::string(key));
        fields[interned_key] = std::move(value);
    }
    
    // Convert to legacy Fact (for compatibility)
    std::shared_ptr<Fact> to_fact() const {
        auto fact = std::make_shared<Fact>();
        fact->id = id;
        fact->type = std::string(type);
        
        for (auto const& [key, value] : fields) {
            fact->fields[std::string(key)] = value;
        }
        
        return fact;
    }
    
    // Create from legacy Fact
    static std::unique_ptr<FastFact> from_fact(Fact const& fact) {
        auto fast_fact = std::make_unique<FastFact>(fact.type);
        fast_fact->id = fact.id;
        
        for (auto const& [key, value] : fact.fields) {
            fast_fact->set_field(key, value);
        }
        
        return fast_fact;
    }
};

#endif // MEMORY_OPTIMIZED_TYPES_HPP

