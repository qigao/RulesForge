#include "catch2/catch_test_macros.hpp"
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

/**
 * Tests for retraction edge cases and infinite loop scenarios.
 * These are important for ensuring stability in production environments.
 */

struct StabilityTestFixture {
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
};

TEST_CASE_METHOD(StabilityTestFixture, "Retraction during rule firing removes dependent activations",
                 "[engine][retraction][stability]") {
    build(R"(
        declare Item id: int, processed: boolean end
        declare Marker id: int end

        rule "Process Item"
            salience 100
        when
            $i: Item(processed == false)
        then
            rfl.update(i, {processed: true});
            rfl.insert({type: "Marker", id: i.id});
        end

        rule "Cleanup Marker"
            salience 50
        when
            $m: Marker()
        then
            rfl.retract(m);
        end
    )");

    SECTION("Retracted facts don't cause dangling activations") {
        auto session = kb->create_session();

        auto item = std::make_shared<Fact>();
        item->type = "Item";
        item->fields["id"] = (int64_t)1;
        item->fields["processed"] = (int64_t)0;

        session->add_fact(item);

        // Should fire "Process Item" then "Cleanup Marker"
        int fired = session->fire_all_rules();

        CHECK(fired == 2);
        // Marker should be retracted, only Item remains
        CHECK(session->get_fact_count() == 1);
    }
}

TEST_CASE_METHOD(StabilityTestFixture, "Multiple retractions in single rule", "[engine][retraction][stability]") {
    build(R"(
        declare A id: int end
        declare B id: int end
        declare C id: int end

        rule "Cleanup Multiple"
        when
            $a: A()
            $b: B()
            $c: C()
        then
            rfl.retract(a);
            rfl.retract(b);
            rfl.retract(c);
        end
    )");

    SECTION("All facts retracted correctly") {
        auto session = kb->create_session();

        auto a = std::make_shared<Fact>();
        a->type = "A";
        a->fields["id"] = (int64_t)1;

        auto b = std::make_shared<Fact>();
        b->type = "B";
        b->fields["id"] = (int64_t)2;

        auto c = std::make_shared<Fact>();
        c->type = "C";
        c->fields["id"] = (int64_t)3;

        session->add_fact(a);
        session->add_fact(b);
        session->add_fact(c);

        int fired = session->fire_all_rules();

        CHECK(fired == 1);
        CHECK(session->get_fact_count() == 0);
    }
}

TEST_CASE_METHOD(StabilityTestFixture, "Update followed by retraction of same fact", "[engine][retraction][stability]") {
    build(R"(
        declare Counter value: int end

        rule "Update Then Retract"
        when
            $c: Counter(value == 0)
        then
            rfl.update(c, {value: 1});
            rfl.retract(c);
        end
    )");

    SECTION("Retraction after update works correctly") {
        auto session = kb->create_session();

        auto counter = std::make_shared<Fact>();
        counter->type = "Counter";
        counter->fields["value"] = (int64_t)0;

        session->add_fact(counter);

        int fired = session->fire_all_rules();

        CHECK(fired == 1);
        CHECK(session->get_fact_count() == 0);
    }
}

TEST_CASE_METHOD(StabilityTestFixture, "Bounded iteration with update loop", "[engine][update][stability]") {
    build(R"(
        declare Counter value: int end

        rule "Increment Until 100"
        when
            $c: Counter(value < 100)
        then
            rfl.update(c, {value: c.value + 1});
        end
    )");

    SECTION("Update loop terminates correctly") {
        auto session = kb->create_session();

        auto counter = std::make_shared<Fact>();
        counter->type = "Counter";
        counter->fields["value"] = (int64_t)0;

        session->add_fact(counter);

        int fired = session->fire_all_rules();

        // Should fire exactly 100 times (0->1, 1->2, ..., 99->100)
        CHECK(fired == 100);

        auto val = counter->get_field("value");
        REQUIRE(val.has_value());
        CHECK(std::get<int64_t>(*val) == 100);
    }
}

TEST_CASE_METHOD(StabilityTestFixture, "Retract fact used by multiple rules", "[engine][retraction][stability]") {
    build(R"(
        declare Shared value: int end
        declare ResultA end
        declare ResultB end

        rule "Rule A"
            salience 100
        when
            $s: Shared()
        then
            rfl.insert({type: "ResultA"});
            rfl.retract(s);
        end

        rule "Rule B"
            salience 50
        when
            $s: Shared()
        then
            rfl.insert({type: "ResultB"});
        end
    )");

    SECTION("Lower priority rule doesn't fire after fact is retracted") {
        auto session = kb->create_session();

        auto shared = std::make_shared<Fact>();
        shared->type = "Shared";
        shared->fields["value"] = (int64_t)1;

        session->add_fact(shared);

        int fired = session->fire_all_rules();

        // Only Rule A should fire (higher salience)
        // Rule B's activation should be removed when Shared is retracted
        CHECK(fired == 1);

        // Only ResultA should exist (Shared retracted, ResultB never created)
        CHECK(session->get_fact_count() == 1);
    }
}


