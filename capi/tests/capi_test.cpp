#include "tinytest.h"
#include "rule_forge.h"

#include <string>
#include <cstdio>

suite("CAPI") {
    group("Initialization and Cleanup") {
        it("initializes and cleans up") {
            check_int_eq(ruleforge_init(), RULES_FORGE_OK);
            check_int_eq(ruleforge_cleanup(), RULES_FORGE_OK);
        }
    }

    group("Version Information") {
        it("returns version string") {
            const char* version = ruleforge_get_version();
            check_not_null(version);
            check_str_eq(version, RULEFORGE_VERSION_STRING);
        }
    }

    group("Error Handling") {
        it("reports errors correctly") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            ruleforge_status_t status = ruleforge_kb_create(nullptr);
            check_int_ne(status, RULES_FORGE_OK);
            check_str_contains(ruleforge_get_last_error_message(), "NULL");
            ruleforge_cleanup();
        }
    }

    group("Knowledge Base Management") {
        it("creates and destroys knowledge base") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;

            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            check_not_null(kb);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);

            ruleforge_cleanup();
        }

        it("loads RFL") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

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
            check_int_eq(ruleforge_kb_load_drl(kb, simple_drl), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }

        it("loads decision table CSV") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            const char* simple_csv = R"(
Rule Name,CONDITION value,ACTION result
Rule1,hello,world
Rule2,test,passed
)";
            check_int_eq(ruleforge_kb_load_decision_table_csv(kb, simple_csv), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }
    }

    group("Stateful Session and Fact Management") {
        it("adds fact from JSON") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            const char* fact_type = "MyFact";
            const char* fact_json = R"({"name": "Alice", "age": 30, "isStudent": true, "score": 95.5})";
            check_int_eq(ruleforge_session_add_fact_json(session, fact_type, fact_json), RULES_FORGE_OK);

            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }

        it("fires all rules") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

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
            check_int_eq(ruleforge_kb_load_drl(kb, simple_drl), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            const char* fact_type = "Fact";
            const char* fact_json = R"({"id": 1})";
            check_int_eq(ruleforge_session_add_fact_json(session, fact_type, fact_json), RULES_FORGE_OK);

            int fired_count = 0;
            check_int_eq(ruleforge_session_fire_all_rules(session, -1, &fired_count), RULES_FORGE_OK);
            check_int_eq(fired_count, 1);

            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }

        it("queries facts and accesses fields") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

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
            check_int_eq(ruleforge_kb_load_drl(kb, query_drl), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            check_int_eq(ruleforge_session_add_fact_json(session, "Person", R"({"name": "Bob", "age": 25})"), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_add_fact_json(session, "Person", R"({"name": "Charlie", "age": 17})"), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_add_fact_json(session, "Person", R"({"name": "Diana", "age": 30})"), RULES_FORGE_OK);

            check_size_eq(ruleforge_session_get_fact_count(session), 3);

            check_int_eq(ruleforge_session_fire_all_rules(session, -1, nullptr), RULES_FORGE_OK);

            ruleforge_query_result_t query_result = nullptr;
            check_int_eq(ruleforge_session_query(session, "AdultPersons", &query_result), RULES_FORGE_OK);
            check_not_null(query_result);

            int result_size = ruleforge_query_result_get_size(query_result);
            check_int_eq(result_size, 2);

            ruleforge_fact_t fact_bob = nullptr;
            check_int_eq(ruleforge_query_result_get_fact_at_index(query_result, 0, "p", &fact_bob), RULES_FORGE_OK);
            check_not_null(fact_bob);

            char name_buffer[50];
            size_t actual_length = 0;
            check_int_eq(ruleforge_fact_get_field_as_string(fact_bob, "name", name_buffer, sizeof(name_buffer), &actual_length), RULES_FORGE_OK);
            check_str_eq(name_buffer, "Bob");

            double age_double = 0.0;
            check_int_eq(ruleforge_fact_get_field_as_double(fact_bob, "age", &age_double), RULES_FORGE_OK);
            check_float_eq(age_double, 25.0, 0.001);

            int64_t age_int = 0;
            check_int_eq(ruleforge_fact_get_field_as_int(fact_bob, "age", &age_int), RULES_FORGE_OK);
            check_long_eq(age_int, 25);

            check_int_ne(ruleforge_fact_get_field_as_string(fact_bob, "nonExistent", name_buffer, sizeof(name_buffer), &actual_length), RULES_FORGE_OK);

            check_int_eq(ruleforge_query_result_destroy(query_result), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }
    }
}
