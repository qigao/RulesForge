#include "tinytest.h"
#include "rule_forge.h"

#include <string>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <cstring>

namespace {
void write_u32_le(uint8_t* buf, size_t offset, uint32_t value) {
    buf[offset + 0] = static_cast<uint8_t>(value & 0xffu);
    buf[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    buf[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
    buf[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
}

ruleforge_status_t native_predicate_true(void*, int, const char**, char** out_result) {
    if (!out_result) return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    char const value[] = "true";
    auto* result = static_cast<char*>(std::malloc(sizeof(value)));
    if (!result) return RULES_FORGE_ERROR_GENERIC;
    std::memcpy(result, value, sizeof(value));
    *out_result = result;
    return RULES_FORGE_OK;
}
}

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

        it("registers native predicate helpers") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            check_int_eq(
                ruleforge_kb_register_native_predicate(kb, "always_true", native_predicate_true, nullptr),
                RULES_FORGE_OK);
            check_int_ne(
                ruleforge_kb_register_native_predicate(kb, nullptr, native_predicate_true, nullptr),
                RULES_FORGE_OK);
            check_str_contains(ruleforge_get_last_error_message(), "Predicate name is NULL");

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

        it("keeps previous knowledge base when a new load fails") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            const char* valid_drl = R"(
declare Fact
    id: long
end

rule "AnyFactRule"
    when
        $f : Fact()
    then
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, valid_drl), RULES_FORGE_OK);

            const char* invalid_drl = R"(
rule "Broken"
    when
        $f : MissingParen(
    then
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, invalid_drl), RULES_FORGE_ERROR_COMPILATION_FAILED);

            // Should still be able to create session from the previously loaded valid KB.
            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);
            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);

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

        it("adds fact from JSON and returns stable fact handle") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            ruleforge_fact_t fact = nullptr;
            check_int_eq(
                ruleforge_session_add_fact_json_ex(
                    session,
                    "MyFact",
                    R"({"name": "Alice", "age": 30})",
                    &fact),
                RULES_FORGE_OK);
            check_not_null(fact);

            char name_buffer[32] = {0};
            size_t actual_length = 0;
            check_int_eq(
                ruleforge_fact_get_field_as_string(
                    fact, "name", name_buffer, sizeof(name_buffer), &actual_length),
                RULES_FORGE_OK);
            check_str_eq(name_buffer, "Alice");

            int64_t age = 0;
            check_int_eq(ruleforge_fact_get_field_as_int(fact, "age", &age), RULES_FORGE_OK);
            check_int_eq((int)age, 30);

            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }

        it("adds schema-bound JSON fact through TurboScript DataBind") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            auto schema_path = std::filesystem::temp_directory_path()
                / "rulesforge_capi_databind_json.schema";
            {
                std::ofstream schema(schema_path, std::ios::binary);
                schema << "schema Market [id(8), version(1), byte_order(little)]; "
                          "message Customer { int32 age; double score; string name; }";
            }

            std::string drl = std::string("import \"")
                + schema_path.generic_string()
                + R"(";
                    query "FindCustomer"
                        $c : Customer(age == 30, name == "Alice")
                    end
                )";
            check_int_eq(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            ruleforge_fact_t fact = nullptr;
            check_int_eq(
                ruleforge_session_add_fact_json_schema(
                    session,
                    schema_path.string().c_str(),
                    "Customer",
                    R"({"name":"Alice","age":30,"score":98.5})",
                    &fact),
                RULES_FORGE_OK);
            check_not_null(fact);

            int64_t age = 0;
            check_int_eq(ruleforge_fact_get_field_as_int(fact, "age", &age), RULES_FORGE_OK);
            check_int_eq((int)age, 30);

            double score = 0.0;
            check_int_eq(ruleforge_fact_get_field_as_double(fact, "score", &score), RULES_FORGE_OK);
            check(score == 98.5);

            ruleforge_query_result_t query_result = nullptr;
            check_int_eq(ruleforge_session_query(session, "FindCustomer", &query_result), RULES_FORGE_OK);
            check_not_null(query_result);
            check_int_eq(ruleforge_query_result_get_size(query_result), 1);

            check_int_eq(ruleforge_query_result_destroy(query_result), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("resolves short fact names against packaged declarations") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            const char* packaged_drl = R"(
package mqtt.broker.rules

declare MqttSubscribeTask
    client_id: String
    username: String
end

query "FindSubscribeTask"
    $task : MqttSubscribeTask(client_id == "rules-client")
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, packaged_drl), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            check_int_eq(
                ruleforge_session_add_fact_json(
                    session,
                    "MqttSubscribeTask",
                    R"({"client_id":"rules-client","username":"alice"})"),
                RULES_FORGE_OK);

            ruleforge_query_result_t query_result = nullptr;
            check_int_eq(ruleforge_session_query(session, "FindSubscribeTask", &query_result), RULES_FORGE_OK);
            check_not_null(query_result);
            check_int_eq(ruleforge_query_result_get_size(query_result), 1);

            ruleforge_fact_t fact = nullptr;
            check_int_eq(ruleforge_query_result_get_fact_at_index(query_result, 0, "task", &fact), RULES_FORGE_OK);
            check_not_null(fact);

            char username[32] = {0};
            size_t actual_length = 0;
            check_int_eq(
                ruleforge_fact_get_field_as_string(
                    fact, "username", username, sizeof(username), &actual_length),
                RULES_FORGE_OK);
            check_str_eq(username, "alice");

            check_int_eq(ruleforge_query_result_destroy(query_result), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }

        it("rejects binary payload ingestion in RulesForge runtime") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            const char* binary_drl = R"(
declare BinaryFact
    a: int
    b: int
    c: long
end

query "AllBinaryFacts"
    $f : BinaryFact()
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, binary_drl), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            uint8_t payload[16] = {0};
            int32_t a = 100;
            int32_t b = 200;
            int64_t c = 300000LL;
            std::memcpy(payload + 0, &a, sizeof(a));
            std::memcpy(payload + 4, &b, sizeof(b));
            std::memcpy(payload + 8, &c, sizeof(c));

            ruleforge_fact_t fact = nullptr;
            check_int_eq(ruleforge_session_add_fact_binary_ex(
                             session, "BinaryFact", payload, sizeof(payload), &fact),
                         RULES_FORGE_ERROR_INVALID_ARGUMENT);
            check(fact == nullptr);

            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }

        it("adds schema-bound binary fact through TurboScript DataBind") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            auto schema_path = std::filesystem::temp_directory_path()
                / "rulesforge_capi_databind_binary.schema";
            {
                std::ofstream schema(schema_path, std::ios::binary);
                schema << "schema Market [id(10), version(1), byte_order(little)]; "
                          "message BinaryFact { uint32 a; uint32 b; uint32 c; }";
            }

            std::string drl = std::string("import \"")
                + schema_path.generic_string()
                + R"(";
                    query "FindBinary"
                        $f : BinaryFact(a == 100, b == 200, c == 300)
                    end
                )";
            check_int_eq(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            uint8_t payload[12] = {0};
            write_u32_le(payload, 0, 100);
            write_u32_le(payload, 4, 200);
            write_u32_le(payload, 8, 300);

            ruleforge_fact_t fact = nullptr;
            check_int_eq(
                ruleforge_session_add_fact_binary_schema(
                    session,
                    schema_path.string().c_str(),
                    "BinaryFact",
                    payload,
                    sizeof(payload),
                    &fact),
                RULES_FORGE_OK);
            check_not_null(fact);

            int64_t c = 0;
            check_int_eq(ruleforge_fact_get_field_as_int(fact, "c", &c), RULES_FORGE_OK);
            check_int_eq((int)c, 300);

            ruleforge_query_result_t query_result = nullptr;
            check_int_eq(ruleforge_session_query(session, "FindBinary", &query_result), RULES_FORGE_OK);
            check_not_null(query_result);
            check_int_eq(ruleforge_query_result_get_size(query_result), 1);

            check_int_eq(ruleforge_query_result_destroy(query_result), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("adds schema-bound CSV facts through TurboScript DataBind") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            auto schema_path = std::filesystem::temp_directory_path()
                / "rulesforge_capi_databind_csv.schema";
            {
                std::ofstream schema(schema_path, std::ios::binary);
                schema << "schema Market [id(9), version(1), byte_order(little)]; "
                          "message Customer { int32 age; double score; string name; }";
            }

            std::string drl = std::string("import \"")
                + schema_path.generic_string()
                + R"(";
                    rule "AnyCustomer"
                    when
                        $c : Customer(age >= 18)
                    then
                    end
                )";
            check_int_eq(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            char const* csv = "name,age,score\nAlice,30,98.5\nBob,17,70.0\n";
            ruleforge_fact_t* facts = nullptr;
            int loaded = 0;
            check_int_eq(
                ruleforge_session_add_facts_csv_schema(
                    session,
                    schema_path.string().c_str(),
                    "Customer",
                    csv,
                    &facts,
                    &loaded),
                RULES_FORGE_OK);
            check_int_eq(loaded, 2);
            check_not_null(facts);
            check_size_eq(ruleforge_session_get_fact_count(session), 2);

            char name_buffer[32] = {0};
            size_t actual_length = 0;
            check_int_eq(
                ruleforge_fact_get_field_as_string(
                    facts[0], "name", name_buffer, sizeof(name_buffer), &actual_length),
                RULES_FORGE_OK);
            check_str_eq(name_buffer, "Alice");

            ruleforge_fact_array_free(facts);
            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("adds schema-bound XML facts through TurboScript DataBind") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            auto schema_path = std::filesystem::temp_directory_path()
                / "rulesforge_capi_databind_xml.schema";
            {
                std::ofstream schema(schema_path, std::ios::binary);
                schema << "schema Market [id(11), version(1), byte_order(little)]; "
                          "enum Side <uint8> { Buy = 1; Sell = 2; } "
                          "message Order { uint32 id; Side side; string symbol; }";
            }

            std::string drl = std::string("import \"")
                + schema_path.generic_string()
                + R"(";
                    query "FindOrder"
                        $o : Order(id == 2, side == 2, symbol == "WXYZ")
                    end
                )";
            check_int_eq(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            char const* xml =
                "<orders>"
                "<order><id>1</id><side>Buy</side><symbol>ABCD</symbol></order>"
                "<order><id>2</id><side>Sell</side><symbol>WXYZ</symbol></order>"
                "</orders>";
            ruleforge_fact_t* facts = nullptr;
            int loaded = 0;
            check_int_eq(
                ruleforge_session_add_facts_xml_schema(
                    session,
                    schema_path.string().c_str(),
                    "Order",
                    xml,
                    "//order",
                    &facts,
                    &loaded),
                RULES_FORGE_OK);
            check_int_eq(loaded, 2);
            check_not_null(facts);

            int64_t side = 0;
            check_int_eq(ruleforge_fact_get_field_as_int(facts[1], "side", &side), RULES_FORGE_OK);
            check_int_eq((int)side, 2);

            ruleforge_query_result_t query_result = nullptr;
            check_int_eq(ruleforge_session_query(session, "FindOrder", &query_result), RULES_FORGE_OK);
            check_not_null(query_result);
            check_int_eq(ruleforge_query_result_get_size(query_result), 1);

            check_int_eq(ruleforge_query_result_destroy(query_result), RULES_FORGE_OK);
            ruleforge_fact_array_free(facts);
            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("adds facts from CSV string") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            const char* csv = "name,age,score,active\nAlice,30,95.5,true\nBob,17,70,false\n";
            int loaded = 0;
            check_int_eq(
                ruleforge_session_add_facts_csv(session, "Person", csv, &loaded),
                RULES_FORGE_OK
            );
            check_int_eq(loaded, 2);
            check_size_eq(ruleforge_session_get_fact_count(session), 2);

            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }

        it("adds facts from CSV string and returns fact handles") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            const char* csv = "name,age\nAlice,30\nBob,17\n";
            ruleforge_fact_t* facts = nullptr;
            int loaded = 0;
            check_int_eq(
                ruleforge_session_add_facts_csv_ex(session, "Person", csv, &facts, &loaded),
                RULES_FORGE_OK
            );
            check_int_eq(loaded, 2);
            check_not_null(facts);
            check_not_null(facts[0]);
            check_not_null(facts[1]);

            char name_buffer[32] = {0};
            size_t actual_length = 0;
            check_int_eq(
                ruleforge_fact_get_field_as_string(
                    facts[0], "name", name_buffer, sizeof(name_buffer), &actual_length),
                RULES_FORGE_OK);
            check_str_eq(name_buffer, "Alice");

            int64_t age = 0;
            check_int_eq(ruleforge_fact_get_field_as_int(facts[1], "age", &age), RULES_FORGE_OK);
            check_int_eq((int)age, 17);

            ruleforge_fact_array_free(facts);
            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }

        it("adds facts from CSV file") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            const char* temp_csv_path = "capi_test_temp.csv";
            {
                std::ofstream out(temp_csv_path, std::ios::binary);
                out << "name,age\nCharlie,21\nDiana,42\n";
            }

            int loaded = 0;
            check_int_eq(
                ruleforge_session_add_facts_csv_file(session, "Person", temp_csv_path, &loaded),
                RULES_FORGE_OK
            );
            check_int_eq(loaded, 2);
            check_size_eq(ruleforge_session_get_fact_count(session), 2);

            std::remove(temp_csv_path);
            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }

        it("adds facts from CSV file and returns fact handles") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            const char* temp_csv_path = "capi_test_temp_handles.csv";
            {
                std::ofstream out(temp_csv_path, std::ios::binary);
                out << "name,age\nCharlie,21\nDiana,42\n";
            }

            ruleforge_fact_t* facts = nullptr;
            int loaded = 0;
            check_int_eq(
                ruleforge_session_add_facts_csv_file_ex(session, "Person", temp_csv_path, &facts, &loaded),
                RULES_FORGE_OK
            );
            check_int_eq(loaded, 2);
            check_not_null(facts);
            check_not_null(facts[0]);
            check_not_null(facts[1]);

            char name_buffer[32] = {0};
            size_t actual_length = 0;
            check_int_eq(
                ruleforge_fact_get_field_as_string(
                    facts[1], "name", name_buffer, sizeof(name_buffer), &actual_length),
                RULES_FORGE_OK);
            check_str_eq(name_buffer, "Diana");

            ruleforge_fact_array_free(facts);
            std::remove(temp_csv_path);
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

        it("fires rules through explicit native eval predicates") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            check_int_eq(
                ruleforge_kb_register_native_predicate(kb, "always_true", native_predicate_true, nullptr),
                RULES_FORGE_OK);

            const char* native_eval_drl = R"(
declare Fact
    id: long
end

rule "NativeEvalRule"
    when
        $f : Fact()
        eval(native.always_true())
    then
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, native_eval_drl), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);
            check_int_eq(ruleforge_session_add_fact_json(session, "Fact", R"({"id": 1})"), RULES_FORGE_OK);

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

            bool has_bob = false;
            bool has_diana = false;
            char name_buffer[50];
            size_t actual_length = 0;
            for (int i = 0; i < result_size; ++i) {
                ruleforge_fact_t fact_row = nullptr;
                check_int_eq(ruleforge_query_result_get_fact_at_index(query_result, i, "p", &fact_row), RULES_FORGE_OK);
                check_not_null(fact_row);

                check_int_eq(
                    ruleforge_fact_get_field_as_string(
                        fact_row, "name", name_buffer, sizeof(name_buffer), &actual_length),
                    RULES_FORGE_OK);

                double age_double = 0.0;
                check_int_eq(ruleforge_fact_get_field_as_double(fact_row, "age", &age_double), RULES_FORGE_OK);
                int64_t age_int = 0;
                check_int_eq(ruleforge_fact_get_field_as_int(fact_row, "age", &age_int), RULES_FORGE_OK);

                if (std::string(name_buffer) == "Bob") {
                    has_bob = true;
                    check_float_eq(age_double, 25.0, 0.001);
                    check_long_eq(age_int, 25);
                } else if (std::string(name_buffer) == "Diana") {
                    has_diana = true;
                    check_float_eq(age_double, 30.0, 0.001);
                    check_long_eq(age_int, 30);
                }
            }
            check(has_bob);
            check(has_diana);

            // Keep field-missing path covered.
            ruleforge_fact_t any_fact = nullptr;
            check_int_eq(ruleforge_query_result_get_fact_at_index(query_result, 0, "p", &any_fact), RULES_FORGE_OK);
            check_int_ne(
                ruleforge_fact_get_field_as_string(
                    any_fact, "nonExistent", name_buffer, sizeof(name_buffer), &actual_length),
                RULES_FORGE_OK);

            check_int_eq(ruleforge_query_result_destroy(query_result), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }

        it("returns query error when query does not exist") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            const char* drl = R"(
declare Person
    name: String
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, drl), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            ruleforge_query_result_t query_result = reinterpret_cast<ruleforge_query_result_t>(0x1);
            check_int_eq(
                ruleforge_session_query(session, "NoSuchQuery", &query_result),
                RULES_FORGE_ERROR_QUERY_FAILED);
            check(query_result == nullptr);
            check_str_contains(ruleforge_get_last_error_message(), "not found");

            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();

            // Keep this as the last test and extend it with consistency error mapping checks
            // so tinytest's current test-count cap still covers these assertions.
            ruleforge_init();
            kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            const char* failing_drl = R"(
declare Person
    name: String
    age: int
end
declare Audit
    code: int
end

rule "RollbackFailure"
    when
        $p : Person(name == "Alice")
    then
        update $p { age = "invalid" }
        retract $p
        insert Audit { code = "bad" }
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, failing_drl), RULES_FORGE_OK);

            session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            check_int_eq(
                ruleforge_session_set_validation_mode(session, RULES_FORGE_VALIDATION_STRICT),
                RULES_FORGE_OK);
            check_int_eq(
                ruleforge_session_add_fact_json(session, "Person", R"({"name":"Alice","age":30})"),
                RULES_FORGE_OK);

            // First run fails in RHS and leaves session inconsistent due to rollback failure.
            check_int_ne(ruleforge_session_fire_all_rules(session, -1, nullptr), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_get_fact_count(session), 0);

            check_int_eq(
                ruleforge_session_add_fact_json(session, "Person", R"({"name":"Bob","age":40})"),
                RULES_FORGE_ERROR_SESSION_INCONSISTENT);
            check_str_contains(ruleforge_get_last_error_message(), "inconsistent");

            check_int_eq(
                ruleforge_session_set_validation_mode(session, RULES_FORGE_VALIDATION_WARN),
                RULES_FORGE_ERROR_SESSION_INCONSISTENT);
            check_int_eq(
                ruleforge_session_enable_tracing(session, 1),
                RULES_FORGE_ERROR_SESSION_INCONSISTENT);
            check_int_eq(
                ruleforge_session_fire_all_rules(session, -1, nullptr),
                RULES_FORGE_ERROR_SESSION_INCONSISTENT);
            check_int_eq(
                ruleforge_session_set_validation_mode(
                    session, static_cast<ruleforge_validation_mode_t>(999)),
                RULES_FORGE_ERROR_INVALID_ARGUMENT);

            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }
    }
}
