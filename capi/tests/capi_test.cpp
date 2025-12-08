#include "catch2/catch_all.hpp"
#include "rule_forge.h"

#include <string>
#include <vector>

// Helper to check for API errors
#define REQUIRE_DRILLS_OK(status) REQUIRE(status == DRILLS_OK)
#define REQUIRE_DRILLS_ERROR(status) REQUIRE(status != DRILLS_OK)

TEST_CASE("CAPI: Initialization and Cleanup", "[capi]") {
    REQUIRE_DRILLS_OK(ruleforge_init());
    REQUIRE_DRILLS_OK(ruleforge_cleanup());
}

TEST_CASE("CAPI: Version Information", "[capi]") {
    const char* version = ruleforge_get_version();
    REQUIRE(version != nullptr);
    REQUIRE(std::string(version) == RULEFORGE_VERSION_STRING);
}

TEST_CASE("CAPI: Error Handling", "[capi]") {
    ruleforge_init();
    ruleforge_knowledge_base_t kb = nullptr;
    ruleforge_status_t status = ruleforge_kb_create(nullptr); // Pass NULL to trigger error
    REQUIRE_DRILLS_ERROR(status);
    REQUIRE(std::string(ruleforge_get_last_error_message()).find("NULL") != std::string::npos);
    ruleforge_cleanup();
}

TEST_CASE("CAPI: Knowledge Base Management", "[capi]") {
    ruleforge_init();
    ruleforge_knowledge_base_t kb = nullptr;

    SECTION("Create and Destroy Knowledge Base") {
        REQUIRE_DRILLS_OK(ruleforge_kb_create(&kb));
        REQUIRE(kb != nullptr);
        REQUIRE_DRILLS_OK(ruleforge_kb_destroy(kb));
        kb = nullptr; // Clear handle after destruction
    }

    SECTION("Load DRL (Placeholder - requires valid DRL)") {
        REQUIRE_DRILLS_OK(ruleforge_kb_create(&kb));
        const char* simple_drl = R"(
declare Fact
    value: String
end

rule "HelloWorld"
    when
        $f : Fact(value == "hello")
    then
        // No action, just for parsing test
end
)";
        // This test will pass if the DRL parser is robust enough for this simple case.
        // Real DRL parsing requires a fully functional parser.
        REQUIRE_DRILLS_OK(ruleforge_kb_load_drl(kb, simple_drl));
        REQUIRE_DRILLS_OK(ruleforge_kb_destroy(kb));
    }

    SECTION("Load Decision Table CSV") {
        REQUIRE_DRILLS_OK(ruleforge_kb_create(&kb));
        const char* simple_csv = R"(
Rule Name,CONDITION value,ACTION result
Rule1,hello,world
Rule2,test,passed
)";
        // This tests the temporary CSV parser and DecisionTableConverter.
        REQUIRE_DRILLS_OK(ruleforge_kb_load_decision_table_csv(kb, simple_csv));
        REQUIRE_DRILLS_OK(ruleforge_kb_destroy(kb));
    }

    ruleforge_cleanup();
}

TEST_CASE("CAPI: Stateful Session and Fact Management", "[capi]") {
    ruleforge_init();
    ruleforge_knowledge_base_t kb = nullptr;
    REQUIRE_DRILLS_OK(ruleforge_kb_create(&kb));

    SECTION("Add Fact from JSON") {
        ruleforge_stateful_session_t session = nullptr;
        REQUIRE_DRILLS_OK(ruleforge_session_create(kb, &session));
        REQUIRE(session != nullptr);

        const char* fact_type = "MyFact";
        const char* fact_json = R"({"name": "Alice", "age": 30, "isStudent": true, "score": 95.5})";
        REQUIRE_DRILLS_OK(ruleforge_session_add_fact_json(session, fact_type, fact_json));

        REQUIRE_DRILLS_OK(ruleforge_session_destroy(session));
    }

    SECTION("Fire All Rules (requires rules to be loaded)") {
        // Load a simple rule that matches any fact
        const char* simple_drl = R"(
declare Fact
    id: long
end

rule "AnyFactRule"
    when
        $f : Fact()
    then
        // No action, just for firing test
end
)";
        REQUIRE_DRILLS_OK(ruleforge_kb_load_drl(kb, simple_drl));

        ruleforge_stateful_session_t session = nullptr;
        REQUIRE_DRILLS_OK(ruleforge_session_create(kb, &session));
        REQUIRE(session != nullptr);

        const char* fact_type = "Fact";
        const char* fact_json = R"({"id": 1})";
        REQUIRE_DRILLS_OK(ruleforge_session_add_fact_json(session, fact_type, fact_json));

        int fired_count = 0;
        REQUIRE_DRILLS_OK(ruleforge_session_fire_all_rules(session, -1, &fired_count));
        REQUIRE(fired_count == 1);
        REQUIRE_DRILLS_OK(ruleforge_session_destroy(session));
    }

    SECTION("Query Facts and Access Fields") {
        // Load a rule and query
        const char* query_drl = R"(
declare Person
    name: String
    age: int
end

rule "PersonRule"
    when
        $p : Person(age > 18)
    then
        // No action
end

query "AdultPersons"
    $p : Person(age > 18)
end
)";
        REQUIRE_DRILLS_OK(ruleforge_kb_load_drl(kb, query_drl));

        ruleforge_stateful_session_t session = nullptr;
        REQUIRE_DRILLS_OK(ruleforge_session_create(kb, &session));
        REQUIRE(session != nullptr);

        // Add some facts
        REQUIRE_DRILLS_OK(ruleforge_session_add_fact_json(session, "Person", R"({"name": "Bob", "age": 25})"));
        REQUIRE_DRILLS_OK(ruleforge_session_add_fact_json(session, "Person", R"({"name": "Charlie", "age": 17})"));
        REQUIRE_DRILLS_OK(ruleforge_session_add_fact_json(session, "Person", R"({"name": "Diana", "age": 30})"));

        // Test fact count
        REQUIRE(ruleforge_session_get_fact_count(session) == 3);

        REQUIRE_DRILLS_OK(ruleforge_session_fire_all_rules(session, -1, nullptr));

        ruleforge_query_result_t query_result = nullptr;
        REQUIRE_DRILLS_OK(ruleforge_session_query(session, "AdultPersons", &query_result));
        REQUIRE(query_result != nullptr);

        int result_size = ruleforge_query_result_get_size(query_result);
        REQUIRE(result_size == 2); // Bob and Diana

        ruleforge_fact_t fact_bob = nullptr;
        REQUIRE_DRILLS_OK(ruleforge_query_result_get_fact_at_index(query_result, 0, "p", &fact_bob));
        REQUIRE(fact_bob != nullptr);

        char name_buffer[50];
        size_t actual_length = 0;
        REQUIRE_DRILLS_OK(ruleforge_fact_get_field_as_string(fact_bob, "name", name_buffer, sizeof(name_buffer), &actual_length));
        REQUIRE(std::string(name_buffer) == "Bob");

        double age_double = 0.0;
        REQUIRE_DRILLS_OK(ruleforge_fact_get_field_as_double(fact_bob, "age", &age_double));
        REQUIRE(age_double == 25.0);

        // Test boolean and int (assuming age can be retrieved as int64_t)
        int64_t age_int = 0;
        REQUIRE_DRILLS_OK(ruleforge_fact_get_field_as_int(fact_bob, "age", &age_int));
        REQUIRE(age_int == 25);

        // Test non-existent field
        REQUIRE_DRILLS_ERROR(ruleforge_fact_get_field_as_string(fact_bob, "nonExistent", name_buffer, sizeof(name_buffer), &actual_length));

        REQUIRE_DRILLS_OK(ruleforge_query_result_destroy(query_result));
        REQUIRE_DRILLS_OK(ruleforge_session_destroy(session));
    }

    REQUIRE_DRILLS_OK(ruleforge_kb_destroy(kb));
    ruleforge_cleanup();
}
