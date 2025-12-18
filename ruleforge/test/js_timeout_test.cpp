// PROD-001: Test for JavaScript execution timeout
#include "catch2/catch_all.hpp"
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"
#include "rfl_js_manager.hpp"

TEST_CASE("Engine: JS Execution Timeout", "[engine][timeout][prod]") {
    // Rule with an infinite loop in the RHS
    std::string drl = R"(
package test.timeout

declare TestFact
    value: int
end

rule "Infinite Loop"
when
    $f : TestFact()
then
    // This will run forever without timeout protection
    while (true) {
        var x = 1 + 1;
    }
end
)";

    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);

    REQUIRE(result.success);
    REQUIRE(kb);

    auto session = kb->create_session();
    REQUIRE(session);

    auto fact = std::make_shared<Fact>();
    fact->type = "test.timeout.TestFact";
    fact->fields["value"] = (int64_t)42;
    session->add_fact(fact);

    // The rule should timeout - default is 5 seconds, but we can't wait that long in tests
    // This test verifies the mechanism works, actual timeout value would need adjustment
    // for production use

    // Note: This test may take up to 5 seconds (default timeout) to complete
    // In CI, consider using a shorter timeout or mocking

    SECTION("Timeout throws JSExecutionTimeoutException") {
        bool timeout_occurred = false;
        std::string rule_name;

        try {
            session->fire_all_rules();
        } catch (JSExecutionTimeoutException const& e) {
            timeout_occurred = true;
            rule_name = e.get_rule_name();
            INFO("Timeout exception: " << e.what());
        } catch (std::exception const& e) {
            FAIL("Unexpected exception: " << e.what());
        }

        CHECK(timeout_occurred);
        CHECK(rule_name == "Infinite Loop");
    }
}

TEST_CASE("Engine: JS Execution Without Timeout", "[engine][timeout][prod]") {
    // Rule with normal execution - should complete without timeout
    std::string drl = R"(
package test.timeout

declare TestFact
    value: int
end

rule "Normal Rule"
when
    $f : TestFact()
then
    console.log("Normal execution completed");
end
)";

    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);

    REQUIRE(result.success);
    REQUIRE(kb);

    auto session = kb->create_session();
    REQUIRE(session);

    auto fact = std::make_shared<Fact>();
    fact->type = "test.timeout.TestFact";
    fact->fields["value"] = (int64_t)42;
    session->add_fact(fact);

    // Normal execution should complete without exception
    int fired = 0;
    REQUIRE_NOTHROW(fired = session->fire_all_rules());
    CHECK(fired == 1);
}
