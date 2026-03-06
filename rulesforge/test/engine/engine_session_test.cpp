#include "parser/rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "tinytest.h"

struct TestFixture {
    std::unique_ptr<StatefulSession> session;

    void build_session(std::string const& drl) {
        ParsingResult result;
        auto kb = build_knowledge_base(drl, result);
        if (!result.success) {
            for (auto const& err : result.errors) {
                throw std::runtime_error("RFL parsing failed: " + err.to_string());
            }
            throw std::runtime_error("RFL parsing failed: Unknown error");
        }
        if (!kb) { throw std::runtime_error("KnowledgeBase is null"); }
        session = kb->create_session();
        if (!session) { throw std::runtime_error("Session is null"); }
    }
};

static std::shared_ptr<KnowledgeBase> create_test_kb() {
    std::string drl = R"(
        package com.example.testing;

        declare Person
            name: String
            age: int
        end
        declare Adult
            name: String
        end
        declare NameParam
            name: String
        end

        rule "Find Adults"
        when
            $p : Person(age >= 18)
        then
            insert Adult { name = $p.name }
        end

        query "findAdults"(NameParam $param)
            $a: Adult(name == $param.name)
        end
    )";

    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    if (!result.success) {
        for (auto const& err : result.errors) {
            throw std::runtime_error("RFL parsing failed: " + err.to_string());
        }
        throw std::runtime_error("RFL parsing failed: Unknown error");
    }
    if (!kb) { throw std::runtime_error("KnowledgeBase is null"); }
    return kb;
}

struct SessionTestFixture {
    std::vector<std::shared_ptr<Fact>> facts_;

    std::shared_ptr<Fact> make_person(std::string const& name, int age) {
        auto fact = std::make_shared<Fact>();
        fact->type = "com.example.testing.Person";
        fact->fields["name"] = name;
        fact->fields["age"] = static_cast<int64_t>(age);
        facts_.push_back(fact);
        return fact;
    }
};

suite("Engine Session") {
    group("Simple Rule Fire") {
        it("fires rule and inserts fact") {
            TestFixture fixture;
            fixture.build_session(R"(
                package com.example.testing;
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
            person->type = "com.example.testing.Person";
            person->fields["name"] = "John";
            person->fields["age"] = (int64_t)25;

            fixture.session->add_fact(person);
            int fired = fixture.session->fire_all_rules();

            check(fired == 1);
            check(fixture.session->get_fact_count() == 2);
        }
    }

    group("Rule Firing and Queries") {
        it("rule firing inserts a new fact") {
            SessionTestFixture fixture;
            auto kb = create_test_kb();
            std::unique_ptr<StatefulSession> session = kb->create_session();
            check(session != nullptr);

            check(session->get_fact_count() == 0);

            session->add_fact(fixture.make_person("John", 30));
            session->add_fact(fixture.make_person("Amy", 15));
            check(session->get_fact_count() == 2);

            int fired_count = session->fire_all_rules();
            check(fired_count == 1);
            check(session->get_fact_count() == 3);
        }

        it("querying finds facts inserted by rules") {
            SessionTestFixture fixture;
            auto kb = create_test_kb();
            std::unique_ptr<StatefulSession> session = kb->create_session();
            check(session != nullptr);

            session->add_fact(fixture.make_person("John", 30));
            session->fire_all_rules();
            check(session->get_fact_count() == 2);

            auto query_arg_fact = std::make_shared<Fact>();
            query_arg_fact->type = "com.example.testing.NameParam";
            query_arg_fact->fields["name"] = "John";

            QueryResult query_results = session->execute_query("findAdults", {query_arg_fact.get()});

            check(query_results.size() == 1);

            QueryResultRow row = query_results.single();

            std::optional<std::string> adult_name = row.getFieldAs<std::string>("$a", "name");

            check(adult_name.has_value());
            check(adult_name.value() == "John");
        }
    }

    group("Isolation between sessions") {
        it("sessions are isolated from each other") {
            SessionTestFixture fixture;
            auto kb = create_test_kb();
            std::unique_ptr<StatefulSession> session1 = kb->create_session();
            std::unique_ptr<StatefulSession> session2 = kb->create_session();
            check(session1 != nullptr);
            check(session2 != nullptr);

            session1->add_fact(fixture.make_person("Alice", 40));
            check(session1->get_fact_count() == 1);
            check(session2->get_fact_count() == 0);

            int fired1 = session1->fire_all_rules();
            check(fired1 == 1);
            check(session1->get_fact_count() == 2);
            check(session2->get_fact_count() == 0);

            session2->add_fact(fixture.make_person("Bob", 12));
            check(session1->get_fact_count() == 2);
            check(session2->get_fact_count() == 1);

            int fired2 = session2->fire_all_rules();
            check(fired2 == 0);
            check(session1->get_fact_count() == 2);
            check(session2->get_fact_count() == 1);
        }
    }

    group("Query with no parameters") {
        it("executes query without parameters") {
            std::string drl = R"(
                declare Item name:String end
                query "GetAllItems" $i: Item() end
            )";
            ParsingResult result;
            auto kb = build_knowledge_base(drl, result);
            check(result.success);

            auto session = kb->create_session();

            auto item1 = std::make_shared<Fact>();
            item1->type = "Item";
            item1->fields["name"] = "Anvil";

            auto item2 = std::make_shared<Fact>();
            item2->type = "Item";
            item2->fields["name"] = "Rocket";

            session->add_fact(item1);
            session->add_fact(item2);

            QueryResult query_results = session->execute_query("GetAllItems");
            check(query_results.size() == 2);

            std::vector<std::string> item_names = query_results.getColumnFieldAs<std::string>("$i", "name");

            // Check both items are present (order may vary)
            bool has_anvil = std::find(item_names.begin(), item_names.end(), "Anvil") != item_names.end();
            bool has_rocket = std::find(item_names.begin(), item_names.end(), "Rocket") != item_names.end();
            check(has_anvil);
            check(has_rocket);
        }
    }

    group("Global Containers") {
        it("uses global List in RHS assignment and expression") {
            TestFixture fixture;
            fixture.build_session(R"(
                global List results

                declare Trigger
                    flag: int
                end

                declare Snapshot
                    items: List<int>
                    itemCount: int
                end

                query "findSnapshots"
                    $s: Snapshot()
                end

                rule "Use Global List"
                when
                    $t: Trigger(flag == 1)
                then
                    insert Snapshot { items = $results, itemCount = $results.size }
                end
            )");

            fixture.session->set_global("results", make_typed_list({int64_t(10), int64_t(20)}));

            auto trigger = std::make_shared<Fact>();
            trigger->type = "Trigger";
            trigger->fields["flag"] = int64_t(1);
            fixture.session->add_fact(trigger);

            int fired = fixture.session->fire_all_rules();
            check(fired == 1);

            QueryResult qr = fixture.session->execute_query("findSnapshots");
            check(qr.size() == 1);
            auto row = qr.single();
            auto snap_opt = row.get("$s");
            check(snap_opt.has_value());
            Fact* snap = *snap_opt;

            auto list_field = snap->get_field("items");
            check(list_field.has_value());
            auto list_ptr = std::get_if<std::shared_ptr<TypedList>>(&*list_field);
            check(list_ptr != nullptr);
            check(*list_ptr != nullptr);
            check((*list_ptr)->values.size() == 2);
            check(std::get<int64_t>((*list_ptr)->values[0]) == 10);
            check(std::get<int64_t>((*list_ptr)->values[1]) == 20);

            auto item_count = snap->get_field("itemCount");
            check(item_count.has_value());
            check(std::get<int64_t>(*item_count) == 2);
        }

        it("iterates global List in for loop") {
            TestFixture fixture;
            fixture.build_session(R"(
                global List results

                declare Trigger
                    flag: int
                end

                declare Result
                    value: int
                end

                query "findResults"
                    $r: Result()
                end

                rule "Iterate Global List"
                when
                    $t: Trigger(flag == 1)
                then
                    for $x in $results {
                        insert Result { value = $x.value }
                    }
                end
            )");

            fixture.session->set_global("results", make_typed_list({int64_t(3), int64_t(5), int64_t(8)}));

            auto trigger = std::make_shared<Fact>();
            trigger->type = "Trigger";
            trigger->fields["flag"] = int64_t(1);
            fixture.session->add_fact(trigger);

            int fired = fixture.session->fire_all_rules();
            check(fired == 1);

            QueryResult qr = fixture.session->execute_query("findResults");
            check(qr.size() == 3);
        }
    }
}
