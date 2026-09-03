#include "tinytest.hpp"
#include "rules_forge.h"
#include "data_bind.h"
#include "core/fact.hpp"

#include <cmath>
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

int append_serialized_bytes(void const* data, size_t len, void* user) {
    if (!data || !user) {
        return -1;
    }
    static_cast<std::string*>(user)->append(static_cast<char const*>(data), len);
    return 0;
}

int reject_serialized_bytes(void const*, size_t, void*) {
    return -1;
}
}

suite("CAPI") {
    group("Initialization and Cleanup") {
        it("initializes and cleans up") {
            check_greater_equal(DATA_BIND_VERSION, 20501);
            check_equal(DATA_BIND_ABI_VERSION, 8);
            check_equal(data_bind_library_version(), DATA_BIND_VERSION);
            check_equal(data_bind_abi_version(), DATA_BIND_ABI_VERSION);
            check_not_null(data_bind_version_string());
            check_equal(ruleforge_init(), RULES_FORGE_OK);
            check_equal(ruleforge_cleanup(), RULES_FORGE_OK);
        }

        it("deep clones all DataBind value storage independently") {
            char const* schema =
                "composite Header { uint32 seq; uint64 ts; } "
                "message CloneFact { Header header; list<uint32> values; "
                "set<string> tags; map<string,int32> attrs; bytes raw; uuid id; "
                "datetime at; date d; time t; duration span; decimal price; "
                "bigint count; money total; }";
            char const* json =
                R"({"header":{"seq":7,"ts":99},"values":[3,4],)"
                R"("tags":["alpha","beta"],"attrs":{"x":30,"y":40},"raw":"Az",)"
                R"("id":"01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001",)"
                R"("at":"Sat, 04 Mar 2006 13:27:54 GMT","d":"2026-06-28",)"
                R"("t":"09:30:05.123","span":"1h30m5s250ms","price":"123.4500",)"
                R"("count":"000123456789012345678901234567890",)"
                R"("total":{"amount":"99.9900","currency":"EUR"}})";
            DataBind* codec = nullptr;
            DataBindValue* source = nullptr;
            DataBindValue* copy = nullptr;
            DataBindError error = DATA_BIND_ERROR_INIT;

            check_equal(data_bind_create_from_text(
                             schema, std::strlen(schema), &codec, &error), DATA_BIND_OK);
            check_not_null(codec);
            if (codec) {
                check_equal(data_bind_parse_json(
                                 codec, "CloneFact", json, std::strlen(json),
                                 &source, &error), DATA_BIND_OK);
                check_not_null(source);
            }
            if (source) {
                auto const* source_header = data_bind_value_get(source, "header");
                auto const* source_values = data_bind_value_get(source, "values");
                auto const* source_tags = data_bind_value_get(source, "tags");
                auto const* source_attrs = data_bind_value_get(source, "attrs");
                size_t source_bytes_len = 0;
                auto const* source_bytes = data_bind_value_as_bytes(
                    data_bind_value_get(source, "raw"), &source_bytes_len);
                char const* source_bigint = data_bind_value_as_bigint_string(
                    data_bind_value_get(source, "count"));

                check_equal(data_bind_value_clone(source, &copy), DATA_BIND_OK);
                check_not_null(copy);
                if (copy) {
                    check_not_equal(static_cast<void const*>(copy),
                                    static_cast<void const*>(source));
                    check_not_equal(
                        static_cast<void const*>(data_bind_value_get(copy, "header")),
                        static_cast<void const*>(source_header));
                    check_not_equal(
                        static_cast<void const*>(data_bind_value_get(copy, "values")),
                        static_cast<void const*>(source_values));
                    check_not_equal(
                        static_cast<void const*>(data_bind_value_get(copy, "tags")),
                        static_cast<void const*>(source_tags));
                    check_not_equal(
                        static_cast<void const*>(data_bind_value_get(copy, "attrs")),
                        static_cast<void const*>(source_attrs));
                    size_t copy_bytes_len = 0;
                    auto const* copy_bytes = data_bind_value_as_bytes(
                        data_bind_value_get(copy, "raw"), &copy_bytes_len);
                    check_equal(copy_bytes_len, source_bytes_len);
                    check_not_equal(static_cast<void const*>(copy_bytes),
                                    static_cast<void const*>(source_bytes));
                    check_not_equal(
                        static_cast<void const*>(data_bind_value_as_bigint_string(
                            data_bind_value_get(copy, "count"))),
                        static_cast<void const*>(source_bigint));
                }
            }

            data_bind_value_free(source);
            source = nullptr;
            if (copy) {
                auto const* header = data_bind_value_get(copy, "header");
                check_equal(data_bind_value_as_int(data_bind_value_get(header, "seq")), 7);

                auto const* values = data_bind_value_get(copy, "values");
                check(data_bind_value_kind(values) == DATA_BIND_VALUE_LIST);
                check_equal(data_bind_value_count(values), 2);
                check_equal(data_bind_value_as_int(data_bind_value_at(values, 1)), 4);

                auto const* tags = data_bind_value_get(copy, "tags");
                check(data_bind_value_kind(tags) == DATA_BIND_VALUE_SET);
                check_equal(data_bind_value_count(tags), 2);
                check_equal(data_bind_value_as_string(data_bind_value_at(tags, 1)), "beta");

                auto const* attrs = data_bind_value_get(copy, "attrs");
                check(data_bind_value_kind(attrs) == DATA_BIND_VALUE_MAP);
                check_equal(data_bind_value_count(attrs), 2);
                DataBindMapEntry first_attr = data_bind_value_map_entry_at(attrs, 0);
                check_equal(first_attr.key, "x");
                check_equal(data_bind_value_as_int(first_attr.value), 30);

                size_t bytes_len = 0;
                auto const* bytes = data_bind_value_as_bytes(
                    data_bind_value_get(copy, "raw"), &bytes_len);
                check_equal(bytes_len, 2);
                check_equal(bytes, "Az", 2);

                char text[64] = {0};
                check_equal(data_bind_value_as_uuid_string(
                                 data_bind_value_get(copy, "id"), text, sizeof(text)),
                             "01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001");
                check_within(data_bind_value_as_datetime_timestamp(
                                    data_bind_value_get(copy, "at")),
                                1141478874.0, 0.001);

                DataBindDate date{};
                check_equal(data_bind_value_get_date(
                                 data_bind_value_get(copy, "d"), &date), DATA_BIND_OK);
                check_equal(date.year, 2026);
                DataBindTime time{};
                check_equal(data_bind_value_get_time(
                                 data_bind_value_get(copy, "t"), &time), DATA_BIND_OK);
                check_equal(time.millisecond, 123);
                check_equal(static_cast<int>(data_bind_value_as_duration_milliseconds(
                                 data_bind_value_get(copy, "span"))), 5405250);

                DataBindDecimal decimal{};
                check_equal(data_bind_value_get_decimal(
                                 data_bind_value_get(copy, "price"), &decimal), DATA_BIND_OK);
                check_equal(static_cast<int>(decimal.mantissa), 12345);
                check_equal(decimal.scale, 2);
                check_equal(data_bind_value_as_bigint_string(
                                 data_bind_value_get(copy, "count")),
                             "123456789012345678901234567890");
                DataBindMoney money{};
                check_equal(data_bind_value_get_money(
                                 data_bind_value_get(copy, "total"), &money), DATA_BIND_OK);
                check_equal(money.currency, "EUR");
                check_equal(static_cast<int>(money.amount.mantissa), 9999);
            }
            data_bind_value_free(copy);
            data_bind_free(codec);
        }
    }

    group("Version Information") {
        it("returns version string") {
            const char* version = ruleforge_get_version();
            check_not_null(version);
            check_equal(version, RULEFORGE_VERSION_STRING);
        }
    }

    group("Error Handling") {
        it("reports errors correctly") {
            ruleforge_init();
            ruleforge_status_t status = ruleforge_kb_create(nullptr);
            check_not_equal(status, RULES_FORGE_OK);
            check_contains(ruleforge_get_last_error_message(), "NULL");
            ruleforge_cleanup();
        }
    }

    group("DataBindObject") {
        it("owns clones serializes and writes a schema-bound object") {
            ruleforge_init();
            auto schema_path = write_temp_schema(
                "rulesforge_capi_databind_object.schema",
                "schema ObjectApi [id(51), version(1), byte_order(little)]; "
                "message Item { "
                "[name(\"item-id\"), alias(\"legacy-id\")] uint32 id; "
                "string name; }");
            char const* json = R"({"legacy-id":7,"name":"alpha"})";
            ruleforge_data_bind_object_t object = nullptr;
            check_equal(ruleforge_data_bind_object_from_json(
                             schema_path.string().c_str(), "Item", json,
                             std::strlen(json), &object),
                         RULES_FORGE_OK);
            check_not_null(object);
            check_equal(ruleforge_data_bind_object_get_type_name(object), "Item");

            ruleforge_data_bind_object_t clone = nullptr;
            check_equal(ruleforge_data_bind_object_clone(object, &clone), RULES_FORGE_OK);
            check_not_null(clone);
            check_equal(ruleforge_data_bind_object_destroy(object), RULES_FORGE_OK);
            object = nullptr;
            std::filesystem::remove(schema_path);

            char* serialized = nullptr;
            size_t serialized_len = 0;
            check_equal(ruleforge_data_bind_object_serialize_json(
                             clone, &serialized, &serialized_len), RULES_FORGE_OK);
            check_not_null(serialized);
            check_equal(serialized_len, std::strlen(serialized));
            check_contains(serialized, "\"item-id\":7");
            check(std::strstr(serialized, "\"legacy-id\"") == nullptr);
            check(std::strstr(serialized, "\"id\":") == nullptr);
            ruleforge_data_bind_serialized_free(serialized);

            serialized = nullptr;
            check_equal(ruleforge_data_bind_object_serialize_yaml(
                             clone, &serialized, nullptr), RULES_FORGE_OK);
            check_contains(serialized, "alpha");
            ruleforge_data_bind_serialized_free(serialized);

            serialized = nullptr;
            check_equal(ruleforge_data_bind_object_serialize_xml(
                             clone, &serialized, nullptr), RULES_FORGE_OK);
            check_contains(serialized, "<item-id>7</item-id>");
            ruleforge_data_bind_serialized_free(serialized);

            uint8_t* binary = nullptr;
            size_t binary_len = 0;
            check_equal(ruleforge_data_bind_object_serialize_binary(
                             clone, &binary, &binary_len), RULES_FORGE_OK);
            check_not_null(binary);
            check_greater(binary_len, 0);
            ruleforge_data_bind_binary_free(binary);

            std::string sink_output;
            check_equal(ruleforge_data_bind_object_write_json(
                             clone, append_serialized_bytes, &sink_output), RULES_FORGE_OK);
            check_contains(sink_output.c_str(), "\"name\":\"alpha\"");
            sink_output.clear();
            check_equal(ruleforge_data_bind_object_write_yaml(
                             clone, append_serialized_bytes, &sink_output), RULES_FORGE_OK);
            check_contains(sink_output.c_str(), "alpha");
            sink_output.clear();
            check_equal(ruleforge_data_bind_object_write_xml(
                             clone, append_serialized_bytes, &sink_output), RULES_FORGE_OK);
            check_contains(sink_output.c_str(), "<name>alpha</name>");
            check_equal(ruleforge_data_bind_object_write_json(
                             clone, reject_serialized_bytes, nullptr),
                         RULES_FORGE_ERROR_GENERIC);
            check_contains(ruleforge_get_last_error_message(), "writer");

            check_equal(ruleforge_data_bind_object_destroy(clone), RULES_FORGE_OK);
            check_equal(ruleforge_data_bind_object_destroy(nullptr),
                          RULES_FORGE_ERROR_INVALID_ARGUMENT);
            ruleforge_cleanup();
        }

        it("constructs binary YAML XML and CSV objects") {
            ruleforge_init();
            auto schema_path = write_temp_schema(
                "rulesforge_capi_databind_object_formats.schema",
                "schema ObjectFormats [id(52), version(1), byte_order(little)]; "
                "message Item { uint32 id; }");
            uint8_t binary[4] = {0};
            write_u32_le(binary, 0, 1);
            char const* yaml = "id: 2\n";
            char const* xml = "<Item><id>3</id></Item>";
            char const* csv = "id\n4\n";

            ruleforge_data_bind_object_t objects[4] = {};
            check_equal(ruleforge_data_bind_object_from_binary(
                             schema_path.string().c_str(), "Item", binary,
                             sizeof(binary), &objects[0]), RULES_FORGE_OK);
            check_equal(ruleforge_data_bind_object_from_yaml(
                             schema_path.string().c_str(), "Item", yaml,
                             std::strlen(yaml), &objects[1]), RULES_FORGE_OK);
            check_equal(ruleforge_data_bind_object_from_xml(
                             schema_path.string().c_str(), "Item", xml,
                             std::strlen(xml), &objects[2]), RULES_FORGE_OK);
            check_equal(ruleforge_data_bind_object_from_csv(
                             schema_path.string().c_str(), "Item", csv,
                             std::strlen(csv), 0, &objects[3]), RULES_FORGE_OK);

            for (int i = 0; i < 4; ++i) {
                check_not_null(objects[i]);
                char* json = nullptr;
                check_equal(ruleforge_data_bind_object_serialize_json(
                                 objects[i], &json, nullptr), RULES_FORGE_OK);
                std::string expected = "\"id\":" + std::to_string(i + 1);
                check_contains(json, expected.c_str());
                ruleforge_data_bind_serialized_free(json);

                char* csv_output = nullptr;
                check_equal(ruleforge_data_bind_object_serialize_csv(
                                 objects[i], &csv_output, nullptr), RULES_FORGE_OK);
                check_contains(csv_output, "id");
                ruleforge_data_bind_serialized_free(csv_output);

                uint8_t* binary_output = nullptr;
                size_t binary_len = 0;
                check_equal(ruleforge_data_bind_object_serialize_binary(
                                 objects[i], &binary_output, &binary_len), RULES_FORGE_OK);
                check_equal(binary_len, 4);
                check_not_null(binary_output);
                ruleforge_data_bind_binary_free(binary_output);
                check_equal(ruleforge_data_bind_object_destroy(objects[i]), RULES_FORGE_OK);
            }
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }
    }

    group("Knowledge Base Management") {
        it("creates and destroys knowledge base") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;

            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            check_not_null(kb);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);

            ruleforge_cleanup();
        }

        it("reuses imported schemas and exposes enum names") {
            ruleforge_init();
            auto schema_path = write_temp_schema(
                "rulesforge_capi_kb_schema_registry.schema",
                "schema Registry [id(53), version(1), byte_order(little)]; "
                "enum Side <uint8> { Buy = 1; Sell = 2; } "
                "message Order { "
                "[name(\"order-id\"), alias(\"legacy-id\")] uint32 id; "
                "Side side; }");
            std::string rfl = std::string("import schema \"")
                + schema_path.generic_string()
                + "\"\nquery \"Orders\"\n$o : Order()\nend\n";

            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            check_equal(ruleforge_kb_load_drl(kb, rfl.c_str()), RULES_FORGE_OK);

            int exists = 0;
            check_equal(ruleforge_kb_has_schema_type(kb, "Order", &exists), RULES_FORGE_OK);
            check_equal(exists, 1);
            check_equal(ruleforge_kb_has_schema_type(kb, "Side", &exists), RULES_FORGE_OK);
            check_equal(exists, 1);
            check_equal(ruleforge_kb_has_schema_type(kb, "Missing", &exists), RULES_FORGE_OK);
            check_equal(exists, 0);

            ruleforge_stateful_session_t session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            ruleforge_fact_t fact = nullptr;
            check_equal(ruleforge_session_add_fact_json(
                             session, "Order", R"({"legacy-id":7,"side":"Sell"})", &fact),
                         RULES_FORGE_OK);
            check_not_null(fact);

            auto const* internal_fact = reinterpret_cast<Fact const*>(fact);
            int64_t order_id = 0;
            check_equal(ruleforge_fact_get_field_as_int(
                             fact, "id", &order_id), RULES_FORGE_OK);
            check_equal(order_id, 7);
            check(internal_fact->fields.find("order-id")
                  == internal_fact->fields.end());
            check(internal_fact->fields.find("legacy-id")
                  == internal_fact->fields.end());
            auto side_field = internal_fact->fields.find("side");
            auto const* runtime_enum = side_field != internal_fact->fields.end()
                ? std::get_if<EnumValue>(&side_field->second) : nullptr;
            check_not_null(runtime_enum);
            check(runtime_enum != nullptr && runtime_enum->type_name == "Side");
            check(runtime_enum != nullptr && runtime_enum->item_name == "Sell");

            char enum_name[16] = {};
            size_t enum_name_len = 0;
            int64_t enum_value = 0;
            check_equal(ruleforge_fact_get_field_as_enum(
                             fact, "side", enum_name, sizeof(enum_name),
                             &enum_name_len, &enum_value), RULES_FORGE_OK);
            check_equal(enum_name, "Sell");
            check_equal(enum_name_len, 4);
            check_equal(enum_value, 2);

            enum_name[0] = '\0';
            check_equal(ruleforge_fact_get_enum_name(
                             fact, "side", enum_name, sizeof(enum_name), &enum_name_len),
                         RULES_FORGE_OK);
            check_equal(enum_name, "Sell");

            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("loads RFL") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);

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
            check_equal(ruleforge_kb_load_drl(kb, simple_drl), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }

        it("loads decision table CSV") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            const char* simple_csv = R"(
Rule Name,CONDITION value,ACTION result
Rule1,hello,world
Rule2,test,passed
)";
            check_equal(ruleforge_kb_load_decision_table_csv(kb, simple_csv), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }

        it("keeps previous knowledge base when a new load fails") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);

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
            check_equal(ruleforge_kb_load_drl(kb, valid_drl), RULES_FORGE_OK);

            const char* invalid_drl = R"(
rule "Broken"
    when
        $f : MissingParen(
    then
end
)";
            check_equal(ruleforge_kb_load_drl(kb, invalid_drl), RULES_FORGE_ERROR_COMPILATION_FAILED);

            // Should still be able to create session from the previously loaded valid KB.
            ruleforge_stateful_session_t session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);
            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);

            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();
        }
    }

    group("Stateful Session and Fact Management") {
        it("adds an independently owned DataBindObject without consuming it") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            auto schema_path = write_temp_schema(
                "rulesforge_capi_databind_object_session.schema",
                "schema ObjectSession [id(53), version(1), byte_order(little)]; "
                "message ObjectFact { int32 value; }");
            std::string rfl = std::string("import \"")
                + schema_path.generic_string()
                + R"(";
query "FindObjectFact"
    $fact : ObjectFact(value == 17)
end
)";
            check_equal(ruleforge_kb_load_drl(kb, rfl.c_str()), RULES_FORGE_OK);

            char const* json = R"({"value":17})";
            ruleforge_data_bind_object_t object = nullptr;
            check_equal(ruleforge_data_bind_object_from_json(
                             schema_path.string().c_str(), "ObjectFact", json,
                             std::strlen(json), &object), RULES_FORGE_OK);
            ruleforge_stateful_session_t session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            ruleforge_fact_t fact = nullptr;
            check_equal(ruleforge_session_add_data_bind_object(
                             session, object, &fact), RULES_FORGE_OK);
            check_not_null(fact);

            char* serialized = nullptr;
            check_equal(ruleforge_data_bind_object_serialize_json(
                             object, &serialized, nullptr), RULES_FORGE_OK);
            check_contains(serialized, "\"value\":17");
            ruleforge_data_bind_serialized_free(serialized);
            check_equal(ruleforge_data_bind_object_destroy(object), RULES_FORGE_OK);

            int64_t value = 0;
            check_equal(ruleforge_fact_get_field_as_int(fact, "value", &value),
                         RULES_FORGE_OK);
            check_equal(static_cast<int>(value), 17);
            ruleforge_query_result_t result = nullptr;
            check_equal(ruleforge_session_query(
                             session, "FindObjectFact", &result), RULES_FORGE_OK);
            check_equal(ruleforge_query_result_get_size(result), 1);

            check_equal(ruleforge_query_result_destroy(result), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("adds schema-bound JSON fact through Salts DataBind") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);

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
            check_equal(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            ruleforge_fact_t fact = nullptr;
            check_equal(
                ruleforge_session_add_fact_json(
                    session,
                    "Customer",
                    R"({"name":"Alice","age":30,"score":98.5})",
                    &fact),
                RULES_FORGE_OK);
            check_not_null(fact);

            int64_t age = 0;
            check_equal(ruleforge_fact_get_field_as_int(fact, "age", &age), RULES_FORGE_OK);
            check_equal((int)age, 30);

            double score = 0.0;
            check_equal(ruleforge_fact_get_field_as_double(fact, "score", &score), RULES_FORGE_OK);
            check(std::abs(score - 98.5) < 1e-12);

            ruleforge_query_result_t query_result = nullptr;
            check_equal(ruleforge_session_query(session, "FindCustomer", &query_result), RULES_FORGE_OK);
            check_not_null(query_result);
            check_equal(ruleforge_query_result_get_size(query_result), 1);

            check_equal(ruleforge_query_result_destroy(query_result), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("adds a schema-bound JSON fact from asynchronous chunks") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            auto schema_path = write_temp_schema(
                "rulesforge_capi_databind_json_stream.schema",
                "schema Market [id(31), version(1), byte_order(little)]; "
                "message Customer { int32 age; double score; string name; }");
            std::string drl = std::string("import \"") + schema_path.generic_string() + R"(";
query "FindStreamCustomer"
    $c : Customer(age == 30, name == "Alice")
end
)";
            check_equal(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            ruleforge_data_bind_stream_t stream = nullptr;
            check_equal(ruleforge_data_bind_stream_json_create(
                             session, "Customer", &stream),
                         RULES_FORGE_OK);
            check_not_null(stream);
            check_equal(ruleforge_session_destroy(session), RULES_FORGE_ERROR_INVALID_ARGUMENT);
            check_contains(ruleforge_get_last_error_message(), "active");

            char const* part_one = R"({"name":"Ali)";
            char const* part_two = R"(ce","age":30,"score":98.5})";
            check_equal(ruleforge_data_bind_stream_feed(stream, part_one, std::strlen(part_one)),
                         RULES_FORGE_OK);
            check_equal(ruleforge_data_bind_stream_feed(stream, part_two, std::strlen(part_two)),
                         RULES_FORGE_OK);
            check_equal(ruleforge_session_get_fact_count(session), 0);

            ruleforge_fact_t* facts = nullptr;
            int loaded = 0;
            check_equal(ruleforge_data_bind_stream_finish(stream, &facts, &loaded),
                         RULES_FORGE_OK);
            check_equal(loaded, 1);
            check_not_null(facts);
            check_equal(ruleforge_session_get_fact_count(session), 1);
            char name[16] = {0};
            size_t actual_length = 0;
            check_equal(ruleforge_fact_get_field_as_string(
                             facts[0], "name", name, sizeof(name), &actual_length),
                         RULES_FORGE_OK);
            check_equal(name, "Alice");
            check_equal(ruleforge_data_bind_stream_feed(stream, " ", 1),
                         RULES_FORGE_ERROR_INVALID_ARGUMENT);

            ruleforge_fact_array_free(facts);
            check_equal(ruleforge_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("filters complete and streamed JSON by path before rule evaluation") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);
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
            check_equal(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            char const* json =
                R"({"customers":[{"name":"Alice","age":30},{"name":"Bob","age":17}],)"
                R"("ignored":[{"name":"Mallory","age":40}]})";
            char const* json_path = "$.customers[*]";

            ruleforge_stateful_session_t complete_session = nullptr;
            check_equal(ruleforge_session_create(kb, &complete_session), RULES_FORGE_OK);
            ruleforge_fact_t* complete_facts = nullptr;
            int complete_count = 0;
            check_equal(ruleforge_session_add_facts_json_path(
                             complete_session, "Customer",
                             json, json_path, &complete_facts, &complete_count),
                         RULES_FORGE_OK);
            check_equal(complete_count, 2);
            check_equal(ruleforge_session_get_fact_count(complete_session), 2);
            int complete_fired = 0;
            check_equal(ruleforge_session_fire_all_rules(
                             complete_session, -1, &complete_fired), RULES_FORGE_OK);
            check_equal(complete_fired, 1);
            ruleforge_query_result_t complete_query = nullptr;
            check_equal(ruleforge_session_query(
                             complete_session, "Adults", &complete_query), RULES_FORGE_OK);
            check_equal(ruleforge_query_result_get_size(complete_query), 1);

            ruleforge_stateful_session_t stream_session = nullptr;
            check_equal(ruleforge_session_create(kb, &stream_session), RULES_FORGE_OK);
            ruleforge_data_bind_stream_t stream = nullptr;
            check_equal(ruleforge_data_bind_stream_json_path_all_create(
                             stream_session, "Customer",
                             json_path, &stream), RULES_FORGE_OK);
            size_t split = std::strlen(json) / 2;
            check_equal(ruleforge_data_bind_stream_feed(stream, json, split), RULES_FORGE_OK);
            check_equal(ruleforge_data_bind_stream_feed(
                             stream, json + split, std::strlen(json) - split), RULES_FORGE_OK);
            ruleforge_fact_t* stream_facts = nullptr;
            int stream_count = 0;
            check_equal(ruleforge_data_bind_stream_finish(
                             stream, &stream_facts, &stream_count), RULES_FORGE_OK);
            check_equal(stream_count, complete_count);
            check_equal(ruleforge_session_get_fact_count(stream_session), 2);
            int stream_fired = 0;
            check_equal(ruleforge_session_fire_all_rules(
                             stream_session, -1, &stream_fired), RULES_FORGE_OK);
            check_equal(stream_fired, complete_fired);
            ruleforge_query_result_t stream_query = nullptr;
            check_equal(ruleforge_session_query(
                             stream_session, "Adults", &stream_query), RULES_FORGE_OK);
            check_equal(ruleforge_query_result_get_size(stream_query), 1);

            ruleforge_stateful_session_t first_session = nullptr;
            check_equal(ruleforge_session_create(kb, &first_session), RULES_FORGE_OK);
            ruleforge_fact_t first = nullptr;
            check_equal(ruleforge_session_add_fact_json_path(
                             first_session, "Customer",
                             json, json_path, &first), RULES_FORGE_OK);
            check_not_null(first);
            char name[16] = {0};
            size_t name_length = 0;
            check_equal(ruleforge_fact_get_field_as_string(
                             first, "name", name, sizeof(name), &name_length), RULES_FORGE_OK);
            check_equal(name, "Alice");

            check_equal(ruleforge_query_result_destroy(stream_query), RULES_FORGE_OK);
            check_equal(ruleforge_query_result_destroy(complete_query), RULES_FORGE_OK);
            ruleforge_fact_array_free(stream_facts);
            ruleforge_fact_array_free(complete_facts);
            check_equal(ruleforge_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(first_session), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(stream_session), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(complete_session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("loads YAML roots and YPATH-selected facts through Salts DataBind") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);
            auto schema_path = write_temp_schema(
                "rulesforge_capi_databind_yaml.schema",
                "schema Market [id(37), version(1), byte_order(little)]; "
                "message Customer { int32 age; string name; }");
            std::string drl = std::string("import \"") + schema_path.generic_string() + R"(";
query "Adults"
    $customer : Customer(age >= 18)
end
)";
            check_equal(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            char const* root_yaml = "name: Alice\nage: 30\n";
            ruleforge_stateful_session_t root_session = nullptr;
            check_equal(ruleforge_session_create(kb, &root_session), RULES_FORGE_OK);
            ruleforge_fact_t root_fact = nullptr;
            check_equal(ruleforge_session_add_fact_yaml(
                             root_session, "Customer",
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
            check_equal(ruleforge_session_create(kb, &complete_session), RULES_FORGE_OK);
            ruleforge_fact_t* complete_facts = nullptr;
            int complete_count = 0;
            check_equal(ruleforge_session_add_facts_yaml_path(
                             complete_session, "Customer",
                             yaml, yaml_path, &complete_facts, &complete_count),
                         RULES_FORGE_OK);
            check_equal(complete_count, 2);

            ruleforge_stateful_session_t first_session = nullptr;
            check_equal(ruleforge_session_create(kb, &first_session), RULES_FORGE_OK);
            ruleforge_fact_t first_fact = nullptr;
            check_equal(ruleforge_session_add_fact_yaml_path(
                             first_session, "Customer",
                             yaml, yaml_path, &first_fact), RULES_FORGE_OK);
            char name[16] = {0};
            size_t name_length = 0;
            check_equal(ruleforge_fact_get_field_as_string(
                             first_fact, "name", name, sizeof(name), &name_length),
                         RULES_FORGE_OK);
            check_equal(name, "Alice");

            ruleforge_stateful_session_t stream_session = nullptr;
            check_equal(ruleforge_session_create(kb, &stream_session), RULES_FORGE_OK);
            ruleforge_data_bind_stream_t stream = nullptr;
            check_equal(ruleforge_data_bind_stream_yaml_path_all_create(
                             stream_session, "Customer",
                             yaml_path, &stream), RULES_FORGE_OK);
            size_t split = std::strlen(yaml) / 2;
            check_equal(ruleforge_data_bind_stream_feed(stream, yaml, split), RULES_FORGE_OK);
            check_equal(ruleforge_data_bind_stream_feed(
                             stream, yaml + split, std::strlen(yaml) - split),
                         RULES_FORGE_OK);
            ruleforge_fact_t* stream_facts = nullptr;
            int stream_count = 0;
            check_equal(ruleforge_data_bind_stream_finish(
                             stream, &stream_facts, &stream_count), RULES_FORGE_OK);
            check_equal(stream_count, complete_count);
            check_equal(ruleforge_session_get_fact_count(stream_session), 2);

            ruleforge_fact_array_free(stream_facts);
            ruleforge_fact_array_free(complete_facts);
            check_equal(ruleforge_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(stream_session), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(first_session), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(complete_session), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(root_session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("adds schema-bound extended scalar facts through Salts DataBind") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            auto schema_path = std::filesystem::temp_directory_path()
                / "rulesforge_capi_databind_scalars.schema";
            {
                std::ofstream schema(schema_path, std::ios::binary);
                schema << "schema Market [id(12), version(1), byte_order(little)]; "
                          "message ScalarFact { "
                          "uuid id; uint64 counter; bytes raw; datetime observed_at; "
                          "date trade_date; time trade_time; duration latency; "
                          "decimal price; bigint sequence; money total; bool active; "
                          "}";
            }

            std::string drl = std::string("import \"")
                + schema_path.generic_string()
                + R"(";
                    query "FindScalar"
                        $s : ScalarFact(
                            trade_date == "2026-06-28",
                            trade_time == "09:30:05.123",
                            observed_at == "2026-06-28T09:30:05.123Z",
                            latency == 5405250,
                            price == "123.45",
                            sequence == "123456789012345678901234567890",
                            total == "USD 123.45",
                            active == true
                        )
                    end

                    query "StringUuidDoesNotMatch"
                        $s : ScalarFact(
                            id == "01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001"
                        )
                    end
                )";
            check_equal(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);
            check_equal(
                ruleforge_session_set_validation_mode(session, RULES_FORGE_VALIDATION_STRICT),
                RULES_FORGE_OK);

            ruleforge_fact_t fact = nullptr;
            check_equal(
                ruleforge_session_add_fact_json(
                    session,
                    "ScalarFact",
                    R"({
                        "id":"01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001",
                        "counter":18446744073709551615,
                        "raw":"Az",
                        "observed_at":"2026-06-28T09:30:05.123Z",
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

            auto const* internal_fact = reinterpret_cast<Fact const*>(fact);
            auto id_field = internal_fact->fields.find("id");
            check(id_field != internal_fact->fields.end());
            auto const* uuid = id_field != internal_fact->fields.end()
                ? std::get_if<salts_uuid_t>(&id_field->second)
                : nullptr;
            check_not_null(uuid);
            salts_uuid_t expected_uuid{};
            check_equal(
                salts_uuid_parse("01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001", &expected_uuid),
                SALTS_OK);
            check(uuid != nullptr && salts_uuid_equal(uuid, &expected_uuid));

            auto counter_field = internal_fact->fields.find("counter");
            check(counter_field != internal_fact->fields.end());
            auto const* counter = counter_field != internal_fact->fields.end()
                ? std::get_if<uint64_t>(&counter_field->second) : nullptr;
            check_not_null(counter);
            check(counter != nullptr && *counter == UINT64_MAX);

            auto raw_field = internal_fact->fields.find("raw");
            check(raw_field != internal_fact->fields.end());
            auto const* raw = raw_field != internal_fact->fields.end()
                ? std::get_if<BytesValue>(&raw_field->second) : nullptr;
            check_not_null(raw);
            check(raw != nullptr && raw->bytes == std::vector<uint8_t>({'A', 'z'}));

            auto has_runtime_type = [internal_fact]<typename T>(char const* field_name) {
                auto field = internal_fact->fields.find(field_name);
                return field != internal_fact->fields.end()
                    && std::holds_alternative<T>(field->second);
            };
            check(has_runtime_type.operator()<DateTimeValue>("observed_at"));
            check(has_runtime_type.operator()<DateValue>("trade_date"));
            check(has_runtime_type.operator()<TimeValue>("trade_time"));
            check(has_runtime_type.operator()<DurationValue>("latency"));
            check(has_runtime_type.operator()<DecimalValue>("price"));
            check(has_runtime_type.operator()<BigIntValue>("sequence"));
            check(has_runtime_type.operator()<MoneyValue>("total"));

            auto active_field = internal_fact->fields.find("active");
            check(active_field != internal_fact->fields.end());
            auto const* active_value = active_field != internal_fact->fields.end()
                ? std::get_if<bool>(&active_field->second)
                : nullptr;
            check_not_null(active_value);
            check(active_value != nullptr && *active_value);

            char uuid_text_buffer[64] = {0};
            size_t uuid_text_length = 0;
            check(
                ruleforge_fact_get_field_as_string(
                    fact, "id", uuid_text_buffer, sizeof(uuid_text_buffer), &uuid_text_length)
                != RULES_FORGE_OK);

            int64_t latency = 0;
            check_equal(ruleforge_fact_get_field_as_int(fact, "latency", &latency), RULES_FORGE_OK);
            check_equal((int)latency, 5405250);

            uint64_t counter_value = 0;
            check_equal(
                ruleforge_fact_get_field_as_uint64(fact, "counter", &counter_value),
                RULES_FORGE_OK);
            check(counter_value == UINT64_MAX);
            uint8_t raw_buffer[2] = {};
            size_t raw_length = 0;
            check_equal(
                ruleforge_fact_get_field_as_bytes(
                    fact, "raw", raw_buffer, sizeof(raw_buffer), &raw_length),
                RULES_FORGE_OK);
            check_equal(raw_length, 2);
            check(raw_buffer[0] == 'A' && raw_buffer[1] == 'z');

            int active = 0;
            check_equal(ruleforge_fact_get_field_as_bool(fact, "active", &active), RULES_FORGE_OK);
            check_equal(active, 1);
            int64_t active_as_int = 0;
            check(
                ruleforge_fact_get_field_as_int(fact, "active", &active_as_int)
                != RULES_FORGE_OK);

            char text_buffer[96] = {0};
            size_t actual_length = 0;
            check_equal(
                ruleforge_fact_get_field_as_string(
                    fact, "total", text_buffer, sizeof(text_buffer), &actual_length),
                RULES_FORGE_OK);
            check_equal(text_buffer, "USD 123.45");

            ruleforge_query_result_t query_result = nullptr;
            check_equal(ruleforge_session_query(session, "FindScalar", &query_result), RULES_FORGE_OK);
            check_not_null(query_result);
            check_equal(ruleforge_query_result_get_size(query_result), 1);

            check_equal(ruleforge_query_result_destroy(query_result), RULES_FORGE_OK);
            query_result = nullptr;
            check_equal(
                ruleforge_session_query(session, "StringUuidDoesNotMatch", &query_result),
                RULES_FORGE_OK);
            check_not_null(query_result);
            check_equal(ruleforge_query_result_get_size(query_result), 0);

            check_equal(ruleforge_query_result_destroy(query_result), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("resolves short fact names against packaged declarations") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);

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
            check_equal(ruleforge_kb_load_drl(kb, packaged_drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            check_equal(
                ruleforge_session_add_fact_json(
                    session,
                    "MqttSubscribeTask",
                    R"({"client_id":"rules-client","username":"alice"})",
                    nullptr),
                RULES_FORGE_OK);

            ruleforge_query_result_t query_result = nullptr;
            check_equal(ruleforge_session_query(session, "FindSubscribeTask", &query_result), RULES_FORGE_OK);
            check_not_null(query_result);
            check_equal(ruleforge_query_result_get_size(query_result), 1);

            ruleforge_fact_t fact = nullptr;
            check_equal(ruleforge_query_result_get_fact_at_index(query_result, 0, "task", &fact), RULES_FORGE_OK);
            check_not_null(fact);

            char username[32] = {0};
            size_t actual_length = 0;
            check_equal(
                ruleforge_fact_get_field_as_string(
                    fact, "username", username, sizeof(username), &actual_length),
                RULES_FORGE_OK);
            check_equal(username, "alice");

            check_equal(ruleforge_query_result_destroy(query_result), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("adds schema-bound binary fact through Salts DataBind") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);

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
            check_equal(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            uint8_t payload[12] = {0};
            write_u32_le(payload, 0, 100);
            write_u32_le(payload, 4, 200);
            write_u32_le(payload, 8, 300);

            ruleforge_fact_t fact = nullptr;
            check_equal(
                ruleforge_session_add_fact_binary(
                    session,
                    "BinaryFact",
                    payload,
                    sizeof(payload),
                    &fact),
                RULES_FORGE_OK);
            check_not_null(fact);

            int64_t c = 0;
            check_equal(ruleforge_fact_get_field_as_int(fact, "c", &c), RULES_FORGE_OK);
            check_equal((int)c, 300);

            ruleforge_query_result_t query_result = nullptr;
            check_equal(ruleforge_session_query(session, "FindBinary", &query_result), RULES_FORGE_OK);
            check_not_null(query_result);
            check_equal(ruleforge_query_result_get_size(query_result), 1);

            check_equal(ruleforge_query_result_destroy(query_result), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("adds schema-bound CSV facts through Salts DataBind") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);

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
            check_equal(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            char const* csv = "name,age,score\nAlice,30,98.5\nBob,17,70.0\n";
            ruleforge_fact_t* facts = nullptr;
            int loaded = 0;
            check_equal(
                ruleforge_session_add_facts_csv(
                    session,
                    "Customer",
                    csv,
                    &facts,
                    &loaded),
                RULES_FORGE_OK);
            check_equal(loaded, 2);
            check_not_null(facts);
            check_equal(ruleforge_session_get_fact_count(session), 2);

            char name_buffer[32] = {0};
            size_t actual_length = 0;
            check_equal(
                ruleforge_fact_get_field_as_string(
                    facts[0], "name", name_buffer, sizeof(name_buffer), &actual_length),
                RULES_FORGE_OK);
            check_equal(name_buffer, "Alice");

            ruleforge_fact_array_free(facts);
            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("adds schema-bound CSV facts from asynchronous chunks") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);
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
            check_equal(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            ruleforge_data_bind_stream_t stream = nullptr;
            check_equal(ruleforge_data_bind_stream_csv_all_create(
                             session, "Customer", &stream),
                         RULES_FORGE_OK);
            char const* head = "name,age,score\nAli";
            char const* tail = "ce,30,98.5\nBob,17,70.0\n";
            check_equal(ruleforge_data_bind_stream_feed(stream, head, std::strlen(head)),
                         RULES_FORGE_OK);
            check_equal(ruleforge_data_bind_stream_feed(stream, tail, std::strlen(tail)),
                         RULES_FORGE_OK);

            ruleforge_fact_t* facts = nullptr;
            int loaded = 0;
            check_equal(ruleforge_data_bind_stream_finish(stream, &facts, &loaded),
                         RULES_FORGE_OK);
            check_equal(loaded, 2);
            check_not_null(facts);
            check_equal(ruleforge_session_get_fact_count(session), 2);

            ruleforge_fact_array_free(facts);
            check_equal(ruleforge_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("filters complete and streamed CSV by path before rule evaluation") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);
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
            check_equal(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            char const* csv =
                "name_s,age_n,region_s\nAlice,30,west\nBob,17,west\nMallory,40,east\n";
            char const* csv_path = "region == \"west\"";

            ruleforge_stateful_session_t complete_session = nullptr;
            check_equal(ruleforge_session_create(kb, &complete_session), RULES_FORGE_OK);
            ruleforge_fact_t* complete_facts = nullptr;
            int complete_count = 0;
            check_equal(ruleforge_session_add_facts_csv_path(
                             complete_session, "Customer",
                             csv, csv_path, &complete_facts, &complete_count),
                         RULES_FORGE_OK);
            check_equal(complete_count, 2);
            check_equal(ruleforge_session_get_fact_count(complete_session), 2);
            int complete_fired = 0;
            check_equal(ruleforge_session_fire_all_rules(
                             complete_session, -1, &complete_fired), RULES_FORGE_OK);
            check_equal(complete_fired, 1);

            ruleforge_stateful_session_t stream_session = nullptr;
            check_equal(ruleforge_session_create(kb, &stream_session), RULES_FORGE_OK);
            ruleforge_data_bind_stream_t stream = nullptr;
            check_equal(ruleforge_data_bind_stream_csv_path_create(
                             stream_session, "Customer",
                             csv_path, &stream), RULES_FORGE_OK);
            size_t split = std::strlen(csv) / 2;
            check_equal(ruleforge_data_bind_stream_feed(stream, csv, split), RULES_FORGE_OK);
            check_equal(ruleforge_data_bind_stream_feed(
                             stream, csv + split, std::strlen(csv) - split), RULES_FORGE_OK);
            ruleforge_fact_t* stream_facts = nullptr;
            int stream_count = 0;
            check_equal(ruleforge_data_bind_stream_finish(
                             stream, &stream_facts, &stream_count), RULES_FORGE_OK);
            check_equal(stream_count, complete_count);
            check_equal(ruleforge_session_get_fact_count(stream_session), 2);
            int stream_fired = 0;
            check_equal(ruleforge_session_fire_all_rules(
                             stream_session, -1, &stream_fired), RULES_FORGE_OK);
            check_equal(stream_fired, complete_fired);

            ruleforge_fact_array_free(stream_facts);
            ruleforge_fact_array_free(complete_facts);
            check_equal(ruleforge_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(stream_session), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(complete_session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("rejects finishing an invalid DataBind stream without inserting facts") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);
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
            check_equal(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            ruleforge_data_bind_stream_t stream = nullptr;
            check_equal(ruleforge_data_bind_stream_json_create(
                             session, "Customer", &stream),
                         RULES_FORGE_OK);
            char const* invalid = R"({"name":"Alice","age":})";
            ruleforge_status_t feed_status =
                ruleforge_data_bind_stream_feed(stream, invalid, std::strlen(invalid));
            if (feed_status == RULES_FORGE_OK) {
                check_equal(ruleforge_data_bind_stream_finish(stream, nullptr, nullptr),
                             RULES_FORGE_ERROR_INVALID_ARGUMENT);
            } else {
                check_equal(feed_status, RULES_FORGE_ERROR_INVALID_ARGUMENT);
                check_equal(ruleforge_data_bind_stream_finish(stream, nullptr, nullptr),
                             RULES_FORGE_ERROR_INVALID_ARGUMENT);
            }
            check_equal(ruleforge_session_get_fact_count(session), 0);

            check_equal(ruleforge_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("adds schema-bound XML facts through Salts DataBind") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);

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
            check_equal(ruleforge_kb_load_drl(kb, drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            char const* xml =
                "<orders>"
                "<order><id>1</id><side>Buy</side><symbol>ABCD</symbol></order>"
                "<order><id>2</id><side>Sell</side><symbol>WXYZ</symbol></order>"
                "</orders>";
            ruleforge_fact_t* facts = nullptr;
            int loaded = 0;
            check_equal(
                ruleforge_session_add_facts_xml(
                    session,
                    "Order",
                    xml,
                    "//order",
                    &facts,
                    &loaded),
                RULES_FORGE_OK);
            check_equal(loaded, 2);
            check_not_null(facts);

            int64_t side = 0;
            char side_name[16] = {};
            size_t side_name_length = 0;
            check_equal(ruleforge_fact_get_field_as_enum(
                             facts[1], "side", side_name, sizeof(side_name),
                             &side_name_length, &side), RULES_FORGE_OK);
            check_equal((int)side, 2);
            check_equal(side_name, "Sell");

            ruleforge_query_result_t query_result = nullptr;
            check_equal(ruleforge_session_query(session, "FindOrder", &query_result), RULES_FORGE_OK);
            check_not_null(query_result);
            check_equal(ruleforge_query_result_get_size(query_result), 1);

            ruleforge_stateful_session_t stream_session = nullptr;
            check_equal(ruleforge_session_create(kb, &stream_session), RULES_FORGE_OK);
            ruleforge_data_bind_stream_t stream = nullptr;
            check_equal(ruleforge_data_bind_stream_xml_path_all_create(
                             stream_session, "Order",
                             "//order", &stream), RULES_FORGE_OK);
            size_t split = std::strlen(xml) / 2;
            check_equal(ruleforge_data_bind_stream_feed(stream, xml, split), RULES_FORGE_OK);
            check_equal(ruleforge_data_bind_stream_feed(
                             stream, xml + split, std::strlen(xml) - split), RULES_FORGE_OK);
            ruleforge_fact_t* stream_facts = nullptr;
            int stream_loaded = 0;
            check_equal(ruleforge_data_bind_stream_finish(
                             stream, &stream_facts, &stream_loaded), RULES_FORGE_OK);
            check_equal(stream_loaded, loaded);
            ruleforge_query_result_t stream_query = nullptr;
            check_equal(ruleforge_session_query(
                             stream_session, "FindOrder", &stream_query), RULES_FORGE_OK);
            check_equal(ruleforge_query_result_get_size(stream_query), 1);

            check_equal(ruleforge_query_result_destroy(stream_query), RULES_FORGE_OK);
            check_equal(ruleforge_query_result_destroy(query_result), RULES_FORGE_OK);
            ruleforge_fact_array_free(stream_facts);
            ruleforge_fact_array_free(facts);
            check_equal(ruleforge_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(stream_session), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("fires all rules") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);

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
            check_equal(ruleforge_kb_load_drl(kb, simple_drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            const char* fact_type = "Fact";
            const char* fact_json = R"({"id": 1})";
            check_equal(ruleforge_session_add_fact_json(
                             session, fact_type, fact_json,
                             nullptr),
                         RULES_FORGE_OK);

            int fired_count = 0;
            check_equal(ruleforge_session_fire_all_rules(session, -1, &fired_count), RULES_FORGE_OK);
            check_equal(fired_count, 1);

            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("queries facts and accesses fields") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);

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
            check_equal(ruleforge_kb_load_drl(kb, query_drl.c_str()), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            check_equal(ruleforge_session_add_fact_json(
                             session, "Person",
                             R"({"name": "Bob", "age": 25})", nullptr),
                         RULES_FORGE_OK);
            check_equal(ruleforge_session_add_fact_json(
                             session, "Person",
                             R"({"name": "Charlie", "age": 17})", nullptr),
                         RULES_FORGE_OK);
            check_equal(ruleforge_session_add_fact_json(
                             session, "Person",
                             R"({"name": "Diana", "age": 30})", nullptr),
                         RULES_FORGE_OK);

            check_equal(ruleforge_session_get_fact_count(session), 3);

            check_equal(ruleforge_session_fire_all_rules(session, -1, nullptr), RULES_FORGE_OK);

            ruleforge_query_result_t query_result = nullptr;
            check_equal(ruleforge_session_query(session, "AdultPersons", &query_result), RULES_FORGE_OK);
            check_not_null(query_result);

            int result_size = ruleforge_query_result_get_size(query_result);
            check_equal(result_size, 2);

            bool has_bob = false;
            bool has_diana = false;
            char name_buffer[50];
            size_t actual_length = 0;
            for (int i = 0; i < result_size; ++i) {
                ruleforge_fact_t fact_row = nullptr;
                check_equal(ruleforge_query_result_get_fact_at_index(query_result, i, "p", &fact_row), RULES_FORGE_OK);
                check_not_null(fact_row);

                check_equal(
                    ruleforge_fact_get_field_as_string(
                        fact_row, "name", name_buffer, sizeof(name_buffer), &actual_length),
                    RULES_FORGE_OK);

                double age_double = 0.0;
                check_equal(ruleforge_fact_get_field_as_double(fact_row, "age", &age_double), RULES_FORGE_OK);
                int64_t age_int = 0;
                check_equal(ruleforge_fact_get_field_as_int(fact_row, "age", &age_int), RULES_FORGE_OK);

                if (std::string(name_buffer) == "Bob") {
                    has_bob = true;
                    check_within(age_double, 25.0, 0.001);
                    check_equal(age_int, 25);
                } else if (std::string(name_buffer) == "Diana") {
                    has_diana = true;
                    check_within(age_double, 30.0, 0.001);
                    check_equal(age_int, 30);
                }
            }
            check(has_bob);
            check(has_diana);

            // Keep field-missing path covered.
            ruleforge_fact_t any_fact = nullptr;
            check_equal(ruleforge_query_result_get_fact_at_index(query_result, 0, "p", &any_fact), RULES_FORGE_OK);
            check_not_equal(
                ruleforge_fact_get_field_as_string(
                    any_fact, "nonExistent", name_buffer, sizeof(name_buffer), &actual_length),
                RULES_FORGE_OK);

            check_equal(ruleforge_query_result_destroy(query_result), RULES_FORGE_OK);
            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("returns query error when query does not exist") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);

            const char* drl = R"(
declare Person
    name: String
end
)";
            check_equal(ruleforge_kb_load_drl(kb, drl), RULES_FORGE_OK);

            ruleforge_stateful_session_t session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            ruleforge_query_result_t query_result = reinterpret_cast<ruleforge_query_result_t>(0x1);
            check_equal(
                ruleforge_session_query(session, "NoSuchQuery", &query_result),
                RULES_FORGE_ERROR_QUERY_FAILED);
            check(query_result == nullptr);
            check_contains(ruleforge_get_last_error_message(), "not found");

            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            ruleforge_cleanup();

            // Keep this as the last test and extend it with consistency error mapping checks
            // so tinytest's current test-count cap still covers these assertions.
            ruleforge_init();
            kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);

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
            check_equal(ruleforge_kb_load_drl(kb, failing_drl.c_str()), RULES_FORGE_OK);

            session = nullptr;
            check_equal(ruleforge_session_create(kb, &session), RULES_FORGE_OK);
            check_not_null(session);

            check_equal(
                ruleforge_session_set_validation_mode(session, RULES_FORGE_VALIDATION_STRICT),
                RULES_FORGE_OK);
            check_equal(
                ruleforge_session_add_fact_json(
                    session, "Person",
                    R"({"name":"Alice","age":30})", nullptr),
                RULES_FORGE_OK);

            // First run fails in RHS and leaves session inconsistent due to rollback failure.
            check_not_equal(ruleforge_session_fire_all_rules(session, -1, nullptr), RULES_FORGE_OK);
            check_equal(ruleforge_session_get_fact_count(session), 0);

            check_equal(
                ruleforge_session_add_fact_json(
                    session, "Person",
                    R"({"name":"Bob","age":40})", nullptr),
                RULES_FORGE_ERROR_SESSION_INCONSISTENT);
            check_contains(ruleforge_get_last_error_message(), "inconsistent");

            check_equal(
                ruleforge_session_set_validation_mode(session, RULES_FORGE_VALIDATION_WARN),
                RULES_FORGE_ERROR_SESSION_INCONSISTENT);
            check_equal(
                ruleforge_session_enable_tracing(session, 1),
                RULES_FORGE_ERROR_SESSION_INCONSISTENT);
            check_equal(
                ruleforge_session_fire_all_rules(session, -1, nullptr),
                RULES_FORGE_ERROR_SESSION_INCONSISTENT);
            check_equal(
                ruleforge_session_set_validation_mode(
                    session, static_cast<ruleforge_validation_mode_t>(999)),
                RULES_FORGE_ERROR_INVALID_ARGUMENT);

            check_equal(ruleforge_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(person_schema_path);
            ruleforge_cleanup();
        }
    }

    group("Continuous Session and DataBind") {
        it("pushes a schema-bound JSON event and exposes immutable outputs") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);
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
            check_equal(ruleforge_kb_load_drl(kb, rfl.c_str()), RULES_FORGE_OK);

            ruleforge_continuous_config_t config{};
            check_equal(ruleforge_continuous_config_init(&config), RULES_FORGE_OK);
            char const* output_types[] = {"Alert"};
            config.output_fact_types = output_types;
            config.output_fact_type_count = 1;
            config.event_retention_ms = 100;
            config.dedup_retention_ms = 200;

            ruleforge_continuous_session_t session = nullptr;
            check_equal(ruleforge_continuous_session_create(kb, &config, &session),
                         RULES_FORGE_OK);
            check_not_null(session);

            ruleforge_continuous_result_t result = nullptr;
            check_equal(ruleforge_continuous_push_json(
                             session, "Event", "event-1",
                             "events", 100, R"({"value":7})", &result),
                         RULES_FORGE_OK);
            check_not_null(result);
            check_equal(ruleforge_continuous_result_get_status(result),
                         RULES_FORGE_CONTINUOUS_COMMITTED);
            check_equal(ruleforge_continuous_result_get_rules_fired(result), 1);
            check_equal(ruleforge_continuous_result_get_output_count(result), 1);
            ruleforge_fact_t output = nullptr;
            check_equal(ruleforge_continuous_result_get_output(result, 0, &output),
                         RULES_FORGE_OK);
            int64_t value = 0;
            check_equal(ruleforge_fact_get_field_as_int(output, "value", &value),
                         RULES_FORGE_OK);
            check_equal(static_cast<int>(value), 7);
            uint64_t batch_id = ruleforge_continuous_result_get_batch_id(result);
            check_equal(ruleforge_continuous_acknowledge(session, batch_id), RULES_FORGE_OK);
            check_equal(ruleforge_continuous_result_destroy(result), RULES_FORGE_OK);

            result = nullptr;
            check_equal(ruleforge_continuous_push_yaml(
                             session, "Event", "event-2",
                             "events", 101, "value: 8\n", &result),
                         RULES_FORGE_OK);
            check_equal(ruleforge_continuous_result_get_rules_fired(result), 1);
            check_equal(ruleforge_continuous_acknowledge(
                             session, ruleforge_continuous_result_get_batch_id(result)),
                         RULES_FORGE_OK);
            check_equal(ruleforge_continuous_result_destroy(result), RULES_FORGE_OK);

            char const* object_json = R"({"value":9})";
            ruleforge_data_bind_object_t object = nullptr;
            check_equal(ruleforge_data_bind_object_from_json(
                             schema_path.string().c_str(), "Event", object_json,
                             std::strlen(object_json), &object), RULES_FORGE_OK);
            result = nullptr;
            check_equal(ruleforge_continuous_push_data_bind_object(
                             session, object, "event-3", "events", 102, &result),
                         RULES_FORGE_OK);
            check_equal(ruleforge_continuous_result_get_rules_fired(result), 1);
            check_equal(ruleforge_continuous_acknowledge(
                             session, ruleforge_continuous_result_get_batch_id(result)),
                         RULES_FORGE_OK);
            check_equal(ruleforge_continuous_result_destroy(result), RULES_FORGE_OK);
            check_equal(ruleforge_data_bind_object_destroy(object), RULES_FORGE_OK);

            result = nullptr;
            check_equal(ruleforge_continuous_advance_watermark(session, 202, &result),
                         RULES_FORGE_OK);
            int64_t watermark = 0;
            int has_watermark = 0;
            check_equal(ruleforge_continuous_result_get_watermark(
                             result, &watermark, &has_watermark), RULES_FORGE_OK);
            check_equal(has_watermark, 1);
            check_equal(static_cast<int>(watermark), 202);
            check_equal(ruleforge_continuous_acknowledge(
                             session, ruleforge_continuous_result_get_batch_id(result)),
                         RULES_FORGE_OK);
            check_equal(ruleforge_continuous_result_destroy(result), RULES_FORGE_OK);

            ruleforge_continuous_metrics_t metrics{};
            check_equal(ruleforge_continuous_get_metrics(session, &metrics), RULES_FORGE_OK);
            check_equal(metrics.accepted_events, 3);
            check_equal(metrics.active_events, 0);

            check_equal(ruleforge_continuous_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("commits an asynchronous DataBind event only when the stream finishes") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);
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
            check_equal(ruleforge_kb_load_drl(kb, rfl.c_str()), RULES_FORGE_OK);
            ruleforge_continuous_config_t config{};
            check_equal(ruleforge_continuous_config_init(&config), RULES_FORGE_OK);
            ruleforge_continuous_session_t session = nullptr;
            check_equal(ruleforge_continuous_session_create(kb, &config, &session),
                         RULES_FORGE_OK);

            ruleforge_continuous_data_bind_stream_t stream = nullptr;
            check_equal(ruleforge_continuous_data_bind_stream_json_create(
                             session, "Event", "stream-1",
                             "events", 300, &stream), RULES_FORGE_OK);
            check_not_null(stream);
            check_equal(ruleforge_continuous_session_destroy(session),
                         RULES_FORGE_ERROR_INVALID_ARGUMENT);

            char const* first = R"({"val)";
            char const* second = R"(ue":9})";
            check_equal(ruleforge_continuous_data_bind_stream_feed(
                             stream, first, std::strlen(first)), RULES_FORGE_OK);
            check_equal(ruleforge_continuous_data_bind_stream_feed(
                             stream, second, std::strlen(second)), RULES_FORGE_OK);
            ruleforge_continuous_metrics_t metrics{};
            check_equal(ruleforge_continuous_get_metrics(session, &metrics), RULES_FORGE_OK);
            check_equal(metrics.accepted_events, 0);

            ruleforge_continuous_result_t result = nullptr;
            check_equal(ruleforge_continuous_data_bind_stream_finish(stream, &result),
                         RULES_FORGE_OK);
            check_not_null(result);
            check_equal(ruleforge_continuous_result_get_rules_fired(result), 1);
            ruleforge_continuous_result_t duplicate_result = result;
            check_equal(ruleforge_continuous_data_bind_stream_finish(stream, &duplicate_result),
                         RULES_FORGE_ERROR_INVALID_ARGUMENT);
            check_null(duplicate_result);
            check_equal(ruleforge_continuous_get_metrics(session, &metrics), RULES_FORGE_OK);
            check_equal(metrics.accepted_events, 1);

            check_equal(ruleforge_continuous_result_destroy(result), RULES_FORGE_OK);
            check_equal(ruleforge_continuous_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_equal(ruleforge_continuous_session_destroy(session), RULES_FORGE_OK);
            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }

        it("commits path-selected JSON YAML CSV and XML batches from documents and streams") {
            ruleforge_init();
            ruleforge_knowledge_base_t kb = nullptr;
            check_equal(ruleforge_kb_create(&kb), RULES_FORGE_OK);
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
            check_equal(ruleforge_kb_load_drl(kb, rfl.c_str()), RULES_FORGE_OK);

            auto create_session = [&]() {
                ruleforge_continuous_config_t config{};
                check_equal(ruleforge_continuous_config_init(&config), RULES_FORGE_OK);
                ruleforge_continuous_session_t session = nullptr;
                check_equal(ruleforge_continuous_session_create(kb, &config, &session),
                             RULES_FORGE_OK);
                return session;
            };
            auto verify_batch = [&](ruleforge_continuous_session_t session,
                                    ruleforge_continuous_result_t result) {
                check_not_null(result);
                check_equal(ruleforge_continuous_result_get_rules_fired(result), 2);
                ruleforge_continuous_metrics_t metrics{};
                check_equal(ruleforge_continuous_get_metrics(session, &metrics), RULES_FORGE_OK);
                check_equal(metrics.accepted_events, 2);
                check_equal(ruleforge_continuous_acknowledge(
                                 session, ruleforge_continuous_result_get_batch_id(result)),
                             RULES_FORGE_OK);
                check_equal(ruleforge_continuous_result_destroy(result), RULES_FORGE_OK);
            };

            char const* json =
                R"({"events":[{"event_id":"json-1","event_time":100,"value":7,"region":"west"},)"
                R"({"event_id":"json-2","event_time":101,"value":9,"region":"west"}],)"
                R"("ignored":[{"event_id":"json-3","event_time":102,"value":11,"region":"east"}]})";
            char const* json_path = "$.events[*]";
            auto json_complete = create_session();
            ruleforge_continuous_result_t result = nullptr;
            check_equal(ruleforge_continuous_push_json_path(
                             json_complete, "Event", json,
                             json_path, "event_id", "event_time", "events", &result),
                         RULES_FORGE_OK);
            verify_batch(json_complete, result);
            check_equal(ruleforge_continuous_session_destroy(json_complete), RULES_FORGE_OK);

            auto json_stream_session = create_session();
            ruleforge_continuous_data_bind_stream_t stream = nullptr;
            check_equal(ruleforge_continuous_data_bind_stream_json_path_create(
                             json_stream_session, "Event",
                             json_path, "event_id", "event_time", "events", &stream),
                         RULES_FORGE_OK);
            size_t json_split = std::strlen(json) / 2;
            check_equal(ruleforge_continuous_data_bind_stream_feed(
                             stream, json, json_split), RULES_FORGE_OK);
            ruleforge_continuous_metrics_t pending_metrics{};
            check_equal(ruleforge_continuous_get_metrics(
                             json_stream_session, &pending_metrics), RULES_FORGE_OK);
            check_equal(pending_metrics.accepted_events, 0);
            check_equal(ruleforge_continuous_data_bind_stream_feed(
                             stream, json + json_split, std::strlen(json) - json_split),
                         RULES_FORGE_OK);
            result = nullptr;
            check_equal(ruleforge_continuous_data_bind_stream_finish(stream, &result),
                         RULES_FORGE_OK);
            verify_batch(json_stream_session, result);
            check_equal(ruleforge_continuous_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_equal(ruleforge_continuous_session_destroy(json_stream_session),
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
            check_equal(ruleforge_continuous_push_yaml_path(
                             yaml_complete, "Event", yaml,
                             yaml_path, "event_id", "event_time", "events", &result),
                         RULES_FORGE_OK);
            verify_batch(yaml_complete, result);
            check_equal(ruleforge_continuous_session_destroy(yaml_complete), RULES_FORGE_OK);

            auto yaml_stream_session = create_session();
            stream = nullptr;
            check_equal(ruleforge_continuous_data_bind_stream_yaml_path_create(
                             yaml_stream_session, "Event",
                             yaml_path, "event_id", "event_time", "events", &stream),
                         RULES_FORGE_OK);
            size_t yaml_split = std::strlen(yaml) / 2;
            check_equal(ruleforge_continuous_data_bind_stream_feed(
                             stream, yaml, yaml_split), RULES_FORGE_OK);
            check_equal(ruleforge_continuous_data_bind_stream_feed(
                             stream, yaml + yaml_split, std::strlen(yaml) - yaml_split),
                         RULES_FORGE_OK);
            result = nullptr;
            check_equal(ruleforge_continuous_data_bind_stream_finish(stream, &result),
                         RULES_FORGE_OK);
            verify_batch(yaml_stream_session, result);
            check_equal(ruleforge_continuous_data_bind_stream_destroy(stream),
                         RULES_FORGE_OK);
            check_equal(ruleforge_continuous_session_destroy(yaml_stream_session),
                         RULES_FORGE_OK);

            char const* csv =
                "event_id_s,event_time_n,value_n,region_s\n"
                "csv-1,200,7,west\ncsv-2,201,9,west\ncsv-3,202,11,east\n";
            char const* csv_path = "region == \"west\"";
            auto csv_complete = create_session();
            result = nullptr;
            check_equal(ruleforge_continuous_push_csv_path(
                             csv_complete, "Event", csv,
                             csv_path, "event_id", "event_time", "events", &result),
                         RULES_FORGE_OK);
            verify_batch(csv_complete, result);
            check_equal(ruleforge_continuous_session_destroy(csv_complete), RULES_FORGE_OK);

            auto csv_stream_session = create_session();
            stream = nullptr;
            check_equal(ruleforge_continuous_data_bind_stream_csv_path_create(
                             csv_stream_session, "Event",
                             csv_path, "event_id", "event_time", "events", &stream),
                         RULES_FORGE_OK);
            size_t csv_split = std::strlen(csv) / 2;
            check_equal(ruleforge_continuous_data_bind_stream_feed(stream, csv, csv_split),
                         RULES_FORGE_OK);
            check_equal(ruleforge_continuous_get_metrics(
                             csv_stream_session, &pending_metrics), RULES_FORGE_OK);
            check_equal(pending_metrics.accepted_events, 0);
            check_equal(ruleforge_continuous_data_bind_stream_feed(
                             stream, csv + csv_split, std::strlen(csv) - csv_split),
                         RULES_FORGE_OK);
            result = nullptr;
            check_equal(ruleforge_continuous_data_bind_stream_finish(stream, &result),
                         RULES_FORGE_OK);
            verify_batch(csv_stream_session, result);
            check_equal(ruleforge_continuous_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_equal(ruleforge_continuous_session_destroy(csv_stream_session),
                         RULES_FORGE_OK);

            char const* xml =
                "<root><events>"
                "<event><event_id>xml-1</event_id><event_time>300</event_time><value>7</value><region>west</region></event>"
                "<event><event_id>xml-2</event_id><event_time>301</event_time><value>9</value><region>west</region></event>"
                "</events><ignored><event><event_id>xml-3</event_id><event_time>302</event_time><value>11</value><region>east</region></event></ignored></root>";
            char const* xml_path = "/root/events/event";
            auto xml_complete = create_session();
            result = nullptr;
            check_equal(ruleforge_continuous_push_xml_path(
                             xml_complete, "Event", xml,
                             xml_path, "event_id", "event_time", "events", &result),
                         RULES_FORGE_OK);
            verify_batch(xml_complete, result);
            check_equal(ruleforge_continuous_session_destroy(xml_complete), RULES_FORGE_OK);

            auto xml_stream_session = create_session();
            stream = nullptr;
            check_equal(ruleforge_continuous_data_bind_stream_xml_path_create(
                             xml_stream_session, "Event",
                             xml_path, "event_id", "event_time", "events", &stream),
                         RULES_FORGE_OK);
            size_t xml_split = std::strlen(xml) / 2;
            check_equal(ruleforge_continuous_data_bind_stream_feed(stream, xml, xml_split),
                         RULES_FORGE_OK);
            check_equal(ruleforge_continuous_get_metrics(
                             xml_stream_session, &pending_metrics), RULES_FORGE_OK);
            check_equal(pending_metrics.accepted_events, 0);
            check_equal(ruleforge_continuous_data_bind_stream_feed(
                             stream, xml + xml_split, std::strlen(xml) - xml_split),
                         RULES_FORGE_OK);
            result = nullptr;
            check_equal(ruleforge_continuous_data_bind_stream_finish(stream, &result),
                         RULES_FORGE_OK);
            verify_batch(xml_stream_session, result);
            check_equal(ruleforge_continuous_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_equal(ruleforge_continuous_session_destroy(xml_stream_session),
                         RULES_FORGE_OK);

            auto invalid_metadata_session = create_session();
            stream = nullptr;
            check_equal(ruleforge_continuous_data_bind_stream_json_path_create(
                             invalid_metadata_session, "Event",
                             "$[*]", "event_id", "event_time", "events", &stream),
                         RULES_FORGE_OK);
            char const* invalid_metadata_json =
                R"([{"event_id":"","event_time":400,"value":7,"region":"west"}])";
            check_equal(ruleforge_continuous_data_bind_stream_feed(
                             stream, invalid_metadata_json, std::strlen(invalid_metadata_json)),
                         RULES_FORGE_ERROR_INVALID_ARGUMENT);
            ruleforge_continuous_metrics_t failed_metrics{};
            check_equal(ruleforge_continuous_get_metrics(
                             invalid_metadata_session, &failed_metrics), RULES_FORGE_OK);
            check_equal(failed_metrics.accepted_events, 0);
            result = reinterpret_cast<ruleforge_continuous_result_t>(stream);
            check_equal(ruleforge_continuous_data_bind_stream_finish(stream, &result),
                         RULES_FORGE_ERROR_INVALID_ARGUMENT);
            check_null(result);
            check_equal(ruleforge_continuous_data_bind_stream_destroy(stream), RULES_FORGE_OK);
            check_equal(ruleforge_continuous_session_destroy(invalid_metadata_session),
                         RULES_FORGE_OK);

            check_equal(ruleforge_kb_destroy(kb), RULES_FORGE_OK);
            std::filesystem::remove(schema_path);
            ruleforge_cleanup();
        }
    }
}
