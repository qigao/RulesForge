#include "catch2/catch_all.hpp"
#include "fact_builder.hpp"
#include "typed_fact_builders.hpp"
#include "drools_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

TEST_CASE("Basic Fact Builder", "[fact_builder]") {
    SECTION("String fields") {
        auto fact = FACT("Person")
            .set("name", "John Doe")
            .set("status", "Active")
            .build();
        
        REQUIRE(fact->type == "Person");
        REQUIRE(fact->fields.count("name") == 1);
        REQUIRE(std::get<std::string>(fact->fields.at("name")) == "John Doe");
        REQUIRE(std::get<std::string>(fact->fields.at("status")) == "Active");
    }
    
    SECTION("Numeric fields") {
        auto fact = FACT("Person")
            .set("age", 30)
            .set("salary", 75000.50)
            .set("active", true)
            .build();
        
        CHECK(std::get<int64_t>(fact->fields.at("age")) == 30);
        CHECK(std::get<double>(fact->fields.at("salary")) == 75000.50);
        CHECK(std::get<int64_t>(fact->fields.at("active")) == 1); // Boolean as int64_t
    }
    
    SECTION("Method chaining") {
        auto fact = FACT("Customer")
            .set("id", 123)
            .set("name", "Alice")
            .set("balance", 50000.0)
            .with_id(456)
            .build();
        
        CHECK(fact->id == 456);
        CHECK(fact->type == "Customer");
        CHECK(std::get<int64_t>(fact->fields.at("id")) == 123);
        CHECK(std::get<std::string>(fact->fields.at("name")) == "Alice");
        CHECK(std::get<double>(fact->fields.at("balance")) == 50000.0);
    }
    
    SECTION("Field manipulation") {
        auto builder = FACT("Test")
            .set("field1", "value1")
            .set("field2", 42);
        
        CHECK(builder.has("field1"));
        CHECK(builder.has("field2"));
        CHECK(!builder.has("nonexistent"));
        
        auto value = builder.get("field1");
        REQUIRE(value.has_value());
        CHECK(std::get<std::string>(*value) == "value1");
        
        auto fact = std::move(builder)
            .remove("field1")
            .set_nil("field3")
            .build();
        
        CHECK(fact->fields.count("field1") == 0);
        CHECK(fact->fields.count("field2") == 1);
        CHECK(fact->fields.count("field3") == 1);
        CHECK(std::holds_alternative<NilValue>(fact->fields.at("field3")));
    }
}

TEST_CASE("Typed Fact Builders", "[fact_builder][typed]") {
    SECTION("Person builder") {
        auto person = PERSON()
            .name("John Smith")
            .age(35)
            .email("john@example.com")
            .active()
            .build();
        
        CHECK(person->type == "Person");
        CHECK(std::get<std::string>(person->fields.at("name")) == "John Smith");
        CHECK(std::get<int64_t>(person->fields.at("age")) == 35);
        CHECK(std::get<std::string>(person->fields.at("status")) == "Active");
    }
    
    SECTION("Customer builder") {
        auto customer = CUSTOMER()
            .id(12345)
            .name("Big Corp")
            .balance(250000.0)
            .gold()
            .wealthy()
            .build();
        
        CHECK(customer->type == "Customer");
        CHECK(std::get<int64_t>(customer->fields.at("id")) == 12345);
        CHECK(std::get<std::string>(customer->fields.at("name")) == "Big Corp");
        CHECK(std::get<double>(customer->fields.at("balance")) == 250000.0);
        CHECK(std::get<std::string>(customer->fields.at("tier")) == "Gold");
    }
    
    SECTION("Order builder") {
        auto order = ORDER()
            .id(789)
            .customer_id(12345)
            .item("Premium Widget")
            .quantity(5)
            .amount(1500.0)
            .pending()
            .build();
        
        CHECK(order->type == "Order");
        CHECK(std::get<int64_t>(order->fields.at("id")) == 789);
        CHECK(std::get<int64_t>(order->fields.at("customerId")) == 12345);
        CHECK(std::get<std::string>(order->fields.at("item")) == "Premium Widget");
        CHECK(std::get<int64_t>(order->fields.at("quantity")) == 5);
        CHECK(std::get<double>(order->fields.at("amount")) == 1500.0);
        CHECK(std::get<std::string>(order->fields.at("status")) == "Pending");
    }
    
    SECTION("Domain-specific convenience methods") {
        auto senior = PERSON()
            .name("Elder")
            .senior()  // Should set age to 65+
            .build();
        
        CHECK(std::get<int64_t>(senior->fields.at("age")) >= 65);
        
        auto high_value_order = ORDER()
            .item("Luxury Item")
            .high_value()  // Should set amount >= 1000
            .build();
        
        CHECK(std::get<double>(high_value_order->fields.at("amount")) >= 1000.0);
    }
}

TEST_CASE("Fact Builder Integration", "[fact_builder][integration]") {
    std::string drl = R"(
        declare Person
            name: String
            age: int
            status: String
        end
        declare Adult
            name: String
        end
        rule "Classify Adults"
        when
            $p : Person(age >= 18, status == "Active")
        then
            drools.insert({type: "Adult", name: $p.name});
        end
    )";
    
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    REQUIRE(result.success);
    auto session = kb->create_session();
    
    // Create facts using builders
    auto john = PERSON()
        .name("John")
        .age(25)
        .active()
        .build();
    
    auto jane = PERSON()
        .name("Jane")
        .age(16)
        .active()
        .build();
    
    auto bob = PERSON()
        .name("Bob")
        .age(30)
        .inactive()
        .build();
    
    // Add facts and fire rules
    session->add_fact(john);
    session->add_fact(jane);
    session->add_fact(bob);
    
    int fired = session->fire_all_rules();
    
    CHECK(fired == 1); // Only John should trigger the Adult rule
    CHECK(session->get_fact_count() == 4); // 3 persons + 1 adult
}

TEST_CASE("Batch Fact Building", "[fact_builder][batch]") {
    SECTION("Multiple similar facts") {
        auto customers = FactBuilders::create_multiple("Customer", 5, 
            [](auto& builder, size_t index) {
                builder.set("id", static_cast<int64_t>(index + 1))
                       .set("name", "Customer" + std::to_string(index + 1))
                       .set("balance", 1000.0 * (index + 1))
                       .set("status", "Active");
            });
        
        REQUIRE(customers.size() == 5);
        
        for (size_t i = 0; i < customers.size(); ++i) {
            auto const& customer = customers[i];
            CHECK(customer->type == "Customer");
            CHECK(std::get<int64_t>(customer->fields.at("id")) == static_cast<int64_t>(i + 1));
            CHECK(std::get<double>(customer->fields.at("balance")) == 1000.0 * (i + 1));
        }
    }
    
    SECTION("Transform existing facts") {
        // Create source facts
        std::vector<std::shared_ptr<Fact>> people;
        people.push_back(PERSON().name("Alice").age(30).build());
        people.push_back(PERSON().name("Bob").age(25).build());
        
        // Transform to customers
        auto customers = FactBuilders::transform(people, "Customer", 
            [](auto& builder, Fact const& person) {
                auto name = std::get<std::string>(person.fields.at("name"));
                auto age = std::get<int64_t>(person.fields.at("age"));
                
                builder.set("name", name)
                       .set("age", age)
                       .set("status", "New")
                       .set("balance", 0.0);
            });
        
        REQUIRE(customers.size() == 2);
        CHECK(customers[0]->type == "Customer");
        CHECK(std::get<std::string>(customers[0]->fields.at("name")) == "Alice");
        CHECK(std::get<std::string>(customers[0]->fields.at("status")) == "New");
    }
}

TEST_CASE("Builder Reusability", "[fact_builder]") {
    // Test that builders can be reused (const& build)
    auto template_builder = CUSTOMER()
        .status("Active")
        .tier("Silver");
    
    auto customer1 = template_builder
        .set("id", 1)
        .set("name", "Customer 1")
        .build(); // const& version - builder remains usable
    
    auto customer2 = template_builder
        .set("id", 2)
        .set("name", "Customer 2")
        .set("balance", 5000.0)
        .build();
    
    // Both should have the template properties
    CHECK(std::get<std::string>(customer1->fields.at("status")) == "Active");
    CHECK(std::get<std::string>(customer1->fields.at("tier")) == "Silver");
    CHECK(std::get<std::string>(customer2->fields.at("status")) == "Active");
    CHECK(std::get<std::string>(customer2->fields.at("tier")) == "Silver");
    
    // But different specific properties
    CHECK(std::get<int64_t>(customer1->fields.at("id")) == 1);
    CHECK(std::get<int64_t>(customer2->fields.at("id")) == 2);
    CHECK(customer2->fields.count("balance") == 1);
    CHECK(customer1->fields.count("balance") == 0);
}