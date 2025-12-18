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

TEST_CASE("Simple Binding Test", "[jmespath]") {
    auto session = build_session(R"(
        declare TestFact
            name: String
        end
        rule "Simple Test"
        when
            $fact : TestFact(name == "test")
        then
            console.log("About to test jmespath function...");
            console.log("Type of jmespath:", typeof jmespath);
            
            if (typeof jmespath === 'function') {
                console.log("jmespath is a function - calling it now...");
                var result = jmespath('{"test": 42}', 'test');
                console.log("jmespath call completed, result:", result);
            } else {
                console.log("jmespath is NOT a function, it is:", typeof jmespath);
            }
        end
    )");

    auto fact = std::make_shared<Fact>();
    fact->type = "TestFact";
    fact->fields["name"] = std::string("test");

    session->add_fact(fact);
    int fired = session->fire_all_rules();

    CHECK(fired == 1);
}

