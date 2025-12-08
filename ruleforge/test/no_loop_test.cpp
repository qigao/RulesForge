#include "catch2/catch_test_macros.hpp"
#include "drools_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

struct NoLoopTestFixture {
    std::shared_ptr<KnowledgeBase> kb;

    void build(std::string const& drl) {
        ParsingResult result;
        kb = build_knowledge_base(drl, result);
        if (!result.success) {
            for (auto const& err : result.errors) { FAIL(err.to_string()); }
        }
        REQUIRE(result.success);
        REQUIRE(kb != nullptr);
    }

    std::shared_ptr<Fact> make_counter(int value) {
        auto f = std::make_shared<Fact>();
        f->type = "Counter";
        f->fields["value"] = (int64_t)value;
        return f;
    }
};

TEST_CASE_METHOD(NoLoopTestFixture, "no-loop prevents self-reactivation", "[engine][no-loop]") {
    build(R"(
        declare Counter value: int end

        rule "Increment Counter"
            no-loop
        when
            $c: Counter(value < 10)
        then
            drools.update(c, {value: c.value + 1});
        end
    )");

    SECTION("Rule fires only once per fact due to no-loop") {
        auto session = kb->create_session();
        auto counter = make_counter(0);
        session->add_fact(counter);

        int fired = session->fire_all_rules();

        // With no-loop, the rule should fire exactly once
        // Without no-loop, it would fire 10 times (0->1, 1->2, ..., 9->10)
        CHECK(fired == 1);

        // Value should be incremented only once
        auto val = counter->get_field("value");
        REQUIRE(val.has_value());
        CHECK(std::get<int64_t>(*val) == 1);
    }
}

TEST_CASE_METHOD(NoLoopTestFixture, "no-loop allows different facts to trigger", "[engine][no-loop]") {
    build(R"(
        declare Item processed: boolean end
        declare Result count: int end

        rule "Process Item"
            no-loop
        when
            $i: Item(processed == false)
        then
            drools.update(i, {processed: true});
            drools.insert({type: "Result", count: 1});
        end
    )");

    SECTION("Each fact can trigger the rule once") {
        auto session = kb->create_session();

        auto item1 = std::make_shared<Fact>();
        item1->type = "Item";
        item1->fields["processed"] = (int64_t)0;

        auto item2 = std::make_shared<Fact>();
        item2->type = "Item";
        item2->fields["processed"] = (int64_t)0;

        session->add_fact(item1);
        session->add_fact(item2);

        int fired = session->fire_all_rules();

        // Both items should be processed (2 rules fired)
        CHECK(fired == 2);

        // Both items should be marked as processed
        CHECK(std::get<int64_t>(*item1->get_field("processed")) == 1);
        CHECK(std::get<int64_t>(*item2->get_field("processed")) == 1);
    }
}

TEST_CASE_METHOD(NoLoopTestFixture, "Without no-loop, rule can fire repeatedly", "[engine][no-loop]") {
    build(R"(
        declare Counter value: int end

        rule "Increment Without NoLoop"
        when
            $c: Counter(value < 3)
        then
            drools.update(c, {value: c.value + 1});
        end
    )");

    SECTION("Rule fires multiple times without no-loop") {
        auto session = kb->create_session();
        auto counter = make_counter(0);
        session->add_fact(counter);

        int fired = session->fire_all_rules();

        // Without no-loop, should fire 3 times: 0->1, 1->2, 2->3
        CHECK(fired == 3);

        auto val = counter->get_field("value");
        REQUIRE(val.has_value());
        CHECK(std::get<int64_t>(*val) == 3);
    }
}
