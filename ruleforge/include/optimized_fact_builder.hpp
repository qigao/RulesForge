#ifndef OPTIMIZED_FACT_BUILDER_HPP
#define OPTIMIZED_FACT_BUILDER_HPP

#include "memory_optimized_types.hpp"
#include "fact_builder.hpp"

#include <string_view>

// Memory-optimized fact builder using string_view and object pools
template<typename BuilderType>
class OptimizedFactBuilderBase {
public:
    // String field with string_view (automatically interned)
    BuilderType& set(std::string_view field_name, std::string const& value) {
        auto interned_key = StringInterner::instance().intern_persistent(std::string(field_name));
        static_cast<BuilderType*>(this)->fast_fact_->fields[interned_key] = value;
        return static_cast<BuilderType&>(*this);
    }
    
    BuilderType& set(std::string_view field_name, std::string_view value) {
        return set(field_name, std::string(value));
    }
    
    BuilderType& set(std::string_view field_name, char const* value) {
        return set(field_name, std::string(value));
    }
    
    // Integer field
    template<typename T>
    BuilderType& set(std::string_view field_name, T value) {
        static_assert(std::is_integral_v<T>, "Integer types only");
        auto interned_key = StringInterner::instance().intern_persistent(std::string(field_name));
        static_cast<BuilderType*>(this)->fast_fact_->fields[interned_key] = static_cast<int64_t>(value);
        return static_cast<BuilderType&>(*this);
    }
    
    // Double field
    BuilderType& set(std::string_view field_name, double value) {
        auto interned_key = StringInterner::instance().intern_persistent(std::string(field_name));
        static_cast<BuilderType*>(this)->fast_fact_->fields[interned_key] = value;
        return static_cast<BuilderType&>(*this);
    }
    
    // Float field (converted to double)
    BuilderType& set(std::string_view field_name, float value) {
        return set(field_name, static_cast<double>(value));
    }
    
    // Boolean field
    BuilderType& set(std::string_view field_name, bool value) {
        return set(field_name, static_cast<int64_t>(value ? 1 : 0));
    }
    
    // Set explicit ID
    BuilderType& with_id(int64_t id) {
        static_cast<BuilderType*>(this)->fast_fact_->id = id;
        return static_cast<BuilderType&>(*this);
    }
    
    // Check if field exists
    bool has(std::string_view field_name) const {
        return static_cast<BuilderType const*>(this)->fast_fact_->fields.count(field_name) > 0;
    }
    
    // Get field value
    std::optional<ConstraintValue> get(std::string_view field_name) const {
        return static_cast<BuilderType const*>(this)->fast_fact_->get_field(field_name);
    }

protected:
    OptimizedFactBuilderBase() = default;
    ~OptimizedFactBuilderBase() = default;
};

// Main optimized fact builder
class OptimizedFactBuilder : public OptimizedFactBuilderBase<OptimizedFactBuilder> {
public:
    static OptimizedFactBuilder create(std::string_view type_name) {
        return OptimizedFactBuilder(type_name);
    }
    
    // Build and return legacy Fact (for compatibility)
    std::shared_ptr<Fact> build() && {
        return fast_fact_->to_fact();
    }
    
    std::shared_ptr<Fact> build() const& {
        return fast_fact_->to_fact();
    }
    
    // Build and return optimized FastFact
    std::unique_ptr<FastFact> build_fast() && {
        return std::move(fast_fact_);
    }
    
    std::unique_ptr<FastFact> build_fast() const& {
        return FastFact::from_fact(*fast_fact_->to_fact());
    }
    
    // Get reference to underlying FastFact
    FastFact& fast_fact() { return *fast_fact_; }
    FastFact const& fast_fact() const { return *fast_fact_; }

private:
    friend class OptimizedFactBuilderBase<OptimizedFactBuilder>;
    
    explicit OptimizedFactBuilder(std::string_view type_name) 
        : fast_fact_(std::make_unique<FastFact>(type_name)) {}
    
    std::unique_ptr<FastFact> fast_fact_;
};

// Convenience macro for optimized builders
#define FAST_FACT(type_name) OptimizedFactBuilder::create(type_name)

// Optimized typed builders
namespace OptimizedBuilders {

class FastPersonBuilder : public OptimizedFactBuilderBase<FastPersonBuilder> {
public:
    static FastPersonBuilder create() {
        return FastPersonBuilder();
    }
    
    FastPersonBuilder& name(std::string_view name) {
        return set("name", name);
    }
    
    FastPersonBuilder& age(int age) {
        return set("age", age);
    }
    
    FastPersonBuilder& email(std::string_view email) {
        return set("email", email);
    }
    
    FastPersonBuilder& status(std::string_view status) {
        return set("status", status);
    }
    
    FastPersonBuilder& active() { return status("Active"); }
    FastPersonBuilder& inactive() { return status("Inactive"); }
    
    std::shared_ptr<Fact> build() && {
        return fast_fact_->to_fact();
    }
    
    std::shared_ptr<Fact> build() const& {
        return fast_fact_->to_fact();
    }

private:
    friend class OptimizedFactBuilderBase<FastPersonBuilder>;
    
    FastPersonBuilder() : fast_fact_(std::make_unique<FastFact>("Person")) {}
    
    std::unique_ptr<FastFact> fast_fact_;
};

class FastCustomerBuilder : public OptimizedFactBuilderBase<FastCustomerBuilder> {
public:
    static FastCustomerBuilder create() {
        return FastCustomerBuilder();
    }
    
    FastCustomerBuilder& id(int64_t customer_id) { return set("id", customer_id); }
    FastCustomerBuilder& name(std::string_view name) { return set("name", name); }
    FastCustomerBuilder& balance(double amount) { return set("balance", amount); }
    FastCustomerBuilder& status(std::string_view status) { return set("status", status); }
    FastCustomerBuilder& tier(std::string_view tier) { return set("tier", tier); }
    
    FastCustomerBuilder& active() { return status("Active"); }
    FastCustomerBuilder& inactive() { return status("Inactive"); }
    FastCustomerBuilder& gold() { return tier("Gold"); }
    FastCustomerBuilder& silver() { return tier("Silver"); }
    FastCustomerBuilder& bronze() { return tier("Bronze"); }
    
    std::shared_ptr<Fact> build() && {
        return fast_fact_->to_fact();
    }
    
    std::shared_ptr<Fact> build() const& {
        return fast_fact_->to_fact();
    }

private:
    friend class OptimizedFactBuilderBase<FastCustomerBuilder>;
    
    FastCustomerBuilder() : fast_fact_(std::make_unique<FastFact>("Customer")) {}
    
    std::unique_ptr<FastFact> fast_fact_;
};

} // namespace OptimizedBuilders

// Convenience macros for optimized typed builders  
#define FAST_PERSON() OptimizedBuilders::FastPersonBuilder::create()
#define FAST_CUSTOMER() OptimizedBuilders::FastCustomerBuilder::create()

#endif // OPTIMIZED_FACT_BUILDER_HPP

