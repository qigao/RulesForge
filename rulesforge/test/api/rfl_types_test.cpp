#include "rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "test_helpers.hpp"
#include "tinytest.h"

// Step 1: Manually define the C++ struct.
struct PersonForTest {
    std::string name;
    int64_t age;
};

// Step 2: Manually define the registration function.
static void register_test_types(FactTypeRegistry& registry) {
    registry.register_type<PersonForTest>("Person", [](PersonForTest const& p, Fact& fact) {
        fact.fields["name"] = p.name;
        fact.fields["age"] = p.age;
    });
}

// --- The Unit Test ---
suite("Engine API") {
    group("Fluent C++ API (Manual Registration)") {
        it("populates typed facts and executes matching rule") {
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
            check(result.success);
            check(kb != nullptr);

            // 2. Call our registration function to connect the C++ struct to the RFL type.
            register_test_types(kb->get_fact_type_registry());

            // 3. Create a session from the knowledge base.
            auto session = kb->create_session();
            check(session != nullptr);

            // 4. Use the new, fluent API on the session.
            session->add_fact_typed(PersonForTest{"John", 30});
            session->add_fact_typed(PersonForTest{"Amy", 15});

            // 5. Assert that the facts were created correctly.
            check(session->get_fact_count() == 2);

            // 6. Fire the rules and assert that the engine logic works with the new facts.
            int fired_count = session->fire_all_rules();
            check(fired_count == 1);
        }

        it("fails fast when type is not registered") {
            char const* drl = R"(
                declare Person
                    name : String
                    age : int
                end
            )";

            ParsingResult result;
            auto kb = build_knowledge_base(drl, result);
            check(result.success);

            auto session = kb->create_session();
            check(session != nullptr);

            bool caught_exception = false;
            try {
                // This type is not registered in the registry, should throw std::runtime_error
                session->add_fact_typed(PersonForTest{"Stranger", 40});
            } catch (std::runtime_error const& e) {
                caught_exception = true;
                // Verify the error message contains the type name or error description
                check(std::string(e.what()).find("PersonForTest") != std::string::npos);
            }
            check(caught_exception);
        }
    }
}
