#include "rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/rhs_executor.hpp"
#include "engine/stateful_session.hpp"
#include "rete/rete_node_query.hpp"
#include "expression_descriptor.hpp"
#include "test_helpers.hpp"
#include "tinytest.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

std::unique_ptr<StatefulSession> build_session(std::string const& drl) {
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    if (!result.success) throw_parse_failure(result);
    if (!kb) { throw std::runtime_error("KnowledgeBase is null"); }
    auto session = kb->create_session();
    if (!session) { throw std::runtime_error("Session is null"); }
    return session;
}

namespace {
char* duplicate_c_string(char const* value) {
    std::size_t const size = std::strlen(value) + 1;
    auto* result = static_cast<char*>(std::malloc(size));
    if (result == nullptr) {
        return nullptr;
    }
    std::memcpy(result, value, size);
    return result;
}

int native_predicate_equals_first_arg(void* ctx, int argc, char const** argv, char** out_result) {
    if (out_result == nullptr) {
        return 1;
    }
    auto const* expected = static_cast<char const*>(ctx);
    bool const matches = argc > 0 && argv != nullptr && argv[0] != nullptr
        && expected != nullptr && std::string(argv[0]) == expected;
    *out_result = duplicate_c_string(matches ? "true" : "false");
    return *out_result == nullptr ? 1 : 0;
}

int native_predicate_equals_two_args(void*, int argc, char const** argv, char** out_result) {
    if (out_result == nullptr) {
        return 1;
    }
    bool const matches = argc == 2
        && argv != nullptr
        && argv[0] != nullptr
        && argv[1] != nullptr
        && std::string(argv[0]) == argv[1];
    *out_result = duplicate_c_string(matches ? "true" : "false");
    return *out_result == nullptr ? 1 : 0;
}

int native_predicate_first_arg_is_raw_gold(void*, int argc, char const** argv, char** out_result) {
    if (out_result == nullptr) {
        return 1;
    }
    bool const matches = argc > 0
        && argv != nullptr
        && argv[0] != nullptr
        && std::string(argv[0]) == "gold";
    *out_result = duplicate_c_string(matches ? "true" : "false");
    return *out_result == nullptr ? 1 : 0;
}

int native_predicate_numeric_equals_two_args(void*, int argc, char const** argv, char** out_result) {
    if (out_result == nullptr) {
        return 1;
    }
    bool matches = false;
    if (argc == 2 && argv != nullptr && argv[0] != nullptr && argv[1] != nullptr) {
        char* lhs_end = nullptr;
        char* rhs_end = nullptr;
        double const lhs = std::strtod(argv[0], &lhs_end);
        double const rhs = std::strtod(argv[1], &rhs_end);
        matches = lhs_end != argv[0]
            && rhs_end != argv[1]
            && lhs_end != nullptr
            && rhs_end != nullptr
            && *lhs_end == '\0'
            && *rhs_end == '\0'
            && std::abs(lhs - rhs) < 0.000001;
    }
    *out_result = duplicate_c_string(matches ? "true" : "false");
    return *out_result == nullptr ? 1 : 0;
}

void check_build_fails_with(std::string const& drl, std::string const& expected_message) {
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    check(kb == nullptr);
    check(!result.success);
    check(!result.errors.empty());
    if (!result.errors.empty()) {
        check(result.errors.front().message.find(expected_message) != std::string::npos);
    }
}

} // namespace

suite("RFL Engine") {
    group("Execution mode selection") {
        it("creates sessions with the selected execution implementation") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Trigger end
                rule "noop"
                when
                    Trigger()
                then
                end
            )", result);
            if (!result.success) {
                throw_parse_failure(result);
            }

            kb->set_execution_mode(rulesforge::ExecutionMode::V2HighPerformance);
            auto high_performance_session = kb->create_session();
            check(high_performance_session->get_execution_mode() == "v2_high_performance");

            kb->set_execution_mode(rulesforge::ExecutionMode::V1Standard);
            auto standard_session = kb->create_session();
            check(standard_session->get_execution_mode() == "v1_standard");
        }
    }

    group("DataBind schema import") {
        it("builds rule-visible declarations from DataBind schema") {
            auto schema_path = std::filesystem::temp_directory_path()
                / "rulesforge_schema_import_engine_core.schema";
            {
                std::ofstream schema(schema_path, std::ios::binary);
                schema << "schema Market [id(7), version(1), byte_order(little)]; "
                          "enum Tier <uint8> { Bronze = 1; Silver = 2; } "
                          "message Customer { int32 age; Tier tier; list<string> tags; "
                          "map<string,int32> scores; string name; uuid id; }";
            }

            std::string const drl = std::string("import \"")
                + schema_path.generic_string()
                + R"(";
                    rule "adult customer"
                    when
                        $c : Customer(age >= 18, name == "Ada")
                    then
                    end
                )";

            ParsingResult result;
            auto kb = build_knowledge_base(drl, result, "schema_import_test.rfl");
            if (!result.success) {
                throw_parse_failure(result);
            }

            check(kb != nullptr);
            auto const& declarations = kb->get_parser_state().parsed_declarations;
            auto it = std::find_if(declarations.begin(), declarations.end(),
                                   [](ParsedDeclaration const& decl) {
                                       return decl.type_name == "Customer";
                                   });
            check(it != declarations.end());
            if (it != declarations.end()) {
                check(it->fields.size() == 6);
                check(it->fields[0].name == "age");
                check(it->fields[0].type == FT_Int);
                check(it->fields[1].name == "tier");
                check(it->fields[1].type == FT_Int);
                check(it->fields[2].name == "tags");
                check(it->fields[2].type == FT_List);
                check(it->fields[3].name == "scores");
                check(it->fields[3].type == FT_Map);
                check(it->fields[4].name == "name");
                check(it->fields[4].type == FT_String);
                check(it->fields[5].name == "id");
                check(it->fields[5].type == FT_Uuid);
            }
            check(kb->get_parser_state().parsed_enums.size() == 1);
        }

        it("allows schema input types with RFL internal derived facts") {
            auto schema_path = std::filesystem::temp_directory_path()
                / "rulesforge_schema_import_with_internal_decl.schema";
            {
                std::ofstream schema(schema_path, std::ios::binary);
                schema << "schema Market [id(17), version(1), byte_order(little)]; "
                          "message Customer { int32 age; string name; }";
            }

            std::string const drl = std::string("import schema \"")
                + schema_path.generic_string()
                + R"(";
                    declare Alert
                        message: String
                    end

                    rule "adult customer alert"
                    when
                        $c : Customer(age >= 18)
                    then
                        insert Alert { message = $c.name }
                    end

                    query "Alerts"
                        $a : Alert()
                    end
                )";

            ParsingResult result;
            auto kb = build_knowledge_base(drl, result, "schema_internal_decl_test.rfl");
            if (!result.success) {
                throw_parse_failure(result);
            }
            auto session = kb->create_session();

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["age"] = static_cast<int64_t>(42);
            customer->fields["name"] = std::string("Ada");
            session->add_fact(customer);

            check(session->fire_all_rules() == 1);
            auto alerts = session->execute_query("Alerts");
            check(alerts.size() == 1);
            if (!alerts.empty()) {
                auto message = alerts.single().getFieldAs<std::string>("$a", "message");
                check(message.has_value());
                check(*message == "Ada");
            }

            std::filesystem::remove(schema_path);
        }

        it("rejects RFL declarations that conflict with schema input types") {
            auto schema_path = std::filesystem::temp_directory_path()
                / "rulesforge_schema_import_conflict.schema";
            {
                std::ofstream schema(schema_path, std::ios::binary);
                schema << "schema Market [id(18), version(1), byte_order(little)]; "
                          "message Customer { int32 age; }";
            }

            std::string const drl = std::string("import \"")
                + schema_path.generic_string()
                + R"(";
                    declare Customer
                        status: String
                    end
                )";

            check_build_fails_with(drl, "conflicts with DataBind schema type 'Customer'");
            std::filesystem::remove(schema_path);
        }

    }

    group("Enums") {
        it("uses bare enum symbols as string-compatible rule values") {
            auto session = build_session(R"(
                enum Currency
                    CNY
                    USD
                end

                declare Price
                    currency: Currency
                end

                declare Result
                    currency: Currency
                end

                rule "Seed CNY"
                when
                    not Price(currency == CNY)
                then
                    insert Price { currency = CNY }
                end

                rule "Copy CNY"
                when
                    $price : Price(currency == CNY)
                    not Result(currency == CNY)
                then
                    insert Result { currency = $price.currency }
                end

                query "Results"
                    $result : Result()
                end
            )");

            check(session->fire_all_rules() == 2);
            auto results = session->execute_query("Results");
            check(results.size() == 1);
            if (!results.empty()) {
                auto currency = results.single().getFieldAs<std::string>("$result", "currency");
                check(currency.has_value());
                check(*currency == "CNY");
            }
        }
    }

    group("Simple Rule Fire") {
        it("fires rule and inserts fact") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                declare Adult
                    name: String
                end
                rule "Find Adults"
                when
                    $p : Person(age >= 18)
                then
                    insert Adult { name = $p.name }
                end
            )");

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = "John";
            person->fields["age"] = (int64_t)25;

            session->add_fact(person);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 1);
            check(session->get_fact_count() == 2);
            check(stats.cpp_action_plan_exec_count == 1);
            check(stats.cpp_action_plan_error_count == 0);
        }

        it("fires linear multi-action RHS through C++ action plan") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                declare Adult
                    name: String
                end
                declare Audit
                    name: String
                end
                rule "Find Adults With Audit"
                when
                    $p : Person(age >= 18)
                then
                    insert Adult { name = $p.name }
                    insert Audit { name = $p.name }
                end
            )");

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = "John";
            person->fields["age"] = (int64_t)25;

            session->add_fact(person);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 1);
            check(session->get_fact_count() == 3);
            check(stats.cpp_action_plan_exec_count == 1);
            check(stats.cpp_action_plan_error_count == 0);
            check(stats.expression_error_count == 0);
        }

        it("assigns bare fact references as fact ids through RHS value binding") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                declare Ref
                    factId: int
                end
                rule "Capture Fact Id"
                when
                    $p : Person(age >= 18)
                then
                    insert Ref { factId = $p }
                end
            )");

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = "John";
            person->fields["age"] = (int64_t)25;

            session->add_fact(person);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 1);
            check(stats.cpp_action_plan_exec_count == 1);
            check(stats.cpp_action_plan_error_count == 0);
            Fact* ref = nullptr;
            for (int64_t id = 1; id <= session->get_next_fact_id(); ++id) {
                Fact* fact = session->get_fact_by_id(id);
                if (fact && fact->type == "Ref") {
                    ref = fact;
                    break;
                }
            }
            check(ref != nullptr);
            check(std::holds_alternative<int64_t>(ref->fields["factId"]));
            check(std::get<int64_t>(ref->fields["factId"]) == person->id);
        }

        it("executes if branches with multiple commands through C++ action plan") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                    tier: String
                    processed: int
                end
                declare Audit
                    label: String
                end
                rule "Classify With Audit"
                when
                    $p : Person(age >= 18, processed == 0)
                then
                    if $p.age >= 65 {
                        update $p { tier = "senior", processed = 1 }
                        insert Audit { label = "senior" }
                    } else {
                        update $p { tier = "adult", processed = 1 }
                        insert Audit { label = "adult" }
                    }
                end
            )");

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = "Jane";
            person->fields["age"] = int64_t(70);
            person->fields["tier"] = std::string("");
            person->fields["processed"] = int64_t(0);

            session->add_fact(person);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 1);
            check(session->get_fact_count() == 2);
            check(std::get<std::string>(person->fields["tier"]) == "senior");
            check(std::get<int64_t>(person->fields["processed"]) == 1);
            check(stats.cpp_action_plan_exec_count == 1);
            check(stats.cpp_action_plan_error_count == 0);
            check(stats.condition_eval_us > 0);
        }

        it("executes else-if branches through C++ action plan") {
            auto session = build_session(R"(
                declare Item
                    price: int
                    tier: String
                end
                rule "Classify"
                when
                    $item : Item(tier == "")
                then
                    if $item.price > 100 {
                        update $item { tier = "premium" }
                    } else if $item.price > 50 {
                        update $item { tier = "standard" }
                    } else {
                        update $item { tier = "budget" }
                    }
                end
            )");

            auto premium = std::make_shared<Fact>();
            premium->type = "Item";
            premium->fields["price"] = int64_t(200);
            premium->fields["tier"] = std::string("");

            auto standard = std::make_shared<Fact>();
            standard->type = "Item";
            standard->fields["price"] = int64_t(75);
            standard->fields["tier"] = std::string("");

            auto budget = std::make_shared<Fact>();
            budget->type = "Item";
            budget->fields["price"] = int64_t(20);
            budget->fields["tier"] = std::string("");

            session->add_fact(premium);
            session->add_fact(standard);
            session->add_fact(budget);

            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 3);
            check(std::get<std::string>(premium->fields["tier"]) == "premium");
            check(std::get<std::string>(standard->fields["tier"]) == "standard");
            check(std::get<std::string>(budget->fields["tier"]) == "budget");
            check(stats.cpp_action_plan_exec_count == 3);
            check(stats.cpp_action_plan_error_count == 0);
            check(stats.condition_eval_us > 0);
        }

        it("executes single switch RHS through C++ action plan") {
            auto session = build_session(R"(
                declare Order
                    level: int
                    discount: int
                end
                rule "Switch Discount"
                when
                    $o : Order(discount == 0)
                then
                    switch $o.level {
                        case 1 {
                            update $o { discount = 10 }
                        }
                        case 2 {
                            update $o { discount = 20 }
                        }
                        default {
                            update $o { discount = 5 }
                        }
                    }
                end
            )");

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["level"] = int64_t(1);
            order->fields["discount"] = int64_t(0);

            auto second = std::make_shared<Fact>();
            second->type = "Order";
            second->fields["level"] = int64_t(2);
            second->fields["discount"] = int64_t(0);

            auto defaulted = std::make_shared<Fact>();
            defaulted->type = "Order";
            defaulted->fields["level"] = int64_t(3);
            defaulted->fields["discount"] = int64_t(0);

            session->add_fact(order);
            session->add_fact(second);
            session->add_fact(defaulted);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 3);
            check(std::get<int64_t>(order->fields["discount"]) == 10);
            check(std::get<int64_t>(second->fields["discount"]) == 20);
            check(std::get<int64_t>(defaulted->fields["discount"]) == 5);
            check(stats.cpp_action_plan_exec_count == 3);
            check(stats.cpp_action_plan_error_count == 0);
        }

        it("executes single while RHS through C++ action plan at runtime") {
            auto session = build_session(R"(
                declare Counter
                    count: int
                    limit: int
                end
                rule "While Counter"
                when
                    $c : Counter(count == 0)
                then
                    while $c.count < $c.limit {
                        update $c { count = $c.count + 1 }
                    }
                end
            )");

            auto counter = std::make_shared<Fact>();
            counter->type = "Counter";
            counter->fields["count"] = int64_t(0);
            counter->fields["limit"] = int64_t(2);

            session->add_fact(counter);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 1);
            check(std::get<int64_t>(counter->fields["count"]) == 2);
            check(stats.cpp_action_plan_exec_count >= 1);
            check(stats.cpp_action_plan_error_count == 0);
        }

        it("executes simple single for RHS through C++ action plan at runtime") {
            auto session = build_session(R"(
                declare Item
                    value: int
                end
                declare Container
                    items: List<Item>
                end
                declare Result
                    value: int
                end
                rule "For Results"
                when
                    $c : Container()
                then
                    for $item in $c.items {
                        insert Result { value = $item.value }
                    }
                end
            )");

            auto first = std::make_shared<Fact>();
            first->type = "Item";
            first->fields["value"] = int64_t(10);

            auto second = std::make_shared<Fact>();
            second->type = "Item";
            second->fields["value"] = int64_t(20);

            auto container = std::make_shared<Fact>();
            container->type = "Container";
            container->fields["items"] = FactList{{first.get(), second.get()}};

            session->add_fact(first);
            session->add_fact(second);
            session->add_fact(container);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 1);
            check(session->get_fact_count() == 5);
            std::vector<int64_t> result_values;
            for (int64_t id = 1; id <= session->get_next_fact_id(); ++id) {
                Fact* fact = session->get_fact_by_id(id);
                if (fact && fact->type == "Result") {
                    auto it = fact->fields.find("value");
                    if (it != fact->fields.end() && std::holds_alternative<int64_t>(it->second)) {
                        result_values.push_back(std::get<int64_t>(it->second));
                    }
                }
            }
            std::sort(result_values.begin(), result_values.end());
            check(result_values.size() == 2);
            check(result_values[0] == 10);
            check(result_values[1] == 20);
            check(stats.cpp_action_plan_exec_count >= 1);
            check(stats.cpp_action_plan_error_count == 0);
        }

        it("assigns scalar int double and string values through C++ expression evaluator") {
            auto session = build_session(R"(
                declare Order
                    quantity: int
                    unitPrice: double
                    total: double
                    label: String
                    count: int
                end
                rule "Calculate Order"
                when
                    $o : Order(count == 0)
                then
                    update $o {
                        total = $o.quantity * $o.unitPrice,
                        label = "calculated",
                        count = $o.quantity + 1
                    }
                end
            )");

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["quantity"] = int64_t(3);
            order->fields["unitPrice"] = 12.5;
            order->fields["total"] = 0.0;
            order->fields["label"] = std::string("");
            order->fields["count"] = int64_t(0);

            session->add_fact(order);
            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            check(fired == 1);
            check(std::holds_alternative<double>(order->fields["total"]));
            check(std::abs(std::get<double>(order->fields["total"]) - 37.5) < 0.0001);
            check(std::get<std::string>(order->fields["label"]) == "calculated");
            check(std::get<int64_t>(order->fields["count"]) == 4);
            check(stats.expression_non_scalar_error_count == 0);
        }

        it("assigns non-scalar globals directly without using the expression backend") {
            auto session = build_session(R"(
                global List results

                declare Trigger
                    flag: int
                end
                declare Snapshot
                    items: List<int>
                    itemCount: int
                end
                rule "Use Global List"
                when
                    $t : Trigger(flag == 1)
                then
                    insert Snapshot { items = $results, itemCount = $results.size }
                end
            )");

            session->set_global("results", make_typed_list({int64_t(10), int64_t(20)}));

            auto trigger = std::make_shared<Fact>();
            trigger->type = "Trigger";
            trigger->fields["flag"] = int64_t(1);
            session->add_fact(trigger);

            rulesforge::rhs_prof::reset_stats();
            int fired = session->fire_all_rules();
            auto stats = rulesforge::rhs_prof::get_stats();

            Fact* snapshot = nullptr;
            for (int64_t id = 1; id <= session->get_next_fact_id(); ++id) {
                Fact* fact = session->get_fact_by_id(id);
                if (fact && fact->type == "Snapshot") {
                    snapshot = fact;
                    break;
                }
            }

            check(fired == 1);
            check(snapshot != nullptr);
            auto list_field = snapshot->get_field("items");
            check(list_field.has_value());
            auto list_ptr = std::get_if<std::shared_ptr<TypedList>>(&*list_field);
            check(list_ptr != nullptr);
            check(*list_ptr != nullptr);
            check((*list_ptr)->values.size() == 2);
            auto item_count = snapshot->get_field("itemCount");
            check(item_count.has_value());
            check(std::get<int64_t>(*item_count) == 2);
            check(stats.expression_non_scalar_error_count == 0);
        }

        it("filters alpha int fields with non-equality comparison") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                rule "Find Adults"
                when
                    Person(age > 18)
                then
                end
            )");

            auto minor = std::make_shared<Fact>();
            minor->type = "Person";
            minor->fields["name"] = "Alice";
            minor->fields["age"] = (int64_t)18;

            auto adult = std::make_shared<Fact>();
            adult->type = "Person";
            adult->fields["name"] = "Bob";
            adult->fields["age"] = (int64_t)19;

            session->add_fact(minor);
            session->add_fact(adult);

            check(session->fire_all_rules() == 1);
        }

        it("fails alpha numeric comparison on runtime type mismatch") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                rule "Find Adults"
                when
                    Person(age > 18)
                then
                end
            )");

            auto mismatched = std::make_shared<Fact>();
            mismatched->type = "Person";
            mismatched->fields["name"] = "TypeMismatch";
            mismatched->fields["age"] = std::string("old");

            auto adult = std::make_shared<Fact>();
            adult->type = "Person";
            adult->fields["name"] = "Bob";
            adult->fields["age"] = int64_t(19);

            session->add_fact(mismatched);
            session->add_fact(adult);
            check(session->fire_all_rules() == 1);
        }

        it("matches alpha numeric comparison after field is modified to matching numeric value") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                rule "Find Adults"
                when
                    Person(age > 18)
                then
                end
            )");

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = "Pat";
            person->fields["age"] = int64_t(17);

            session->add_fact(person);
            check(session->fire_all_rules() == 0);

            session->update_fact(person.get(), [](Fact& fact) {
                fact.fields["age"] = int64_t(19);
            });

            check(session->fire_all_rules() == 1);
        }

        it("does not fire alpha numeric comparison after matching fact is retracted") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                rule "Find Adults"
                when
                    Person(age > 18)
                then
                end
            )");

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = "Retracted";
            person->fields["age"] = int64_t(21);

            session->add_fact(person);
            session->retract_fact(person);

            check(session->fire_all_rules() == 0);
        }

        it("filters alpha numeric comparison during batch assert") {
            auto session = build_session(R"(
                declare Person
                    name: String
                    age: int
                end
                rule "Find Adults"
                when
                    Person(age > 18)
                then
                end
            )");

            auto minor = std::make_shared<Fact>();
            minor->type = "Person";
            minor->fields["name"] = "Minor";
            minor->fields["age"] = int64_t(18);

            auto adult = std::make_shared<Fact>();
            adult->type = "Person";
            adult->fields["name"] = "Adult";
            adult->fields["age"] = int64_t(19);

            auto senior = std::make_shared<Fact>();
            senior->type = "Person";
            senior->fields["name"] = "Senior";
            senior->fields["age"] = int64_t(70);

            session->add_facts(std::vector<std::shared_ptr<Fact>>{minor, adult, senior});

            check(session->fire_all_rules() == 2);
        }

        it("filters alpha numeric comparison through PHREAK deferred flush") {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Person
                    name: String
                    age: int
                end
                rule "Find Adults"
                when
                    Person(age > 18)
                then
                end
            )", result);
            if (!result.success) throw_parse_failure(result);
            check(kb != nullptr);
            kb->set_phreak_experimental(true);

            auto session = kb->create_session();
            check(session != nullptr);

            auto minor = std::make_shared<Fact>();
            minor->type = "Person";
            minor->fields["name"] = "Minor";
            minor->fields["age"] = int64_t(18);

            auto adult = std::make_shared<Fact>();
            adult->type = "Person";
            adult->fields["name"] = "Adult";
            adult->fields["age"] = int64_t(19);

            session->add_facts(std::vector<std::shared_ptr<Fact>>{minor, adult});

            auto before_fire = session->runtime_counters();
            check(before_fire.phreak_dirty_segment_marks > 0);
            check(before_fire.phreak_dirty_path_marks > 0);

            check(session->fire_all_rules() == 1);

            auto after_fire = session->runtime_counters();
            check(after_fire.phreak_flush_iterations > before_fire.phreak_flush_iterations);
        }

        it("filters alpha mixed int and double numeric comparison") {
            auto session = build_session(R"(
                declare Score
                    value: int
                end
                rule "Find High Scores"
                when
                    Score(value > 10.5)
                then
                end
            )");

            auto low = std::make_shared<Fact>();
            low->type = "Score";
            low->fields["value"] = int64_t(10);

            auto high = std::make_shared<Fact>();
            high->type = "Score";
            high->fields["value"] = int64_t(11);

            session->add_fact(low);
            session->add_fact(high);

            check(session->fire_all_rules() == 1);
        }

        it("filters alpha double fields with non-equality comparison") {
            auto session = build_session(R"(
                declare Order
                    amount: double
                end
                rule "Find High Value Orders"
                when
                    Order(amount > 500.0)
                then
                end
            )");

            auto small = std::make_shared<Fact>();
            small->type = "Order";
            small->fields["amount"] = 500.0;

            auto large = std::make_shared<Fact>();
            large->type = "Order";
            large->fields["amount"] = 500.5;

            session->add_fact(small);
            session->add_fact(large);

            check(session->fire_all_rules() == 1);
        }

    group("Join Condition") {
        it("joins facts correctly") {
            auto session = build_session(R"(
                declare Customer
                    id: int
                    name: String
                end
                declare Order
                    customerId: int
                    item: String
                end
                rule "Find Customer Orders"
                when
                    $c : Customer($id : id)
                    $o : Order(customerId == $id)
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = (int64_t)123;
            customer->fields["name"] = "Acme Inc.";

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["customerId"] = (int64_t)123;
            order->fields["item"] = "Anvil";

            session->add_fact(customer);
            session->add_fact(order);

            check(session->fire_all_rules() == 1);
        }

        it("joins int fields with non-equality comparison") {
            auto session = build_session(R"(
                declare Customer
                    id: int
                end
                declare Order
                    customerId: int
                    item: String
                end
                rule "Find Later Customer Orders"
                when
                    $c : Customer($id : id)
                    $o : Order(customerId > $id)
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = (int64_t)123;

            auto later_order = std::make_shared<Fact>();
            later_order->type = "Order";
            later_order->fields["customerId"] = (int64_t)124;
            later_order->fields["item"] = "Anvil";

            auto same_order = std::make_shared<Fact>();
            same_order->type = "Order";
            same_order->fields["customerId"] = (int64_t)123;
            same_order->fields["item"] = "Hammer";

            session->add_fact(customer);
            session->add_fact(later_order);
            session->add_fact(same_order);

            check(session->fire_all_rules() == 1);
        }

        it("treats join numeric comparison type mismatch as non-match") {
            auto session = build_session(R"(
                declare Customer
                    id: int
                end
                declare Order
                    customerId: int
                end
                rule "Find Later Customer Orders"
                when
                    $c : Customer($id : id)
                    $o : Order(customerId > $id)
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = int64_t(123);

            auto mismatched_order = std::make_shared<Fact>();
            mismatched_order->type = "Order";
            mismatched_order->fields["customerId"] = std::string("later");

            auto later_order = std::make_shared<Fact>();
            later_order->type = "Order";
            later_order->fields["customerId"] = int64_t(124);

            session->add_fact(customer);
            session->add_fact(mismatched_order);
            session->add_fact(later_order);
            check(session->fire_all_rules() == 1);
        }

        it("joins mixed int and double numeric comparison") {
            auto session = build_session(R"(
                declare Customer
                    limit: int
                end
                declare Order
                    amount: double
                end
                rule "Find Orders Above Customer Limit"
                when
                    $c : Customer($limit : limit)
                    $o : Order(amount > $limit)
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["limit"] = int64_t(100);

            auto low_order = std::make_shared<Fact>();
            low_order->type = "Order";
            low_order->fields["amount"] = 100.0;

            auto high_order = std::make_shared<Fact>();
            high_order->type = "Order";
            high_order->fields["amount"] = 100.5;

            session->add_fact(customer);
            session->add_fact(low_order);
            session->add_fact(high_order);

            check(session->fire_all_rules() == 1);
        }

        it("joins double fields with non-equality comparison") {
            auto session = build_session(R"(
                declare Customer
                    limit: double
                end
                declare Order
                    amount: double
                end
                rule "Find Orders Above Customer Limit"
                when
                    $c : Customer($limit : limit)
                    $o : Order(amount > $limit)
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["limit"] = 500.0;

            auto high_order = std::make_shared<Fact>();
            high_order->type = "Order";
            high_order->fields["amount"] = 500.5;

            auto low_order = std::make_shared<Fact>();
            low_order->type = "Order";
            low_order->fields["amount"] = 500.0;

            session->add_fact(customer);
            session->add_fact(high_order);
            session->add_fact(low_order);

            check(session->fire_all_rules() == 1);
        }

        it("treats missing join fields as non-matches instead of synthetic nil") {
            auto session = build_session(R"(
                declare Pattern
                    expected: String
                end
                declare Document
                    text: String
                end
                rule "Find Expected Text"
                when
                    $p : Pattern($expected : expected)
                    $d : Document(text == $expected)
                then
                end
            )");

            auto pattern = std::make_shared<Fact>();
            pattern->type = "Pattern";
            pattern->fields["expected"] = "Gold";

            auto missing_expected = std::make_shared<Fact>();
            missing_expected->type = "Pattern";

            auto matching = std::make_shared<Fact>();
            matching->type = "Document";
            matching->fields["text"] = "Gold";

            session->add_fact(pattern);
            session->add_fact(missing_expected);
            session->add_fact(matching);

            check(session->fire_all_rules() == 1);
        }

        it("treats join numeric expression type mismatch as non-match") {
            auto session = build_session(R"(
                declare Customer
                    limit: double
                end
                declare Order
                    amount: double
                    multiplier: double
                end
                rule "Find Orders Above Adjusted Customer Limit"
                when
                    $c : Customer($limit : limit)
                    $o : Order(amount > ($c.limit * $multiplier))
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["limit"] = 100.0;

            auto mismatched = std::make_shared<Fact>();
            mismatched->type = "Order";
            mismatched->fields["amount"] = 250.0;
            mismatched->fields["multiplier"] = std::string("2.0");

            session->add_fact(customer);
            session->add_fact(mismatched);
            check(session->fire_all_rules() == 0);
        }

        it("treats missing join expression fields as non-matches") {
            auto session = build_session(R"(
                declare Customer
                    limit: double
                end
                declare Order
                    amount: double
                    multiplier: double
                end
                rule "Find Orders Above Adjusted Customer Limit"
                when
                    $c : Customer($limit : limit)
                    $o : Order(amount > ($c.limit * $multiplier))
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["limit"] = 100.0;

            auto missing_multiplier = std::make_shared<Fact>();
            missing_multiplier->type = "Order";
            missing_multiplier->fields["amount"] = 250.0;

            auto matching = std::make_shared<Fact>();
            matching->type = "Order";
            matching->fields["amount"] = 250.0;
            matching->fields["multiplier"] = 2.0;

            session->add_fact(customer);
            session->add_fact(missing_multiplier);
            session->add_fact(matching);

            check(session->fire_all_rules() == 1);
        }
    }

    group("Conditional Join") {
        it("runs parameterized rule-side query call orchestration") {
            auto session = build_session(R"(
                declare Customer
                    id: int
                end
                declare Order
                    customerId: int
                end
                query "findOrders"(Customer $c)
                    $o : Order(customerId == $c.id)
                end
                rule "Parameterized Query Call Rule"
                when
                    $c : Customer()
                    "findOrders"($c)
                then
                end
            )");
            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = int64_t(7);
            session->add_fact(customer);
            check(session->fire_all_rules() == 0);

            auto unrelated_order = std::make_shared<Fact>();
            unrelated_order->type = "Order";
            unrelated_order->fields["customerId"] = int64_t(9);
            session->add_fact(unrelated_order);
            check(session->fire_all_rules() == 0);

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["customerId"] = int64_t(7);
            session->add_fact(order);
            check(session->fire_all_rules() == 1);
        }

        it("projects rule-side query result bindings into C++ RHS") {
            auto session = build_session(R"(
                declare Customer
                    id: int
                end
                declare Order
                    customerId: int
                end
                declare Result
                    customerId: int
                end
                query "findOrders"(Customer $c)
                    $o : Order(customerId == $c.id)
                end
                query "findResults"
                    $r : Result()
                end
                rule "Projected Query Call RHS Rule"
                when
                    $c : Customer()
                    "findOrders"($c)
                then
                    insert Result { customerId = $o.customerId }
                end
            )");
            check(session->get_knowledge_base()->rhs_backend_summary().cpp_action_plan_rule_count == 1);
            check(session->get_knowledge_base()->rhs_backend_summary().command_count == 1);

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = int64_t(7);

            auto matching_order_one = std::make_shared<Fact>();
            matching_order_one->type = "Order";
            matching_order_one->fields["customerId"] = int64_t(7);

            auto matching_order_two = std::make_shared<Fact>();
            matching_order_two->type = "Order";
            matching_order_two->fields["customerId"] = int64_t(7);

            auto unrelated_order = std::make_shared<Fact>();
            unrelated_order->type = "Order";
            unrelated_order->fields["customerId"] = int64_t(9);

            session->add_fact(customer);
            session->add_fact(matching_order_one);
            session->add_fact(matching_order_two);
            session->add_fact(unrelated_order);

            rulesforge::rhs_prof::reset_stats();
            check(session->fire_all_rules() == 2);
            auto stats = rulesforge::rhs_prof::get_stats();
            check(stats.cpp_action_plan_exec_count == 2);
            check(stats.cpp_action_plan_error_count == 0);

            auto results = session->execute_query("findResults");
            check(results.success());
            check(results.size() == 2);
            for (auto row : results) {
                auto result_fact = row.get("$r");
                check(result_fact.has_value());
                check(*result_fact != nullptr);
                auto customer_id = (*result_fact)->get_field("customerId");
                check(customer_id.has_value());
                check(std::holds_alternative<int64_t>(*customer_id));
                check(std::get<int64_t>(*customer_id) == 7);
            }
        }
    }

    group("Alpha modify propagation") {
        it("does not let unrelated field updates bypass alpha constraints") {
            auto session = build_session(R"(
                declare Order
                    quantity: int
                    unitPrice: double
                    finalPrice: double
                end

                rule "No Discount"
                    no-loop
                when
                    $order : Order(quantity >= 1, quantity <= 2, finalPrice <= 0.01)
                then
                    update $order { finalPrice = $order.quantity * $order.unitPrice }
                end

                rule "Round Down Payment"
                    salience -10
                    no-loop
                when
                    $order : Order(quantity > 2, finalPrice > 0.01)
                then
                    update $order { finalPrice = floor($order.finalPrice) }
                end
            )");

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["quantity"] = static_cast<int64_t>(1);
            order->fields["unitPrice"] = 25.99;
            order->fields["finalPrice"] = 0.01;

            session->add_fact(order);

            check(session->fire_all_rules() == 1);
            check(std::holds_alternative<double>(order->fields["finalPrice"]));
            check(std::abs(std::get<double>(order->fields["finalPrice"]) - 25.99) < 0.0001);
        }

        it("reevaluates numeric expression constraints on modify paths") {
            auto session = build_session(R"(
                declare Order
                    quantity: int
                    unitPrice: double
                    finalPrice: double
                    note: String
                end

                rule "Touch Matching Order"
                when
                    $order : Order(finalPrice <= ($quantity * $unitPrice))
                then
                end
            )");

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["quantity"] = int64_t(2);
            order->fields["unitPrice"] = 10.0;
            order->fields["finalPrice"] = 25.0;
            order->fields["note"] = "initial";

            session->add_fact(order);
            check(session->fire_all_rules() == 0);

            session->update_fact(order.get(), [](Fact& fact) {
                fact.fields["finalPrice"] = 20.0;
            });

            check(session->fire_all_rules() == 1);
        }

    }

    group("'not' Pattern") {
        it("reindexes equality not matches after modify and retract") {
            auto session = build_session(R"(
                declare Customer id:int end
                declare Block customerId:int end
                rule "Available"
                when
                    $customer : Customer($id : id)
                    not (Block(customerId == $id))
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = int64_t(1);
            session->add_fact(customer);
            check(session->fire_all_rules() == 1);

            auto block = std::make_shared<Fact>();
            block->type = "Block";
            block->fields["customerId"] = int64_t(1);
            session->add_fact(block);
            check(session->fire_all_rules() == 0);

            session->update_fact(block.get(), [](Fact& fact) {
                fact.fields["customerId"] = int64_t(2);
            });
            check(session->fire_all_rules() == 1);

            session->update_fact(block.get(), [](Fact& fact) {
                fact.fields["customerId"] = int64_t(1);
            });
            check(session->fire_all_rules() == 0);

            session->retract_fact(block);
            check(session->fire_all_rules() == 1);
        }

        it("reindexes equality exists matches after modify and retract") {
            auto session = build_session(R"(
                declare Customer id:int end
                declare Block customerId:int end
                rule "Blocked"
                when
                    $customer : Customer($id : id)
                    exists (Block(customerId == $id))
                then
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = int64_t(1);
            session->add_fact(customer);
            check(session->fire_all_rules() == 0);

            auto block = std::make_shared<Fact>();
            block->type = "Block";
            block->fields["customerId"] = int64_t(1);
            session->add_fact(block);
            check(session->fire_all_rules() == 1);

            session->update_fact(block.get(), [](Fact& fact) {
                fact.fields["customerId"] = int64_t(2);
            });
            check(session->fire_all_rules() == 0);

            session->update_fact(block.get(), [](Fact& fact) {
                fact.fields["customerId"] = int64_t(1);
            });
            check(session->fire_all_rules() == 1);

            session->retract_fact(block);
            check(session->fire_all_rules() == 0);
        }
    }

    group("Salience") {
        it("respects rule salience order") {
            auto session = build_session(R"(
                declare Trigger end
                declare Result name:String end
                rule "High Salience" salience 10 when Trigger() then
                    insert Result { name = "High" }
                end
                rule "Low Salience" salience 5 when Trigger() then
                    insert Result { name = "Low" }
                end
            )");

            auto fact = std::make_shared<Fact>();
            fact->type = "Trigger";
            session->add_fact(fact);
            int fired = session->fire_all_rules();

            check(fired == 2);
            check(session->get_fact_count() == 3);
        }
    }

    group("Logical Insertions (TMS)") {
        it("retracts logical facts when support is removed") {
            auto session = build_session(R"(
                declare Alarm reason:String end
                declare Fire active:bool end
                rule "Sound Alarm on Fire"
                when
                    $f : Fire(active == true)
                then
                    insertLogical Alarm { reason = "fire" }
                end
            )");

            auto fire_fact = std::make_shared<Fact>(Fact{0, "Fire", {{"active", (int64_t)1}}});
            session->add_fact(fire_fact);
            session->fire_all_rules();

            check(session->get_fact_count() == 2);

            session->retract_fact(fire_fact);
            check(session->get_fact_count() == 0);
        }
    }

    group("'or' Condition") {
        it("fires due to Gold status") {
            auto session = build_session(R"(
                declare Customer
                    id : int
                    status : String
                end
                declare Order
                    customerId : int
                    amount : double
                end
                declare PremiumCustomer
                    id : int
                end

                rule "Identify Premium Customers"
                when
                    $c1 : Customer( status == "Gold" )
                    or
                    $c2 : Customer( $id : id )
                    Order( customerId == $id, amount > 500.0 )
                then
                    insert PremiumCustomer { }
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = (int64_t)1;
            customer->fields["status"] = "Gold";

            session->add_fact(customer);
            check(session->fire_all_rules() == 1);
            check(session->get_fact_count() == 2);
        }

        it("fires due to high-value order") {
            auto session = build_session(R"(
                declare Customer
                    id : int
                    status : String
                end
                declare Order
                    customerId : int
                    amount : double
                end
                declare PremiumCustomer
                    id : int
                end

                rule "Identify Premium Customers"
                when
                    $c1 : Customer( status == "Gold" )
                    or
                    $c2 : Customer( $id : id )
                    Order( customerId == $id, amount > 500.0 )
                then
                    insert PremiumCustomer { }
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = (int64_t)2;
            customer->fields["status"] = "Silver";

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["customerId"] = (int64_t)2;
            order->fields["amount"] = 600.0;

            session->add_fact(customer);
            session->add_fact(order);
            check(session->fire_all_rules() == 1);
            check(session->get_fact_count() == 3);
        }

        it("does not fire for non-premium") {
            auto session = build_session(R"(
                declare Customer
                    id : int
                    status : String
                end
                declare Order
                    customerId : int
                    amount : double
                end
                declare PremiumCustomer
                    id : int
                end

                rule "Identify Premium Customers"
                when
                    $c1 : Customer( status == "Gold" )
                    or
                    $c2 : Customer( $id : id )
                    Order( customerId == $id, amount > 500.0 )
                then
                    insert PremiumCustomer { }
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = (int64_t)3;
            customer->fields["status"] = "Bronze";

            auto order = std::make_shared<Fact>();
            order->type = "Order";
            order->fields["customerId"] = (int64_t)3;
            order->fields["amount"] = 100.0;

            session->add_fact(customer);
            session->add_fact(order);
            check(session->fire_all_rules() == 0);
            check(session->get_fact_count() == 2);
        }
    }
}
}
