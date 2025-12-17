#include "catch2/catch_test_macros.hpp"
#include "drools_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

// --- Test Setup ---

// Step 1: Manually define the C++ struct.
struct PersonForTest {
    std::string name;
    int64_t age;
};

// Step 2: Manually define the registration function.
void register_test_types(FactTypeRegistry& registry) {
    registry.register_type<PersonForTest>("Person", [](PersonForTest const& p, Fact& fact) {
        fact.fields["name"] = p.name;
        fact.fields["age"] = p.age;
    });
}

// --- The Unit Test ---

TEST_CASE("Engine: Fluent C++ API (Manual Registration)", "[engine][api]") {
    char const* drl = R"(
        declare Person
            name : String
            age : int
        end
        rule "Find Adults for API Test"
        when
            $p : Person(age >= 18)
        then
            -- No action needed, just need the rule to fire.
        end
    )";

    // 1. Parse the DRL to build the KnowledgeBase.
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    REQUIRE(result.success);
    REQUIRE(kb != nullptr);

    // 2. Call our registration function to connect the C++ struct to the DRL type.
    register_test_types(kb->get_fact_type_registry());

    // 3. Create a session from the knowledge base.
    auto session = kb->create_session();
    REQUIRE(session != nullptr);

    // 4. Use the new, fluent API on the session.
    session->add_fact_typed(PersonForTest{"John", 30});
    session->add_fact_typed(PersonForTest{"Amy", 15});

    // 5. Assert that the facts were created correctly.
    CHECK(session->get_fact_count() == 2);

    // 6. Fire the rules and assert that the engine logic works with the new facts.
    int fired_count = session->fire_all_rules();
    CHECK(fired_count == 1);
}

