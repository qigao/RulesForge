#include "tinytest.h"
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"
#include "rfl_js_manager.hpp"

std::unique_ptr<StatefulSession> build_session(std::string const& drl) {
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    if (!result.success) {
        for (auto const& err : result.errors) {
            throw std::runtime_error("RFL parsing failed: " + err.to_string());
        }
        throw std::runtime_error("RFL parsing failed: Unknown error");
    }
    if (!kb) { throw std::runtime_error("KnowledgeBase is null"); }
    auto session = kb->create_session();
    if (!session) { throw std::runtime_error("Session is null"); }
    return session;
}

suite("JMESPath Simple") {
    it("executes minimal jmespath test") {
        auto session = build_session(R"(
            declare TestFact
                name: String
            end
            rule "Minimal JMESPath Test"
            when
                $fact : TestFact(name == "test")
            then
                console.log("=== JavaScript Execution Started ===");
                console.log("Type of jmespath:", typeof jmespath);

                if (typeof jmespath === 'function') {
                    console.log("jmespath is a function - testing it!");
                    var result = jmespath('{"test": 42}', 'test');
                    console.log("jmespath result:", result);
                } else {
                    console.log("jmespath is NOT a function");
                }

                console.log("=== JavaScript Execution Completed ===");
            end
        )");

        auto fact = std::make_shared<Fact>();
        fact->type = "TestFact";
        fact->fields["name"] = std::string("test");

        session->add_fact(fact);
        int fired = session->fire_all_rules();

        check(fired == 1);
    }
}
