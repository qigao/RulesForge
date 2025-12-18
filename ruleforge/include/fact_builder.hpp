#ifndef FACT_BUILDER_HPP
#define FACT_BUILDER_HPP

#include "rfl_rete_defs.hpp"

#include <memory>
#include <string>
#include <type_traits>

// Forward declarations
template<typename BuilderType>
class FactBuilderBase;

class FactBuilder;

// Type-safe field setting with compile-time validation
template<typename BuilderType>
class FactBuilderBase {
public:
    // String field
    BuilderType& set(std::string const& field_name, std::string const& value) {
        static_cast<BuilderType*>(this)->fact_->fields[field_name] = value;
        return static_cast<BuilderType&>(*this);
    }
    
    // String field (C-string overload)
    BuilderType& set(std::string const& field_name, char const* value) {
        static_cast<BuilderType*>(this)->fact_->fields[field_name] = std::string(value);
        return static_cast<BuilderType&>(*this);
    }
    
    // Integer field
    template<typename T>
    BuilderType& set(std::string const& field_name, T value) {
        static_assert(std::is_integral_v<T>, "Integer types only");
        static_cast<BuilderType*>(this)->fact_->fields[field_name] = static_cast<int64_t>(value);
        return static_cast<BuilderType&>(*this);
    }
    
    // Double field
    BuilderType& set(std::string const& field_name, double value) {
        static_cast<BuilderType*>(this)->fact_->fields[field_name] = value;
        return static_cast<BuilderType&>(*this);
    }
    
    // Float field (converted to double)
    BuilderType& set(std::string const& field_name, float value) {
        static_cast<BuilderType*>(this)->fact_->fields[field_name] = static_cast<double>(value);
        return static_cast<BuilderType&>(*this);
    }
    
    // Boolean field (converted to int64_t for compatibility)
    BuilderType& set(std::string const& field_name, bool value) {
        static_cast<BuilderType*>(this)->fact_->fields[field_name] = static_cast<int64_t>(value ? 1 : 0);
        return static_cast<BuilderType&>(*this);
    }
    
    // FactList field
    BuilderType& set(std::string const& field_name, FactList const& fact_list) {
        static_cast<BuilderType*>(this)->fact_->fields[field_name] = fact_list;
        return static_cast<BuilderType&>(*this);
    }
    
    // Nested Fact field (wrapped in FactList)
    BuilderType& set(std::string const& field_name, std::shared_ptr<Fact> nested_fact) {
        FactList fact_list;
        fact_list.facts.push_back(nested_fact);
        static_cast<BuilderType*>(this)->fact_->fields[field_name] = fact_list;
        return static_cast<BuilderType&>(*this);
    }
    
    // Nil/null field
    BuilderType& set_nil(std::string const& field_name) {
        static_cast<BuilderType*>(this)->fact_->fields[field_name] = NilValue{};
        return static_cast<BuilderType&>(*this);
    }
    
    // Remove field
    BuilderType& remove(std::string const& field_name) {
        static_cast<BuilderType*>(this)->fact_->fields.erase(field_name);
        return static_cast<BuilderType&>(*this);
    }
    
    // Set explicit ID (usually auto-generated)
    BuilderType& with_id(int64_t id) {
        static_cast<BuilderType*>(this)->fact_->id = id;
        return static_cast<BuilderType&>(*this);
    }
    
    // Get current field value (for conditional building)
    std::optional<ConstraintValue> get(std::string const& field_name) const {
        return static_cast<BuilderType const*>(this)->fact_->get_field(field_name);
    }
    
    // Check if field exists
    bool has(std::string const& field_name) const {
        return static_cast<BuilderType const*>(this)->fact_->fields.count(field_name) > 0;
    }

protected:
    FactBuilderBase() = default;
    ~FactBuilderBase() = default;
};

// Main Fact builder class
class FactBuilder : public FactBuilderBase<FactBuilder> {
public:
    // Static factory method
    static FactBuilder create(std::string const& type_name) {
        return FactBuilder(type_name);
    }
    
    // Build and return the completed Fact
    std::shared_ptr<Fact> build() && {
        return std::move(fact_);
    }
    
    // Build and return a copy (for reusable builders)
    std::shared_ptr<Fact> build() const& {
        auto copy = std::make_shared<Fact>(*fact_);
        return copy;
    }
    
    // Get reference to underlying Fact (for advanced use)
    Fact& fact() { return *fact_; }
    Fact const& fact() const { return *fact_; }

private:
    friend class FactBuilderBase<FactBuilder>;
    
    explicit FactBuilder(std::string const& type_name) 
        : fact_(std::make_shared<Fact>()) {
        fact_->type = type_name;
    }
    
    std::shared_ptr<Fact> fact_;
};

// Specialized builder for known types (can be extended with type-specific methods)
template<typename FactType>
class TypedFactBuilder : public FactBuilderBase<TypedFactBuilder<FactType>> {
public:
    static TypedFactBuilder create(std::string const& type_name) {
        return TypedFactBuilder(type_name);
    }
    
    std::shared_ptr<Fact> build() && {
        return std::move(this->fact_);
    }
    
    std::shared_ptr<Fact> build() const& {
        auto copy = std::make_shared<Fact>(*this->fact_);
        return copy;
    }

private:
    friend class FactBuilderBase<TypedFactBuilder<FactType>>;
    
    explicit TypedFactBuilder(std::string const& type_name) 
        : fact_(std::make_shared<Fact>()) {
        fact_->type = type_name;
    }
    
    std::shared_ptr<Fact> fact_;
};

// Convenience macros for common patterns
#define FACT(type_name) FactBuilder::create(type_name)
#define TYPED_FACT(fact_type, type_name) TypedFactBuilder<fact_type>::create(type_name)

// Utility functions for batch building
namespace FactBuilders {
    // Create multiple facts of the same type
    template<typename BuilderFunc>
    std::vector<std::shared_ptr<Fact>> create_multiple(
        std::string const& type_name, 
        size_t count, 
        BuilderFunc builder_func) {
        
        std::vector<std::shared_ptr<Fact>> facts;
        facts.reserve(count);
        
        for (size_t i = 0; i < count; ++i) {
            auto builder = FactBuilder::create(type_name);
            builder_func(builder, i);
            facts.push_back(std::move(builder).build());
        }
        
        return facts;
    }
    
    // Transform existing facts
    template<typename TransformFunc>
    std::vector<std::shared_ptr<Fact>> transform(
        std::vector<std::shared_ptr<Fact>> const& source_facts,
        std::string const& target_type,
        TransformFunc transform_func) {
        
        std::vector<std::shared_ptr<Fact>> result;
        result.reserve(source_facts.size());
        
        for (auto const& source : source_facts) {
            auto builder = FactBuilder::create(target_type);
            transform_func(builder, *source);
            result.push_back(std::move(builder).build());
        }
        
        return result;
    }
}

#endif // FACT_BUILDER_HPP

