#ifndef FACT_BUILDER_HPP
#define FACT_BUILDER_HPP

#include "core/fact.hpp"
#include "data/fact_arena.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <optional>

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

    BuilderType& set(std::string_view field_name, std::string_view value) {
        static_cast<BuilderType*>(this)->fact_->fields[field_name] = std::string(value);
        return static_cast<BuilderType&>(*this);
    }

    // Pre-interned key optimizations
    BuilderType& set(rulesforge::InternedString const& field_name, std::string const& value) {
        static_cast<BuilderType*>(this)->fact_->fields[field_name] = value;
        return static_cast<BuilderType&>(*this);
    }

    BuilderType& set(rulesforge::InternedString const& field_name, int64_t value) {
        static_cast<BuilderType*>(this)->fact_->fields[field_name] = value;
        return static_cast<BuilderType&>(*this);
    }

    BuilderType& set(rulesforge::InternedString const& field_name, double value) {
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
        static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>, "Numeric types only");
        if constexpr (std::is_integral_v<T>) {
            static_cast<BuilderType*>(this)->fact_->fields[field_name] = static_cast<int64_t>(value);
        } else {
            static_cast<BuilderType*>(this)->fact_->fields[field_name] = static_cast<double>(value);
        }
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
    BuilderType& set(std::string const& field_name, Fact* nested_fact) {
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
    // Static factory methods
    static FactBuilder create(std::string const& type_name) {
        return FactBuilder(type_name);
    }

    static FactBuilder create(rulesforge::FactArena& arena, std::string const& type_name) {
        return FactBuilder(arena, type_name);
    }

    // Build and return the completed Fact
    Fact* build() {
        return fact_;
    }

    // Get reference to underlying Fact (for advanced use)
    Fact& fact() { return *fact_; }
    Fact const& fact() const { return *fact_; }

private:
    friend class FactBuilderBase<FactBuilder>;

    explicit FactBuilder(std::string const& type_name) {
        // Fallback for non-arena use (legacy or tests)
        // NOT RECOMMENDED in production
        fact_ = new Fact();
        fact_->type = type_name;
    }

    explicit FactBuilder(rulesforge::FactArena& arena, std::string const& type_name) {
        fact_ = arena.create_fact();
        fact_->type = type_name;
    }

    Fact* fact_;
};

// Specialized builder for known types
template<typename FactType>
class TypedFactBuilder : public FactBuilderBase<TypedFactBuilder<FactType>> {
public:
    static TypedFactBuilder create(std::string const& type_name) {
        return TypedFactBuilder(type_name);
    }

    static TypedFactBuilder create(rulesforge::FactArena& arena, std::string const& type_name) {
        return TypedFactBuilder(arena, type_name);
    }

    Fact* build() {
        return this->fact_;
    }

private:
    friend class FactBuilderBase<TypedFactBuilder<FactType>>;

    explicit TypedFactBuilder(std::string const& type_name) {
        this->fact_ = new Fact();
        this->fact_->type = type_name;
    }

    explicit TypedFactBuilder(rulesforge::FactArena& arena, std::string const& type_name) {
        this->fact_ = arena.create_fact();
        this->fact_->type = type_name;
    }

    Fact* fact_;
};

// Convenience macros
#define FACT(type_name) FactBuilder::create(type_name)
#define TYPED_FACT(fact_type, type_name) TypedFactBuilder<fact_type>::create(type_name)

#endif // FACT_BUILDER_HPP
