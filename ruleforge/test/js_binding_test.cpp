#include "tinytest.h"
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"
#include "rfl_js_manager.hpp"

// Helper returns session or nullptr on failure (no assertions in helper)
std::unique_ptr<StatefulSession> build_session(std::string const& drl, ParsingResult& out_result) {
    auto kb = build_knowledge_base(drl, out_result);
    if (!out_result.success || !kb) return nullptr;
    return kb->create_session();
}

suite("JMESPath Binding") {
    it("binds jmespath function correctly") {
        ParsingResult result;
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
        )", result);

        check(result.success);
        for (auto const& err : result.errors) check(false, err.to_string().c_str());
        check(session != nullptr);

        auto fact = std::make_shared<Fact>();
        fact->type = "TestFact";
        fact->fields["name"] = std::string("test");

        session->add_fact(fact);
        int fired = session->fire_all_rules();

        check(fired == 1);
    }
}
