#ifndef TYPED_FACT_BUILDERS_HPP
#define TYPED_FACT_BUILDERS_HPP

#include "data/fact_builder.hpp"

#include <string>

// Common business entity builders
namespace TypedBuilders {

// Person builder with domain-specific methods
class PersonBuilder : public FactBuilderBase<PersonBuilder> {
public:
    static PersonBuilder create() {
        return PersonBuilder();
    }

    static PersonBuilder create(rulesforge::FactArena& arena) {
        return PersonBuilder(arena);
    }

    PersonBuilder& name(std::string const& name) {
        return set("name", name);
    }

    PersonBuilder& age(int age) {
        return set("age", age);
    }

    PersonBuilder& email(std::string const& email) {
        return set("email", email);
    }

    PersonBuilder& status(std::string const& status) {
        return set("status", status);
    }

    // Domain-specific convenience methods
    PersonBuilder& active() {
        return status("Active");
    }

    PersonBuilder& inactive() {
        return status("Inactive");
    }

    PersonBuilder& adult() {
        if (!has("age") || std::get<int64_t>(fact_->fields.at("age")) < 18) {
            age(18);
        }
        return *this;
    }

    PersonBuilder& senior() {
        if (!has("age") || std::get<int64_t>(fact_->fields.at("age")) < 65) {
            age(65);
        }
        return *this;
    }

    Fact* build() && {
        return fact_;
    }

private:
    friend class FactBuilderBase<PersonBuilder>;

    PersonBuilder() {
        fact_ = new Fact();
        fact_->type = "Person";
    }

    explicit PersonBuilder(rulesforge::FactArena& arena) {
        fact_ = arena.create_fact();
        fact_->type = "Person";
    }

    Fact* fact_;
};

// Customer builder
class CustomerBuilder : public FactBuilderBase<CustomerBuilder> {
public:
    static CustomerBuilder create() {
        return CustomerBuilder();
    }

    static CustomerBuilder create(rulesforge::FactArena& arena) {
        return CustomerBuilder(arena);
    }

    CustomerBuilder& id(int64_t customer_id) {
        return set("id", customer_id);
    }

    CustomerBuilder& name(std::string const& name) {
        return set("name", name);
    }

    CustomerBuilder& balance(double amount) {
        return set("balance", amount);
    }

    CustomerBuilder& status(std::string const& status) {
        return set("status", status);
    }

    CustomerBuilder& tier(std::string const& tier) {
        return set("tier", tier);
    }

    // Convenience methods
    CustomerBuilder& active() { return status("Active"); }
    CustomerBuilder& inactive() { return status("Inactive"); }
    CustomerBuilder& bronze() { return tier("Bronze"); }
    CustomerBuilder& silver() { return tier("Silver"); }
    CustomerBuilder& gold() { return tier("Gold"); }
    CustomerBuilder& platinum() { return tier("Platinum"); }

    CustomerBuilder& wealthy(double min_balance = 100000.0) {
        if (!has("balance") || std::get<double>(fact_->fields.at("balance")) < min_balance) {
            balance(min_balance);
        }
        return *this;
    }

    Fact* build() && {
        return fact_;
    }

private:
    friend class FactBuilderBase<CustomerBuilder>;

    CustomerBuilder() {
        fact_ = new Fact();
        fact_->type = "Customer";
    }

    explicit CustomerBuilder(rulesforge::FactArena& arena) {
        fact_ = arena.create_fact();
        fact_->type = "Customer";
    }

    Fact* fact_;
};

// Order builder
class OrderBuilder : public FactBuilderBase<OrderBuilder> {
public:
    static OrderBuilder create() {
        return OrderBuilder();
    }

    static OrderBuilder create(rulesforge::FactArena& arena) {
        return OrderBuilder(arena);
    }

    OrderBuilder& id(int64_t order_id) {
        return set("id", order_id);
    }

    OrderBuilder& customer_id(int64_t customer_id) {
        return set("customerId", customer_id);
    }

    OrderBuilder& amount(double amount) {
        return set("amount", amount);
    }

    OrderBuilder& item(std::string const& item) {
        return set("item", item);
    }

    OrderBuilder& quantity(int qty) {
        return set("quantity", qty);
    }

    OrderBuilder& status(std::string const& status) {
        return set("status", status);
    }

    // Convenience methods
    OrderBuilder& pending() { return status("Pending"); }
    OrderBuilder& shipped() { return status("Shipped"); }
    OrderBuilder& delivered() { return status("Delivered"); }
    OrderBuilder& cancelled() { return status("Cancelled"); }

    OrderBuilder& high_value(double threshold = 1000.0) {
        if (!has("amount") || std::get<double>(fact_->fields.at("amount")) < threshold) {
            amount(threshold);
        }
        return *this;
    }

    Fact* build() && {
        return fact_;
    }

private:
    friend class FactBuilderBase<OrderBuilder>;

    OrderBuilder() {
        fact_ = new Fact();
        fact_->type = "Order";
    }

    explicit OrderBuilder(rulesforge::FactArena& arena) {
        fact_ = arena.create_fact();
        fact_->type = "Order";
    }

    Fact* fact_;
};

} // namespace TypedBuilders

// Convenience macros for typed builders
#define PERSON() TypedBuilders::PersonBuilder::create()
#define CUSTOMER() TypedBuilders::CustomerBuilder::create()
#define ORDER() TypedBuilders::OrderBuilder::create()

#endif // TYPED_FACT_BUILDERS_HPP
