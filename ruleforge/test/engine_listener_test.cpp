#include "catch2/catch_test_macros.hpp"
#include "rfl_parser.hpp"
#include "i_engine_listener.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

// A mock listener that records the events it receives.
class MockListener : public IEngineListener {
public:
    std::vector<std::string> event_log;

    void on_activation_created(std::string const& ruleName, std::vector<std::shared_ptr<Fact>> const& facts) override {
        event_log.push_back("CREATE:" + ruleName);
    }

    void on_activation_retracted(std::string const& ruleName,
                                 std::vector<std::shared_ptr<Fact>> const& facts) override {
        event_log.push_back("RETRACT:" + ruleName);
    }

    void before_rule_fired(std::string const& ruleName) override { event_log.push_back("BEFORE:" + ruleName); }

    void after_rule_fired(std::string const& ruleName) override { event_log.push_back("AFTER:" + ruleName); }
};

TEST_CASE("Engine: Listener and Auditing API", "[engine][listener]") {
    char const* drl = R"(
        declare Status value:String end
        rule "Listen To This" when Status(value == "go") then end
    )";

    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    REQUIRE(result.success);
    auto session = kb->create_session();

    auto listener = std::make_shared<MockListener>();
    session->addListener(listener);

    auto fact = std::make_shared<Fact>();
    fact->type = "Status";
    fact->fields["value"] = "go";

    // 1. Test creation and firing
    session->add_fact(fact);
    // The activation is created when the fact is added.
    // before/after are triggered when rules are fired.
    session->fire_all_rules();

    std::vector<std::string> expected_log = {"CREATE:Listen To This", "BEFORE:Listen To This", "AFTER:Listen To This"};
    CHECK(listener->event_log == expected_log);

    // 2. Test retraction
    listener->event_log.clear();
    session->retract_fact(fact);

    expected_log = {"RETRACT:Listen To This"};
    CHECK(listener->event_log == expected_log);
}

