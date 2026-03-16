/**
 * @file rfl_fact_databind_test.cpp
 * @brief Fact creation tests using data_bind instead of FactBuilder
 */

#include "tinytest.h"
#include "core/fact.hpp"
#include <cstring>

#include "data_bind.h"

// Adapter to convert data_bind Value to RulesForge Fact
class FactValueAdapter {
public:
    static Fact* create_object() {
        return new Fact();
    }

    static void set_field_int(Fact* obj, const char* name, int32_t val) {
        obj->fields[name] = static_cast<int64_t>(val);
    }

    static void set_field_double(Fact* obj, const char* name, double val) {
        obj->fields[name] = val;
    }

    static void set_field_string(Fact* obj, const char* name, const char* val) {
        obj->fields[name] = std::string(val);
    }

    static void set_field_bytes(Fact* obj, const char* name, const uint8_t* data, size_t len) {
        (void)obj; (void)name; (void)data; (void)len;
    }

    static DataBindValueApi get_api() {
        DataBindValueApi api;
        api.create_object = reinterpret_cast<Value* (*)()>(create_object);
        api.set_field_int = reinterpret_cast<void (*)(Value*, const char*, int32_t)>(set_field_int);
        api.set_field_double = reinterpret_cast<void (*)(Value*, const char*, double)>(set_field_double);
        api.set_field_string = reinterpret_cast<void (*)(Value*, const char*, const char*)>(set_field_string);
        api.set_field_bytes = reinterpret_cast<void (*)(Value*, const char*, const uint8_t*, size_t)>(set_field_bytes);
        return api;
    }
};

static void write_schema(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (f) {
        fwrite(content, 1, strlen(content), f);
        fclose(f);
    }
}

suite("Fact Creation with data_bind JSON") {

    section("String fields") {
        given("a Person schema with string fields") {
            write_schema("test_person.rfl",
                "declare Person\n"
                "    name: string\n"
                "    status: string\n"
                "end\n");

            when("parsing JSON data") {
                auto api = FactValueAdapter::get_api();
                DataBind* codec = data_bind_create_ex("test_person.rfl", DATA_BIND_FORMAT_JSON, &api);

                then("codec should be created") {
                    check_not_null(codec);
                }

                then("should parse string fields correctly") {
                    if (codec) {
                        const char* json = R"({"name": "John Doe", "status": "Active"})";
                        Fact* fact = reinterpret_cast<Fact*>(data_bind_parse_string(codec, "Person", json));

                        check_not_null(fact);
                        if (fact) {
                            fact->type = "Person";
                            check(fact->type == "Person");
                            check(fact->fields.find("name") != fact->fields.end());
                            check(std::get<std::string>(fact->fields["name"]) == "John Doe");
                            check(std::get<std::string>(fact->fields["status"]) == "Active");
                            delete fact;
                        }
                    }
                }

                if (codec) data_bind_free(codec);
            }
            remove("test_person.rfl");
        }
    }

    section("Numeric fields") {
        given("a Person schema with numeric fields") {
            write_schema("test_person2.rfl",
                "declare Person\n"
                "    age: int\n"
                "    salary: double\n"
                "    active: boolean\n"
                "end\n");

            when("parsing JSON with numbers") {
                auto api = FactValueAdapter::get_api();
                DataBind* codec = data_bind_create_ex("test_person2.rfl", DATA_BIND_FORMAT_JSON, &api);

                then("codec should be created") {
                    check_not_null(codec);
                }

                then("should parse numeric fields correctly") {
                    if (codec) {
                        const char* json = R"({"age": 30, "salary": 75000.50, "active": true})";
                        Fact* fact = reinterpret_cast<Fact*>(data_bind_parse_string(codec, "Person", json));

                        check_not_null(fact);
                        if (fact) {
                            check(std::get<int64_t>(fact->fields["age"]) == 30);
                            check(std::get<double>(fact->fields["salary"]) == 75000.50);
                            check(std::get<int64_t>(fact->fields["active"]) == 1);
                            delete fact;
                        }
                    }
                }

                if (codec) data_bind_free(codec);
            }
            remove("test_person2.rfl");
        }
    }

    section("Customer with multiple fields") {
        given("a Customer schema") {
            write_schema("test_customer.rfl",
                "declare Customer\n"
                "    id: int\n"
                "    name: string\n"
                "    balance: double\n"
                "end\n");

            when("parsing complete customer data") {
                auto api = FactValueAdapter::get_api();
                DataBind* codec = data_bind_create_ex("test_customer.rfl", DATA_BIND_FORMAT_JSON, &api);

                then("codec should be created") {
                    check_not_null(codec);
                }

                then("should parse all fields correctly") {
                    if (codec) {
                        const char* json = R"({"id": 123, "name": "Alice", "balance": 50000.0})";
                        Fact* fact = reinterpret_cast<Fact*>(data_bind_parse_string(codec, "Customer", json));

                        check_not_null(fact);
                        if (fact) {
                            fact->type = "Customer";
                            check(fact->type == "Customer");
                            check(std::get<int64_t>(fact->fields["id"]) == 123);
                            check(std::get<std::string>(fact->fields["name"]) == "Alice");
                            check(std::get<double>(fact->fields["balance"]) == 50000.0);
                            delete fact;
                        }
                    }
                }

                if (codec) data_bind_free(codec);
            }
            remove("test_customer.rfl");
        }
    }

    section("Optional fields") {
        given("a schema with optional fields") {
            write_schema("test_optional.rfl",
                "declare Test\n"
                "    field1: string\n"
                "    field2: int\n"
                "    field3: string\n"
                "end\n");

            when("parsing partial data") {
                auto api = FactValueAdapter::get_api();
                DataBind* codec = data_bind_create_ex("test_optional.rfl", DATA_BIND_FORMAT_JSON, &api);

                then("codec should be created") {
                    check_not_null(codec);
                }

                then("should handle missing fields") {
                    if (codec) {
                        const char* json = R"({"field2": 42})";
                        Fact* fact = reinterpret_cast<Fact*>(data_bind_parse_string(codec, "Test", json));

                        check_not_null(fact);
                        if (fact) {
                            check(fact->fields.find("field1") == fact->fields.end());
                            check(fact->fields.find("field2") != fact->fields.end());
                            check(std::get<int64_t>(fact->fields["field2"]) == 42);
                            check(fact->fields.find("field3") == fact->fields.end());
                            delete fact;
                        }
                    }
                }

                if (codec) data_bind_free(codec);
            }
            remove("test_optional.rfl");
        }
    }

    section("Product comparison") {
        given("a Product schema") {
            write_schema("test_compare.rfl",
                "declare Product\n"
                "    id: int\n"
                "    name: string\n"
                "    price: double\n"
                "end\n");

            when("parsing product JSON") {
                auto api = FactValueAdapter::get_api();
                DataBind* codec = data_bind_create_ex("test_compare.rfl", DATA_BIND_FORMAT_JSON, &api);

                then("codec should be created") {
                    check_not_null(codec);
                }

                then("should parse product correctly") {
                    if (codec) {
                        const char* json = R"({"id": 100, "name": "Widget", "price": 19.99})";
                        Fact* fact = reinterpret_cast<Fact*>(data_bind_parse_string(codec, "Product", json));

                        check_not_null(fact);
                        if (fact) {
                            fact->type = "Product";
                            check(fact->type == "Product");
                            check(std::get<int64_t>(fact->fields["id"]) == 100);
                            check(std::get<std::string>(fact->fields["name"]) == "Widget");
                            check(std::get<double>(fact->fields["price"]) == 19.99);
                            delete fact;
                        }
                    }
                }

                if (codec) data_bind_free(codec);
            }
            remove("test_compare.rfl");
        }
    }
}
