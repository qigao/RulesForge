// PROD-001: Test for JavaScript execution timeout
#include "tinytest.h"
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"
#include "rfl_js_manager.hpp"

suite("Engine JS Execution Timeout") {
    group("Timeout handling") {
        it("throws JSExecutionTimeoutException on infinite loop") {
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

            check(result.success);
            check(kb != nullptr);

            auto session = kb->create_session();
            check(session != nullptr);

            auto fact = std::make_shared<Fact>();
            fact->type = "test.timeout.TestFact";
            fact->fields["value"] = (int64_t)42;
            session->add_fact(fact);

            bool timeout_occurred = false;
            std::string rule_name;

            try {
                session->fire_all_rules();
            } catch (JSExecutionTimeoutException const& e) {
                timeout_occurred = true;
                rule_name = e.get_rule_name();
                info((std::string("Timeout exception: ") + e.what()).c_str());
            } catch (std::exception const& e) {
                check(false, (std::string("Unexpected exception: ") + e.what()).c_str());
            }

            check(timeout_occurred);
            check(rule_name == "Infinite Loop");
        }
    }

    group("Normal execution") {
        it("completes without timeout") {
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

            check(result.success);
            check(kb != nullptr);

            auto session = kb->create_session();
            check(session != nullptr);

            auto fact = std::make_shared<Fact>();
            fact->type = "test.timeout.TestFact";
            fact->fields["value"] = (int64_t)42;
            session->add_fact(fact);

            int fired = 0;
            bool no_exception = true;
            try {
                fired = session->fire_all_rules();
            } catch (...) {
                no_exception = false;
            }
            check(no_exception);
            check(fired == 1);
        }
    }
}
