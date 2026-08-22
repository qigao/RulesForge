#include "rfl_parser.hpp"
#include "engine/i_engine_listener.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "tinytest.hpp"

class MockListener : public IEngineListener {
public:
    std::vector<std::string> event_log;

    void on_activation_created(std::string const& ruleName, std::vector<Fact*> const& facts) override {
        event_log.push_back("CREATE:" + ruleName);
    }

    void on_activation_retracted(std::string const& ruleName,
                                 std::vector<Fact*> const& facts) override {
        event_log.push_back("RETRACT:" + ruleName);
    }

    void before_rule_fired(std::string const& ruleName) override { event_log.push_back("BEFORE:" + ruleName); }

    void after_rule_fired(std::string const& ruleName) override { event_log.push_back("AFTER:" + ruleName); }
};

suite("Engine Listener and Auditing API") {
    it("tracks activation creation and rule firing") {
        char const* drl = R"(
            declare Status value:String end
            rule "Listen To This" when Status(value == "go") then end
        )";

        ParsingResult result;
        auto kb = build_knowledge_base(drl, result);
        check(result.success);
        auto session = kb->create_session();

        auto listener = std::make_shared<MockListener>();
        session->addListener(listener);

        auto fact = std::make_shared<Fact>();
        fact->type = "Status";
        fact->fields["value"] = "go";

        session->add_fact(fact);
        session->fire_all_rules();

        std::vector<std::string> expected_log = {"CREATE:Listen To This", "BEFORE:Listen To This", "AFTER:Listen To This"};
        check(listener->event_log == expected_log);

        listener->event_log.clear();
        session->retract_fact(fact);

        expected_log = {"RETRACT:Listen To This"};
        check(listener->event_log == expected_log);
    }
}
