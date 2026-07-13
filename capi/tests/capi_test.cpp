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

std::filesystem::path write_temp_schema(std::string const& name, std::string const& schema_text) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream schema(path, std::ios::binary);
    schema << schema_text;
    return path;
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
            check_int_eq(ruleforge_kb_set_execution_mode(kb, RULES_FORGE_EXECUTION_MODE_V2_HIGH_PERFORMANCE),
                         RULES_FORGE_OK);
            check_str_eq(ruleforge_kb_get_execution_mode(kb), "v2_high_performance");
            check_int_eq(ruleforge_kb_set_execution_mode(kb, RULES_FORGE_EXECUTION_MODE_V1_STANDARD),
                         RULES_FORGE_OK);
            check_str_eq(ruleforge_kb_get_execution_mode(kb), "v1_standard");
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);

            ruleforge_cleanup();
        }

        it("loads RFL") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_set_execution_mode(kb, RULES_FORGE_EXECUTION_MODE_V2_HIGH_PERFORMANCE),
                         RULES_FORGE_OK);

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
            check_str_eq(ruleforge_kb_get_execution_mode(kb), "v2_high_performance");
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
        it("adds schema-bound JSON fact through TurboUtils DataBind") {
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

        it("adds a schema-bound JSON fact from asynchronous chunks") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            auto schema_path = write_temp_schema(
                "rulesforge_capi_databind_json_stream.schema",
                "schema Market [id(31), version(1), byte_order(little)]; "
                "message Customer { int32 age; double score; string name; }");
            std::string drl = std::string("import \"") + schema_path.generic_string() + R"(";
query "FindStreamCustomer"
    $c : Customer(age == 30, name == "Alice")
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            ruleforge_data_bind_stream_t stream = nullptr;
            check_int_eq(ruleforge_data_bind_stream_json_create(
                             session, schema_path.string().c_str(), "Customer", &stream),
                         RULES_FORGE_OK);
            check_not_null(stream);
            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_ERROR_INVALID_ARGUMENT);
            check_str_contains(ruleforge_get_last_error_message(), "active");

            char const* part_one = R"({"name":"Ali)";
            char const* part_two = R"(ce","age":30,"score":98.5})";
            check_int_eq(ruleforge_data_bind_stream_feed(stream, part_one, std::strlen(part_one)),
                         RULES_FORGE_OK);
            check_int_eq(ruleforge_data_bind_stream_feed(stream, part_two, std::strlen(part_two)),
                         RULES_FORGE_OK);
            check_size_eq(ruleforge_session_get_fact_count(session), 0);

            ruleforge_fact_t* facts = nullptr;
            int loaded = 0;
            check_int_eq(ruleforge_data_bind_stream_finish(stream, &facts, &loaded),
                         RULES_FORGE_OK);
            check_int_eq(loaded, 1);
            check_not_null(facts);
            check_size_eq(ruleforge_session_get_fact_count(session), 1);
            char name[16] = {0};
            size_t actual_length = 0;
            check_int_eq(ruleforge_fact_get_field_as_string(
                             facts[0], "name", name, sizeof(name), &actual_length),
                         RULES_FORGE_OK);
            check_str_eq(name, "Alice");
            check_int_eq(ruleforge_data_bind_stream_feed(stream, " ", 1),
                         RULES_FORGE_ERROR_INVALID_ARGUMENT);

            ruleforge_fact_array_free(facts);
            check_int_eq(ruleforge_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("filters complete and streamed JSON by path before rule evaluation") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            auto schema_path = write_temp_schema(
                "rulesforge_capi_databind_json_path.schema",
                "schema Market [id(34), version(1), byte_order(little)]; "
                "message Customer { int32 age; string name; }");
            std::string drl = std::string("import \"") + schema_path.generic_string() + R"(";
query "Adults"
    $customer : Customer(age >= 18)
end
rule "Adult customer"
when
    Customer(age >= 18)
then
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            char const* json =
                R"({"customers":[{"name":"Alice","age":30},{"name":"Bob","age":17}],)"
                R"("ignored":[{"name":"Mallory","age":40}]})";
            char const* json_path = "$.customers[*]";

            ruleforge_stateful_session_t complete_session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &complete_session), RULES_FORGE_OK);
            ruleforge_fact_t* complete_facts = nullptr;
            int complete_count = 0;
            check_int_eq(ruleforge_session_add_facts_json_path_schema(
                             complete_session, schema_path.string().c_str(), "Customer",
                             json, json_path, &complete_facts, &complete_count),
                         RULES_FORGE_OK);
            check_int_eq(complete_count, 2);
            check_size_eq(ruleforge_session_get_fact_count(complete_session), 2);
            int complete_fired = 0;
            check_int_eq(ruleforge_session_fire_all_rules(
                             complete_session, -1, &complete_fired), RULES_FORGE_OK);
            check_int_eq(complete_fired, 1);
            ruleforge_query_result_t complete_query = nullptr;
            check_int_eq(ruleforge_session_query(
                             complete_session, "Adults", &complete_query), RULES_FORGE_OK);
            check_int_eq(ruleforge_query_result_get_size(complete_query), 1);

            ruleforge_stateful_session_t stream_session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &stream_session), RULES_FORGE_OK);
            ruleforge_data_bind_stream_t stream = nullptr;
            check_int_eq(ruleforge_data_bind_stream_json_path_all_create(
                             stream_session, schema_path.string().c_str(), "Customer",
                             json_path, &stream), RULES_FORGE_OK);
            size_t split = std::strlen(json) / 2;
            check_int_eq(ruleforge_data_bind_stream_feed(stream, json, split), RULES_FORGE_OK);
            check_int_eq(ruleforge_data_bind_stream_feed(
                             stream, json + split, std::strlen(json) - split), RULES_FORGE_OK);
            ruleforge_fact_t* stream_facts = nullptr;
            int stream_count = 0;
            check_int_eq(ruleforge_data_bind_stream_finish(
                             stream, &stream_facts, &stream_count), RULES_FORGE_OK);
            check_int_eq(stream_count, complete_count);
            check_size_eq(ruleforge_session_get_fact_count(stream_session), 2);
            int stream_fired = 0;
            check_int_eq(ruleforge_session_fire_all_rules(
                             stream_session, -1, &stream_fired), RULES_FORGE_OK);
            check_int_eq(stream_fired, complete_fired);
            ruleforge_query_result_t stream_query = nullptr;
            check_int_eq(ruleforge_session_query(
                             stream_session, "Adults", &stream_query), RULES_FORGE_OK);
            check_int_eq(ruleforge_query_result_get_size(stream_query), 1);

            ruleforge_stateful_session_t first_session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &first_session), RULES_FORGE_OK);
            ruleforge_fact_t first = nullptr;
            check_int_eq(ruleforge_session_add_fact_json_path_schema(
                             first_session, schema_path.string().c_str(), "Customer",
                             json, json_path, &first), RULES_FORGE_OK);
            check_not_null(first);
            char name[16] = {0};
            size_t name_length = 0;
            check_int_eq(ruleforge_fact_get_field_as_string(
                             first, "name", name, sizeof(name), &name_length), RULES_FORGE_OK);
            check_str_eq(name, "Alice");

            check_int_eq(ruleforge_query_result_destroy(stream_query), RULES_FORGE_OK);
            check_int_eq(ruleforge_query_result_destroy(complete_query), RULES_FORGE_OK);
            ruleforge_fact_array_free(stream_facts);
            ruleforge_fact_array_free(complete_facts);
            check_int_eq(ruleforge_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(first_session), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(stream_session), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(complete_session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("loads YAML roots and YPATH-selected facts through TurboUtils DataBind") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            auto schema_path = write_temp_schema(
                "rulesforge_capi_databind_yaml.schema",
                "schema Market [id(37), version(1), byte_order(little)]; "
                "message Customer { int32 age; string name; }");
            std::string drl = std::string("import \"") + schema_path.generic_string() + R"(";
query "Adults"
    $customer : Customer(age >= 18)
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            char const* root_yaml = "name: Alice\nage: 30\n";
            ruleforge_stateful_session_t root_session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &root_session), RULES_FORGE_OK);
            ruleforge_fact_t root_fact = nullptr;
            check_int_eq(ruleforge_session_add_fact_yaml_schema(
                             root_session, schema_path.string().c_str(), "Customer",
                             root_yaml, &root_fact), RULES_FORGE_OK);
            check_not_null(root_fact);

            char const* yaml =
                "customers:\n"
                "  - name: Alice\n"
                "    age: 30\n"
                "  - name: Bob\n"
                "    age: 17\n"
                "ignored:\n"
                "  - name: Mallory\n"
                "    age: 40\n";
            char const* yaml_path = "/customers/*";

            ruleforge_stateful_session_t complete_session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &complete_session), RULES_FORGE_OK);
            ruleforge_fact_t* complete_facts = nullptr;
            int complete_count = 0;
            check_int_eq(ruleforge_session_add_facts_yaml_path_schema(
                             complete_session, schema_path.string().c_str(), "Customer",
                             yaml, yaml_path, &complete_facts, &complete_count),
                         RULES_FORGE_OK);
            check_int_eq(complete_count, 2);

            ruleforge_stateful_session_t first_session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &first_session), RULES_FORGE_OK);
            ruleforge_fact_t first_fact = nullptr;
            check_int_eq(ruleforge_session_add_fact_yaml_path_schema(
                             first_session, schema_path.string().c_str(), "Customer",
                             yaml, yaml_path, &first_fact), RULES_FORGE_OK);
            char name[16] = {0};
            size_t name_length = 0;
            check_int_eq(ruleforge_fact_get_field_as_string(
                             first_fact, "name", name, sizeof(name), &name_length),
                         RULES_FORGE_OK);
            check_str_eq(name, "Alice");

            ruleforge_stateful_session_t stream_session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &stream_session), RULES_FORGE_OK);
            ruleforge_data_bind_stream_t stream = nullptr;
            check_int_eq(ruleforge_data_bind_stream_yaml_path_all_create(
                             stream_session, schema_path.string().c_str(), "Customer",
                             yaml_path, &stream), RULES_FORGE_OK);
            size_t split = std::strlen(yaml) / 2;
            check_int_eq(ruleforge_data_bind_stream_feed(stream, yaml, split), RULES_FORGE_OK);
            check_int_eq(ruleforge_data_bind_stream_feed(
                             stream, yaml + split, std::strlen(yaml) - split),
                         RULES_FORGE_OK);
            ruleforge_fact_t* stream_facts = nullptr;
            int stream_count = 0;
            check_int_eq(ruleforge_data_bind_stream_finish(
                             stream, &stream_facts, &stream_count), RULES_FORGE_OK);
            check_int_eq(stream_count, complete_count);
            check_size_eq(ruleforge_session_get_fact_count(stream_session), 2);

            ruleforge_fact_array_free(stream_facts);
            ruleforge_fact_array_free(complete_facts);
            check_int_eq(ruleforge_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(stream_session), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(first_session), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(complete_session), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(root_session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("adds schema-bound extended scalar facts through TurboUtils DataBind") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            auto schema_path = std::filesystem::temp_directory_path()
                / "rulesforge_capi_databind_scalars.schema";
            {
                std::ofstream schema(schema_path, std::ios::binary);
                schema << "schema Market [id(12), version(1), byte_order(little)]; "
                          "message ScalarFact { "
                          "uuid id; date trade_date; time trade_time; duration latency; "
                          "decimal price; bigint sequence; money total; bool active; "
                          "}";
            }

            std::string drl = std::string("import \"")
                + schema_path.generic_string()
                + R"(";
                    query "FindScalar"
                        $s : ScalarFact(
                            id == "01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001",
                            trade_date == "2026-06-28",
                            trade_time == "09:30:05.123",
                            latency == 5405250,
                            price == "123.45",
                            sequence == "123456789012345678901234567890",
                            total == "USD 123.45"
                        )
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
                    "ScalarFact",
                    R"({
                        "id":"01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001",
                        "trade_date":"2026-06-28",
                        "trade_time":"09:30:05.123",
                        "latency":"1h30m5s250ms",
                        "price":"123.4500",
                        "sequence":"000123456789012345678901234567890",
                        "total":{"amount":"123.4500","currency":"USD"},
                        "active":true
                    })",
                    &fact),
                RULES_FORGE_OK);
            check_not_null(fact);

            int64_t latency = 0;
            check_int_eq(ruleforge_fact_get_field_as_int(fact, "latency", &latency), RULES_FORGE_OK);
            check_int_eq((int)latency, 5405250);

            int active = 0;
            check_int_eq(ruleforge_fact_get_field_as_bool(fact, "active", &active), RULES_FORGE_OK);
            check_int_eq(active, 1);

            char text_buffer[96] = {0};
            size_t actual_length = 0;
            check_int_eq(
                ruleforge_fact_get_field_as_string(
                    fact, "total", text_buffer, sizeof(text_buffer), &actual_length),
                RULES_FORGE_OK);
            check_str_eq(text_buffer, "USD 123.45");

            ruleforge_query_result_t query_result = nullptr;
            check_int_eq(ruleforge_session_query(session, "FindScalar", &query_result), RULES_FORGE_OK);
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

            auto schema_path = write_temp_schema(
                "rulesforge_capi_mqtt.schema",
                "schema Mqtt [id(20), version(1), byte_order(little)]; "
                "message MqttSubscribeTask { string client_id; string username; }");
            std::string packaged_drl = std::string(R"(
package mqtt.broker.rules

import schema ")") + schema_path.generic_string() + R"("

query "FindSubscribeTask"
    $task : MqttSubscribeTask(client_id == "rules-client")
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, packaged_drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            check_int_eq(
                ruleforge_session_add_fact_json_schema(
                    session,
                    schema_path.generic_string().c_str(),
                    "MqttSubscribeTask",
                    R"({"client_id":"rules-client","username":"alice"})",
                    nullptr),
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
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("adds schema-bound binary fact through TurboUtils DataBind") {
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

        it("adds schema-bound CSV facts through TurboUtils DataBind") {
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

        it("adds schema-bound CSV facts from asynchronous chunks") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            auto schema_path = write_temp_schema(
                "rulesforge_capi_databind_csv_stream.schema",
                "schema Market [id(32), version(1), byte_order(little)]; "
                "message Customer { int32 age; double score; string name; }");
            std::string drl = std::string("import \"") + schema_path.generic_string() + R"(";
rule "AnyCustomer"
when
    $c : Customer()
then
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            ruleforge_data_bind_stream_t stream = nullptr;
            check_int_eq(ruleforge_data_bind_stream_csv_all_create(
                             session, schema_path.string().c_str(), "Customer", &stream),
                         RULES_FORGE_OK);
            char const* head = "name,age,score\nAli";
            char const* tail = "ce,30,98.5\nBob,17,70.0\n";
            check_int_eq(ruleforge_data_bind_stream_feed(stream, head, std::strlen(head)),
                         RULES_FORGE_OK);
            check_int_eq(ruleforge_data_bind_stream_feed(stream, tail, std::strlen(tail)),
                         RULES_FORGE_OK);

            ruleforge_fact_t* facts = nullptr;
            int loaded = 0;
            check_int_eq(ruleforge_data_bind_stream_finish(stream, &facts, &loaded),
                         RULES_FORGE_OK);
            check_int_eq(loaded, 2);
            check_not_null(facts);
            check_size_eq(ruleforge_session_get_fact_count(session), 2);

            ruleforge_fact_array_free(facts);
            check_int_eq(ruleforge_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("filters complete and streamed CSV by path before rule evaluation") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            auto schema_path = write_temp_schema(
                "rulesforge_capi_databind_csv_path.schema",
                "schema Market [id(35), version(1), byte_order(little)]; "
                "message Customer { int32 age; string name; string region; }");
            std::string drl = std::string("import \"") + schema_path.generic_string() + R"(";
rule "Adult customer"
when
    Customer(age >= 18)
then
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            char const* csv =
                "name_s,age_n,region_s\nAlice,30,west\nBob,17,west\nMallory,40,east\n";
            char const* csv_path = "region == \"west\"";

            ruleforge_stateful_session_t complete_session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &complete_session), RULES_FORGE_OK);
            ruleforge_fact_t* complete_facts = nullptr;
            int complete_count = 0;
            check_int_eq(ruleforge_session_add_facts_csv_path_schema(
                             complete_session, schema_path.string().c_str(), "Customer",
                             csv, csv_path, &complete_facts, &complete_count),
                         RULES_FORGE_OK);
            check_int_eq(complete_count, 2);
            check_size_eq(ruleforge_session_get_fact_count(complete_session), 2);
            int complete_fired = 0;
            check_int_eq(ruleforge_session_fire_all_rules(
                             complete_session, -1, &complete_fired), RULES_FORGE_OK);
            check_int_eq(complete_fired, 1);

            ruleforge_stateful_session_t stream_session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &stream_session), RULES_FORGE_OK);
            ruleforge_data_bind_stream_t stream = nullptr;
            check_int_eq(ruleforge_data_bind_stream_csv_path_create(
                             stream_session, schema_path.string().c_str(), "Customer",
                             csv_path, &stream), RULES_FORGE_OK);
            size_t split = std::strlen(csv) / 2;
            check_int_eq(ruleforge_data_bind_stream_feed(stream, csv, split), RULES_FORGE_OK);
            check_int_eq(ruleforge_data_bind_stream_feed(
                             stream, csv + split, std::strlen(csv) - split), RULES_FORGE_OK);
            ruleforge_fact_t* stream_facts = nullptr;
            int stream_count = 0;
            check_int_eq(ruleforge_data_bind_stream_finish(
                             stream, &stream_facts, &stream_count), RULES_FORGE_OK);
            check_int_eq(stream_count, complete_count);
            check_size_eq(ruleforge_session_get_fact_count(stream_session), 2);
            int stream_fired = 0;
            check_int_eq(ruleforge_session_fire_all_rules(
                             stream_session, -1, &stream_fired), RULES_FORGE_OK);
            check_int_eq(stream_fired, complete_fired);

            ruleforge_fact_array_free(stream_facts);
            ruleforge_fact_array_free(complete_facts);
            check_int_eq(ruleforge_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(stream_session), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(complete_session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("rejects finishing an invalid DataBind stream without inserting facts") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            auto schema_path = write_temp_schema(
                "rulesforge_capi_databind_invalid_stream.schema",
                "schema Market [id(33), version(1), byte_order(little)]; "
                "message Customer { int32 age; string name; }");
            std::string drl = std::string("import \"") + schema_path.generic_string() + R"(";
rule "AnyCustomer"
when
    $c : Customer()
then
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            ruleforge_data_bind_stream_t stream = nullptr;
            check_int_eq(ruleforge_data_bind_stream_json_create(
                             session, schema_path.string().c_str(), "Customer", &stream),
                         RULES_FORGE_OK);
            char const* invalid = R"({"name":"Alice","age":})";
            ruleforge_status_t feed_status =
                ruleforge_data_bind_stream_feed(stream, invalid, std::strlen(invalid));
            if (feed_status == RULES_FORGE_OK) {
                check_int_eq(ruleforge_data_bind_stream_finish(stream, nullptr, nullptr),
                             RULES_FORGE_ERROR_INVALID_ARGUMENT);
            } else {
                check_int_eq(feed_status, RULES_FORGE_ERROR_INVALID_ARGUMENT);
                check_int_eq(ruleforge_data_bind_stream_finish(stream, nullptr, nullptr),
                             RULES_FORGE_ERROR_INVALID_ARGUMENT);
            }
            check_size_eq(ruleforge_session_get_fact_count(session), 0);

            check_int_eq(ruleforge_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("adds schema-bound XML facts through TurboUtils DataBind") {
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

            ruleforge_stateful_session_t stream_session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &stream_session), RULES_FORGE_OK);
            ruleforge_data_bind_stream_t stream = nullptr;
            check_int_eq(ruleforge_data_bind_stream_xml_path_all_create(
                             stream_session, schema_path.string().c_str(), "Order",
                             "//order", &stream), RULES_FORGE_OK);
            size_t split = std::strlen(xml) / 2;
            check_int_eq(ruleforge_data_bind_stream_feed(stream, xml, split), RULES_FORGE_OK);
            check_int_eq(ruleforge_data_bind_stream_feed(
                             stream, xml + split, std::strlen(xml) - split), RULES_FORGE_OK);
            ruleforge_fact_t* stream_facts = nullptr;
            int stream_loaded = 0;
            check_int_eq(ruleforge_data_bind_stream_finish(
                             stream, &stream_facts, &stream_loaded), RULES_FORGE_OK);
            check_int_eq(stream_loaded, loaded);
            ruleforge_query_result_t stream_query = nullptr;
            check_int_eq(ruleforge_session_query(
                             stream_session, "FindOrder", &stream_query), RULES_FORGE_OK);
            check_int_eq(ruleforge_query_result_get_size(stream_query), 1);

            check_int_eq(ruleforge_query_result_destroy(stream_query), RULES_FORGE_OK);
            check_int_eq(ruleforge_query_result_destroy(query_result), RULES_FORGE_OK);
            ruleforge_fact_array_free(stream_facts);
            ruleforge_fact_array_free(facts);
            check_int_eq(ruleforge_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(stream_session), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("fires all rules") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            auto schema_path = write_temp_schema(
                "rulesforge_capi_fire_fact.schema",
                "schema FireFact [id(21), version(1), byte_order(little)]; "
                "message Fact { int64 id; }");
            std::string simple_drl = std::string(R"(
import schema ")") + schema_path.generic_string() + R"("

rule "AnyFactRule"
    when
        $f : Fact()
    then
        // No action, just for firing test
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, simple_drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            const char* fact_type = "Fact";
            const char* fact_json = R"({"id": 1})";
            check_int_eq(ruleforge_session_add_fact_json_schema(
                             session, schema_path.generic_string().c_str(), fact_type, fact_json,
                             nullptr),
                         RULES_FORGE_OK);

            int fired_count = 0;
            check_int_eq(ruleforge_session_fire_all_rules(session, -1, &fired_count), RULES_FORGE_OK);
            check_int_eq(fired_count, 1);

            check_int_eq(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("queries facts and accesses fields") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            auto schema_path = write_temp_schema(
                "rulesforge_capi_query_person.schema",
                "schema QueryPerson [id(23), version(1), byte_order(little)]; "
                "message Person { string name; int32 age; }");
            std::string query_drl = std::string(R"(
import schema ")") + schema_path.generic_string() + R"("

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
            check_int_eq(ruleforge_kb_load_drl(kb, query_drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            check_int_eq(ruleforge_session_add_fact_json_schema(
                             session, schema_path.generic_string().c_str(), "Person",
                             R"({"name": "Bob", "age": 25})", nullptr),
                         RULES_FORGE_OK);
            check_int_eq(ruleforge_session_add_fact_json_schema(
                             session, schema_path.generic_string().c_str(), "Person",
                             R"({"name": "Charlie", "age": 17})", nullptr),
                         RULES_FORGE_OK);
            check_int_eq(ruleforge_session_add_fact_json_schema(
                             session, schema_path.generic_string().c_str(), "Person",
                             R"({"name": "Diana", "age": 30})", nullptr),
                         RULES_FORGE_OK);

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
            std::filesystem::remove(schema_path);
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

            auto person_schema_path = write_temp_schema(
                "rulesforge_capi_rollback_person.schema",
                "schema RollbackPerson [id(24), version(1), byte_order(little)]; "
                "message Person { string name; int32 age; }");
            std::string failing_drl = std::string(R"(
import schema ")") + person_schema_path.generic_string() + R"("

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
            check_int_eq(ruleforge_kb_load_drl(kb, failing_drl.c_str()), RULES_FORGE_OK);

            session = nullptr;
            check_int_eq(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            check_int_eq(
                ruleforge_session_set_validation_mode(session, RULES_FORGE_VALIDATION_STRICT),
                RULES_FORGE_OK);
            check_int_eq(
                ruleforge_session_add_fact_json_schema(
                    session, person_schema_path.generic_string().c_str(), "Person",
                    R"({"name":"Alice","age":30})", nullptr),
                RULES_FORGE_OK);

            // First run fails in RHS and leaves session inconsistent due to rollback failure.
            check_int_ne(ruleforge_session_fire_all_rules(session, -1, nullptr), RULES_FORGE_OK);
            check_int_eq(ruleforge_session_get_fact_count(session), 0);

            check_int_eq(
                ruleforge_session_add_fact_json_schema(
                    session, person_schema_path.generic_string().c_str(), "Person",
                    R"({"name":"Bob","age":40})", nullptr),
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
            std::filesystem::remove(person_schema_path);
            ruleforge_cleanup();
        }
    }

    group("Continuous Session and DataBind") {
        it("pushes a schema-bound JSON event and exposes immutable outputs") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            auto schema_path = write_temp_schema(
                "rulesforge_capi_continuous.schema",
                "schema Continuous [id(41), version(1), byte_order(little)]; "
                "message Event { int32 value; }");
            std::string rfl = std::string("import \"") + schema_path.generic_string() + R"(";
declare Alert value: int end
rule "Emit Alert"
when
    Event() from entry-point "events"
then
    insert Alert { value = 7 }
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, rfl.c_str()), RULES_FORGE_OK);

            ruleforge_continuous_config_t config{};
            check_int_eq(ruleforge_continuous_config_init(&config), RULES_FORGE_OK);
            char const* output_types[] = {"Alert"};
            config.output_fact_types = output_types;
            config.output_fact_type_count = 1;
            config.event_retention_ms = 100;
            config.dedup_retention_ms = 200;

            ruleforge_continuous_session_t session = nullptr;
            check_int_eq(ruleforge_continuous_session_create(kb, &config, &session),
                         RULES_FORGE_OK);
            check_not_null(session);

            ruleforge_continuous_result_t result = nullptr;
            check_int_eq(ruleforge_continuous_push_json_schema(
                             session, schema_path.string().c_str(), "Event", "event-1",
                             "events", 100, R"({"value":7})", &result),
                         RULES_FORGE_OK);
            check_not_null(result);
            check_int_eq(ruleforge_continuous_result_get_status(result),
                         RULES_FORGE_CONTINUOUS_COMMITTED);
            check_int_eq(ruleforge_continuous_result_get_rules_fired(result), 1);
            check_int_eq(ruleforge_continuous_result_get_output_count(result), 1);
            ruleforge_fact_t output = nullptr;
            check_int_eq(ruleforge_continuous_result_get_output(result, 0, &output),
                         RULES_FORGE_OK);
            int64_t value = 0;
            check_int_eq(ruleforge_fact_get_field_as_int(output, "value", &value),
                         RULES_FORGE_OK);
            check_int_eq(static_cast<int>(value), 7);
            uint64_t batch_id = ruleforge_continuous_result_get_batch_id(result);
            check_int_eq(ruleforge_continuous_acknowledge(session, batch_id), RULES_FORGE_OK);
            check_int_eq(ruleforge_continuous_result_destroy(result), RULES_FORGE_OK);

            result = nullptr;
            check_int_eq(ruleforge_continuous_push_yaml_schema(
                             session, schema_path.string().c_str(), "Event", "event-2",
                             "events", 101, "value: 8\n", &result),
                         RULES_FORGE_OK);
            check_int_eq(ruleforge_continuous_result_get_rules_fired(result), 1);
            check_int_eq(ruleforge_continuous_acknowledge(
                             session, ruleforge_continuous_result_get_batch_id(result)),
                         RULES_FORGE_OK);
            check_int_eq(ruleforge_continuous_result_destroy(result), RULES_FORGE_OK);

            result = nullptr;
            check_int_eq(ruleforge_continuous_advance_watermark(session, 201, &result),
                         RULES_FORGE_OK);
            int64_t watermark = 0;
            int has_watermark = 0;
            check_int_eq(ruleforge_continuous_result_get_watermark(
                             result, &watermark, &has_watermark), RULES_FORGE_OK);
            check_int_eq(has_watermark, 1);
            check_int_eq(static_cast<int>(watermark), 201);
            check_int_eq(ruleforge_continuous_acknowledge(
                             session, ruleforge_continuous_result_get_batch_id(result)),
                         RULES_FORGE_OK);
            check_int_eq(ruleforge_continuous_result_destroy(result), RULES_FORGE_OK);

            ruleforge_continuous_metrics_t metrics{};
            check_int_eq(ruleforge_continuous_get_metrics(session, &metrics), RULES_FORGE_OK);
            check_size_eq(metrics.accepted_events, 2);
            check_size_eq(metrics.active_events, 0);

            check_int_eq(ruleforge_continuous_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("commits an asynchronous DataBind event only when the stream finishes") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            auto schema_path = write_temp_schema(
                "rulesforge_capi_continuous_stream.schema",
                "schema ContinuousStream [id(42), version(1), byte_order(little)]; "
                "message Event { int32 value; }");
            std::string rfl = std::string("import \"") + schema_path.generic_string() + R"(";
rule "Observe"
when
    Event() from entry-point "events"
then
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, rfl.c_str()), RULES_FORGE_OK);
            ruleforge_continuous_config_t config{};
            check_int_eq(ruleforge_continuous_config_init(&config), RULES_FORGE_OK);
            ruleforge_continuous_session_t session = nullptr;
            check_int_eq(ruleforge_continuous_session_create(kb, &config, &session),
                         RULES_FORGE_OK);

            ruleforge_continuous_data_bind_stream_t stream = nullptr;
            check_int_eq(ruleforge_continuous_data_bind_stream_json_create(
                             session, schema_path.string().c_str(), "Event", "stream-1",
                             "events", 300, &stream), RULES_FORGE_OK);
            check_not_null(stream);
            check_int_eq(ruleforge_continuous_session_destroy(session),
                         RULES_FORGE_ERROR_INVALID_ARGUMENT);

            char const* first = R"({"val)";
            char const* second = R"(ue":9})";
            check_int_eq(ruleforge_continuous_data_bind_stream_feed(
                             stream, first, std::strlen(first)), RULES_FORGE_OK);
            check_int_eq(ruleforge_continuous_data_bind_stream_feed(
                             stream, second, std::strlen(second)), RULES_FORGE_OK);
            ruleforge_continuous_metrics_t metrics{};
            check_int_eq(ruleforge_continuous_get_metrics(session, &metrics), RULES_FORGE_OK);
            check_size_eq(metrics.accepted_events, 0);

            ruleforge_continuous_result_t result = nullptr;
            check_int_eq(ruleforge_continuous_data_bind_stream_finish(stream, &result),
                         RULES_FORGE_OK);
            check_not_null(result);
            check_int_eq(ruleforge_continuous_result_get_rules_fired(result), 1);
            ruleforge_continuous_result_t duplicate_result = result;
            check_int_eq(ruleforge_continuous_data_bind_stream_finish(stream, &duplicate_result),
                         RULES_FORGE_ERROR_INVALID_ARGUMENT);
            check_null(duplicate_result);
            check_int_eq(ruleforge_continuous_get_metrics(session, &metrics), RULES_FORGE_OK);
            check_size_eq(metrics.accepted_events, 1);

            check_int_eq(ruleforge_continuous_result_destroy(result), RULES_FORGE_OK);
            check_int_eq(ruleforge_continuous_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_int_eq(ruleforge_continuous_session_destroy(session), RULES_FORGE_OK);
            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("commits path-selected JSON YAML CSV and XML batches from documents and streams") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_int_eq(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            auto schema_path = write_temp_schema(
                "rulesforge_capi_continuous_path_batch.schema",
                "schema Events [id(36), version(1), byte_order(little)]; "
                "message Event { string event_id; int64 event_time; int32 value; string region; }");
            std::string rfl = std::string("import \"") + schema_path.generic_string() + R"(";
rule "Selected event" when
    Event(value >= 7) from entry-point "events"
then
end
)";
            check_int_eq(ruleforge_kb_load_drl(kb, rfl.c_str()), RULES_FORGE_OK);

            auto create_session = [&]() {
                ruleforge_continuous_config_t config{};
                check_int_eq(ruleforge_continuous_config_init(&config), RULES_FORGE_OK);
                ruleforge_continuous_session_t session = nullptr;
                check_int_eq(ruleforge_continuous_session_create(kb, &config, &session),
                             RULES_FORGE_OK);
                return session;
            };
            auto verify_batch = [&](ruleforge_continuous_session_t session,
                                    ruleforge_continuous_result_t result) {
                check_not_null(result);
                check_int_eq(ruleforge_continuous_result_get_rules_fired(result), 2);
                ruleforge_continuous_metrics_t metrics{};
                check_int_eq(ruleforge_continuous_get_metrics(session, &metrics), RULES_FORGE_OK);
                check_size_eq(metrics.accepted_events, 2);
                check_int_eq(ruleforge_continuous_acknowledge(
                                 session, ruleforge_continuous_result_get_batch_id(result)),
                             RULES_FORGE_OK);
                check_int_eq(ruleforge_continuous_result_destroy(result), RULES_FORGE_OK);
            };

            char const* json =
                R"({"events":[{"event_id":"json-1","event_time":100,"value":7,"region":"west"},)"
                R"({"event_id":"json-2","event_time":101,"value":9,"region":"west"}],)"
                R"("ignored":[{"event_id":"json-3","event_time":102,"value":11,"region":"east"}]})";
            char const* json_path = "$.events[*]";
            auto json_complete = create_session();
            ruleforge_continuous_result_t result = nullptr;
            check_int_eq(ruleforge_continuous_push_json_path_schema(
                             json_complete, schema_path.string().c_str(), "Event", json,
                             json_path, "event_id", "event_time", "events", &result),
                         RULES_FORGE_OK);
            verify_batch(json_complete, result);
            check_int_eq(ruleforge_continuous_session_destroy(json_complete), RULES_FORGE_OK);

            auto json_stream_session = create_session();
            ruleforge_continuous_data_bind_stream_t stream = nullptr;
            check_int_eq(ruleforge_continuous_data_bind_stream_json_path_create(
                             json_stream_session, schema_path.string().c_str(), "Event",
                             json_path, "event_id", "event_time", "events", &stream),
                         RULES_FORGE_OK);
            size_t json_split = std::strlen(json) / 2;
            check_int_eq(ruleforge_continuous_data_bind_stream_feed(
                             stream, json, json_split), RULES_FORGE_OK);
            ruleforge_continuous_metrics_t pending_metrics{};
            check_int_eq(ruleforge_continuous_get_metrics(
                             json_stream_session, &pending_metrics), RULES_FORGE_OK);
            check_size_eq(pending_metrics.accepted_events, 0);
            check_int_eq(ruleforge_continuous_data_bind_stream_feed(
                             stream, json + json_split, std::strlen(json) - json_split),
                         RULES_FORGE_OK);
            result = nullptr;
            check_int_eq(ruleforge_continuous_data_bind_stream_finish(stream, &result),
                         RULES_FORGE_OK);
            verify_batch(json_stream_session, result);
            check_int_eq(ruleforge_continuous_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_int_eq(ruleforge_continuous_session_destroy(json_stream_session),
                         RULES_FORGE_OK);

            char const* yaml =
                "events:\n"
                "  - event_id: yaml-1\n"
                "    event_time: 150\n"
                "    value: 7\n"
                "    region: west\n"
                "  - event_id: yaml-2\n"
                "    event_time: 151\n"
                "    value: 9\n"
                "    region: west\n"
                "ignored:\n"
                "  - event_id: yaml-3\n"
                "    event_time: 152\n"
                "    value: 11\n"
                "    region: east\n";
            char const* yaml_path = "/events/*";
            auto yaml_complete = create_session();
            result = nullptr;
            check_int_eq(ruleforge_continuous_push_yaml_path_schema(
                             yaml_complete, schema_path.string().c_str(), "Event", yaml,
                             yaml_path, "event_id", "event_time", "events", &result),
                         RULES_FORGE_OK);
            verify_batch(yaml_complete, result);
            check_int_eq(ruleforge_continuous_session_destroy(yaml_complete), RULES_FORGE_OK);

            auto yaml_stream_session = create_session();
            stream = nullptr;
            check_int_eq(ruleforge_continuous_data_bind_stream_yaml_path_create(
                             yaml_stream_session, schema_path.string().c_str(), "Event",
                             yaml_path, "event_id", "event_time", "events", &stream),
                         RULES_FORGE_OK);
            size_t yaml_split = std::strlen(yaml) / 2;
            check_int_eq(ruleforge_continuous_data_bind_stream_feed(
                             stream, yaml, yaml_split), RULES_FORGE_OK);
            check_int_eq(ruleforge_continuous_data_bind_stream_feed(
                             stream, yaml + yaml_split, std::strlen(yaml) - yaml_split),
                         RULES_FORGE_OK);
            result = nullptr;
            check_int_eq(ruleforge_continuous_data_bind_stream_finish(stream, &result),
                         RULES_FORGE_OK);
            verify_batch(yaml_stream_session, result);
            check_int_eq(ruleforge_continuous_data_bind_stream_destroy(stream),
                         RULES_FORGE_OK);
            check_int_eq(ruleforge_continuous_session_destroy(yaml_stream_session),
                         RULES_FORGE_OK);

            char const* csv =
                "event_id_s,event_time_n,value_n,region_s\n"
                "csv-1,200,7,west\ncsv-2,201,9,west\ncsv-3,202,11,east\n";
            char const* csv_path = "region == \"west\"";
            auto csv_complete = create_session();
            result = nullptr;
            check_int_eq(ruleforge_continuous_push_csv_path_schema(
                             csv_complete, schema_path.string().c_str(), "Event", csv,
                             csv_path, "event_id", "event_time", "events", &result),
                         RULES_FORGE_OK);
            verify_batch(csv_complete, result);
            check_int_eq(ruleforge_continuous_session_destroy(csv_complete), RULES_FORGE_OK);

            auto csv_stream_session = create_session();
            stream = nullptr;
            check_int_eq(ruleforge_continuous_data_bind_stream_csv_path_create(
                             csv_stream_session, schema_path.string().c_str(), "Event",
                             csv_path, "event_id", "event_time", "events", &stream),
                         RULES_FORGE_OK);
            size_t csv_split = std::strlen(csv) / 2;
            check_int_eq(ruleforge_continuous_data_bind_stream_feed(stream, csv, csv_split),
                         RULES_FORGE_OK);
            check_int_eq(ruleforge_continuous_get_metrics(
                             csv_stream_session, &pending_metrics), RULES_FORGE_OK);
            check_size_eq(pending_metrics.accepted_events, 0);
            check_int_eq(ruleforge_continuous_data_bind_stream_feed(
                             stream, csv + csv_split, std::strlen(csv) - csv_split),
                         RULES_FORGE_OK);
            result = nullptr;
            check_int_eq(ruleforge_continuous_data_bind_stream_finish(stream, &result),
                         RULES_FORGE_OK);
            verify_batch(csv_stream_session, result);
            check_int_eq(ruleforge_continuous_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_int_eq(ruleforge_continuous_session_destroy(csv_stream_session),
                         RULES_FORGE_OK);

            char const* xml =
                "<root><events>"
                "<event><event_id>xml-1</event_id><event_time>300</event_time><value>7</value><region>west</region></event>"
                "<event><event_id>xml-2</event_id><event_time>301</event_time><value>9</value><region>west</region></event>"
                "</events><ignored><event><event_id>xml-3</event_id><event_time>302</event_time><value>11</value><region>east</region></event></ignored></root>";
            char const* xml_path = "/root/events/event";
            auto xml_complete = create_session();
            result = nullptr;
            check_int_eq(ruleforge_continuous_push_xml_path_schema(
                             xml_complete, schema_path.string().c_str(), "Event", xml,
                             xml_path, "event_id", "event_time", "events", &result),
                         RULES_FORGE_OK);
            verify_batch(xml_complete, result);
            check_int_eq(ruleforge_continuous_session_destroy(xml_complete), RULES_FORGE_OK);

            auto xml_stream_session = create_session();
            stream = nullptr;
            check_int_eq(ruleforge_continuous_data_bind_stream_xml_path_create(
                             xml_stream_session, schema_path.string().c_str(), "Event",
                             xml_path, "event_id", "event_time", "events", &stream),
                         RULES_FORGE_OK);
            size_t xml_split = std::strlen(xml) / 2;
            check_int_eq(ruleforge_continuous_data_bind_stream_feed(stream, xml, xml_split),
                         RULES_FORGE_OK);
            check_int_eq(ruleforge_continuous_get_metrics(
                             xml_stream_session, &pending_metrics), RULES_FORGE_OK);
            check_size_eq(pending_metrics.accepted_events, 0);
            check_int_eq(ruleforge_continuous_data_bind_stream_feed(
                             stream, xml + xml_split, std::strlen(xml) - xml_split),
                         RULES_FORGE_OK);
            result = nullptr;
            check_int_eq(ruleforge_continuous_data_bind_stream_finish(stream, &result),
                         RULES_FORGE_OK);
            verify_batch(xml_stream_session, result);
            check_int_eq(ruleforge_continuous_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_int_eq(ruleforge_continuous_session_destroy(xml_stream_session),
                         RULES_FORGE_OK);

            auto invalid_metadata_session = create_session();
            stream = nullptr;
            check_int_eq(ruleforge_continuous_data_bind_stream_json_path_create(
                             invalid_metadata_session, schema_path.string().c_str(), "Event",
                             "$[*]", "event_id", "event_time", "events", &stream),
                         RULES_FORGE_OK);
            char const* invalid_metadata_json =
                R"([{"event_id":"","event_time":400,"value":7,"region":"west"}])";
            check_int_eq(ruleforge_continuous_data_bind_stream_feed(
                             stream, invalid_metadata_json, std::strlen(invalid_metadata_json)),
                         RULES_FORGE_ERROR_INVALID_ARGUMENT);
            ruleforge_continuous_metrics_t failed_metrics{};
            check_int_eq(ruleforge_continuous_get_metrics(
                             invalid_metadata_session, &failed_metrics), RULES_FORGE_OK);
            check_size_eq(failed_metrics.accepted_events, 0);
            result = reinterpret_cast<ruleforge_continuous_result_t>(stream);
            check_int_eq(ruleforge_continuous_data_bind_stream_finish(stream, &result),
                         RULES_FORGE_ERROR_INVALID_ARGUMENT);
            check_null(result);
            check_int_eq(ruleforge_continuous_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_int_eq(ruleforge_continuous_session_destroy(invalid_metadata_session),
                         RULES_FORGE_OK);

            check_int_eq(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }
    }
}
