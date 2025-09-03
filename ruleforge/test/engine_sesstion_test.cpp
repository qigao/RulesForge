#include "catch2/catch_all.hpp"
#include "drools_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

#include <catch2/catch_approx.hpp>

// Test fixture to build a stateful session for each test case.
struct TestFixture {
    std::unique_ptr<StatefulSession> session;

    void build_session(std::string const& drl) {
        ParsingResult result;
        auto kb = build_knowledge_base(drl, result);
        if (!result.success) {
            for (auto const& err : result.errors) { FAIL(err.to_string()); }
        }
        REQUIRE(result.success);
        REQUIRE(kb != nullptr);
        session = kb->create_session();
        REQUIRE(session != nullptr);
    }
};

TEST_CASE_METHOD(TestFixture, "Engine: Simple Rule Fire", "[engine]") {
    build_session(R"(
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
            drools.insert({type: "Adult", name: $p.name});
        end
    )");

    auto person = std::make_shared<Fact>();
    person->type = "com.example.testing.Person";
    person->fields["name"] = "John";
    person->fields["age"] = (int64_t)25;

    session->add_fact(person);
    int fired = session->fire_all_rules();

    CHECK(fired == 1);
    CHECK(session->get_fact_count() == 2);
}

// Helper function to create test KB - outside of any class
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
            drools.insert({type: "Adult", name: $p.name});
        end

        query "findAdults"(NameParam $param)
            $a: Adult(name == $param.name)
        end
    )";

    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    if (!result.success) {
        for (auto const& err : result.errors) { 
            FAIL(err.to_string());
        }
    }
    REQUIRE(result.success);
    REQUIRE(kb != nullptr);
    return kb;
}

struct SessionTestFixture {
    std::shared_ptr<Fact> make_person(std::string const& name, int age) {
        auto fact = std::make_shared<Fact>();
        fact->type = "com.example.testing.Person";
        fact->fields["name"] = name;
        fact->fields["age"] = static_cast<int64_t>(age);
        return fact;
    }
};

TEST_CASE_METHOD(SessionTestFixture, "StatefulSession: Rule Firing and Queries", "[engine][session]") {
    auto kb = create_test_kb();
    std::unique_ptr<StatefulSession> session = kb->create_session();
    REQUIRE(session != nullptr);

    SECTION("Rule firing inserts a new fact") {
        CHECK(session->get_fact_count() == 0);

        session->add_fact(make_person("John", 30));
        session->add_fact(make_person("Amy", 15));
        CHECK(session->get_fact_count() == 2);

        int fired_count = session->fire_all_rules();
        CHECK(fired_count == 1);
        CHECK(session->get_fact_count() == 3);   // 2 Person + 1 Adult
    }

    SECTION("Querying finds facts inserted by rules") {
        session->add_fact(make_person("John", 30));
        session->fire_all_rules();
        REQUIRE(session->get_fact_count() == 2);

        auto query_arg_fact = std::make_shared<Fact>();
        query_arg_fact->type = "com.example.testing.NameParam";
        query_arg_fact->fields["name"] = "John";

        QueryResult query_results = session->execute_query("findAdults", {query_arg_fact});

        REQUIRE(query_results.size() == 1);

        QueryResultRow row = query_results.single();

        std::optional<std::string> adult_name = row.getFieldAs<std::string>("$a", "name");

        REQUIRE(adult_name.has_value());
        CHECK(adult_name.value() == "John");
    }
}

TEST_CASE_METHOD(SessionTestFixture, "StatefulSession: Isolation between sessions", "[engine][session]") {
    auto kb = create_test_kb();
    std::unique_ptr<StatefulSession> session1 = kb->create_session();
    std::unique_ptr<StatefulSession> session2 = kb->create_session();
    REQUIRE(session1 != nullptr);
    REQUIRE(session2 != nullptr);

    session1->add_fact(make_person("Alice", 40));
    CHECK(session1->get_fact_count() == 1);
    CHECK(session2->get_fact_count() == 0);

    int fired1 = session1->fire_all_rules();
    CHECK(fired1 == 1);
    CHECK(session1->get_fact_count() == 2);
    CHECK(session2->get_fact_count() == 0);

    session2->add_fact(make_person("Bob", 12));
    CHECK(session1->get_fact_count() == 2);
    CHECK(session2->get_fact_count() == 1);

    int fired2 = session2->fire_all_rules();
    CHECK(fired2 == 0);
    CHECK(session1->get_fact_count() == 2);
    CHECK(session2->get_fact_count() == 1);
}

TEST_CASE("StatefulSession: Query with no parameters", "[engine][session][query]") {
    std::string drl = R"(
        declare Item name:String end
        query "GetAllItems" $i: Item() end
    )";
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    REQUIRE(result.success);

    auto session = kb->create_session();

    auto item1 = std::make_shared<Fact>();
    item1->type = "Item";   // No package, so unqualified name is correct
    item1->fields["name"] = "Anvil";

    auto item2 = std::make_shared<Fact>();
    item2->type = "Item";
    item2->fields["name"] = "Rocket";

    session->add_fact(item1);
    session->add_fact(item2);

    QueryResult query_results = session->execute_query("GetAllItems");
    REQUIRE(query_results.size() == 2);

    std::vector<std::string> item_names = query_results.getColumnFieldAs<std::string>("$i", "name");

    REQUIRE_THAT(item_names, Catch::Matchers::UnorderedEquals(std::vector<std::string>{"Anvil", "Rocket"}));
}
