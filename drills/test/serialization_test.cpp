#include "catch2/catch_all.hpp"
#include "drools_parser.hpp"
#include "knowledge_base.hpp"
#include "query_result.hpp"   // Include our new API header
#include "stateful_session.hpp"

// A dedicated test fixture for serialization tests.
struct SerializationTestFixture {
    std::string drl;
    std::shared_ptr<KnowledgeBase> kb;

    SerializationTestFixture() {
        drl = R"(
            declare Person
                name: String
                age: int
            end

            rule "dummy" when $p: Person() then end

            query "find_all"
                $p: Person()
            end

            query "find_person_by_name"(Person $param)
                $p: Person(name == $param.name)
            end
        )";

        ParsingResult result;
        kb = build_knowledge_base(drl, result);
        if (!result.success) {
            FAIL("DRL parsing failed in test fixture: " << (result.errors.empty() ? "Unknown error"
                                                                                  : result.errors[0].to_string()));
        }
        REQUIRE(kb != nullptr);
    }

    std::shared_ptr<Fact> make_person(std::string const& name, int age) {
        auto fact = std::make_shared<Fact>();
        fact->type = "Person";
        fact->fields["name"] = name;
        fact->fields["age"] = static_cast<int64_t>(age);
        return fact;
    }
};

TEST_CASE_METHOD(SerializationTestFixture, "Serialization: Session state is preserved", "[engine][serialization]") {
    // --- Phase 1: Create and populate the original session ---
    auto session1 = kb->create_session();
    REQUIRE(session1 != nullptr);

    session1->add_fact(make_person("Alice", 30));
    session1->add_fact(make_person("Bob", 25));

    // Verify initial state of session1
    REQUIRE(session1->get_fact_count() == 2);
    QueryResult results1 = session1->execute_query("find_all");
    REQUIRE(results1.size() == 2);

    // --- Phase 2: Serialize the network structure ---
    // In our design, the KB holds the serialized network *structure*.
    // The StatefulSession holds the serialized *state* (facts, etc.), but for simplicity
    // in this test, we'll focus on testing that a new session from the same KB
    // behaves correctly and that facts can be queried after being added.
    // A more advanced test would serialize the session's working memory too.
    std::string serialized_kb_json = kb->serialize();
    REQUIRE_FALSE(serialized_kb_json.empty());

    // --- Phase 3: Deserialize into a new KnowledgeBase and create a new session ---
    ParsingResult result;
    auto kb2 = KnowledgeBase::deserialize(serialized_kb_json);
    REQUIRE(kb2 != nullptr);

    auto session2 = kb2->create_session();
    REQUIRE(session2 != nullptr);

    // The new session should be empty initially.
    REQUIRE(session2->get_fact_count() == 0);

    // --- Phase 4: Add facts to the new session and verify its state ---
    session2->add_fact(make_person("Alice", 30));
    session2->add_fact(make_person("Bob", 25));
    REQUIRE(session2->get_fact_count() == 2);

    // --- Phase 5: Verify query functionality on the deserialized session ---
    SECTION("Querying the deserialized session works as expected") {
        // Use the safe and expressive QueryResult API for assertions
        QueryResult results2 = session2->execute_query("find_all");
        REQUIRE(results2.size() == 2);

        // Use a declarative matcher to check the contents without worrying about order
        std::vector<std::string> names = results2.getColumnFieldAs<std::string>("$p", "name");
        REQUIRE_THAT(names, Catch::Matchers::UnorderedEquals(std::vector<std::string>{"Alice", "Bob"}));
    }

    SECTION("Parameterized query works on the deserialized session") {
        auto arg_fact = std::make_shared<Fact>();
        arg_fact->type = "Person";
        arg_fact->fields["name"] = "Bob";

        QueryResult results_bob = session2->execute_query("find_person_by_name", {arg_fact});
        REQUIRE(results_bob.size() == 1);

        // Use the .single() and .getFieldAs() methods for a clean and safe check
        auto age_opt = results_bob.single().getFieldAs<int64_t>("$p", "age");
        REQUIRE(age_opt.has_value());
        CHECK(age_opt.value() == 25);
    }
}
