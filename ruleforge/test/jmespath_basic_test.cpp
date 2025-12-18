#include "catch2/catch_all.hpp"
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"
#include "rfl_js_manager.hpp"

std::unique_ptr<StatefulSession> build_session(std::string const& drl) {
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    REQUIRE(result.success);
    for (auto const& err : result.errors) FAIL(err.to_string());
    REQUIRE(kb != nullptr);
    auto session = kb->create_session();
    REQUIRE(session != nullptr);
    return session;
}

TEST_CASE("JMESPath: Basic Property Access", "[jmespath]") {
    auto session = build_session(R"(
        declare TestFact
            name: String
        end
        rule "Test Basic Properties"
        when
            $fact : TestFact(name == "test")
        then
            var jsonData = '{"user": {"profile": {"age": 25, "name": "Alice"}}}';
            var age = jmespath(jsonData, 'user.profile.age');
            var name = jmespath(jsonData, 'user.profile.name');
            
            console.log("Age:", age);
            console.log("Name:", name);
        end
    )");

    auto fact = std::make_shared<Fact>();
    fact->type = "TestFact";
    fact->fields["name"] = std::string("test");

    session->add_fact(fact);
    int fired = session->fire_all_rules();

    CHECK(fired == 1);
}

TEST_CASE("JMESPath: Length Function", "[jmespath]") {
    auto session = build_session(R"(
        declare DataFact
            name: String
        end
        rule "Test Length Function"
        when
            $fact : DataFact(name == "test")
        then
            var jsonData = '{"items": [1, 2, 3, 4, 5], "orders": [{"id": 1}, {"id": 2}]}';
            var itemCount = jmespath(jsonData, 'items | length(@)');
            var orderCount = jmespath(jsonData, 'orders | length(@)');
            
            console.log("Item count:", itemCount);
            console.log("Order count:", orderCount);
        end
    )");

    auto fact = std::make_shared<Fact>();
    fact->type = "DataFact";
    fact->fields["name"] = std::string("test");

    session->add_fact(fact);
    int fired = session->fire_all_rules();

    CHECK(fired == 1);
}

