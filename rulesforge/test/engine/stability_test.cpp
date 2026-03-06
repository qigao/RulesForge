#include "parser/rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "tinytest.h"

struct StabilityTestFixture {
    std::shared_ptr<KnowledgeBase> kb;

    void build(std::string const& drl) {
        ParsingResult result;
        kb = build_knowledge_base(drl, result);
        if (!result.success) {
            for (auto const& err : result.errors) {
                throw std::runtime_error("RFL parsing failed: " + err.to_string());
            }
            throw std::runtime_error("RFL parsing failed: Unknown error");
        }
        if (!kb) { throw std::runtime_error("KnowledgeBase is null"); }
    }
};

suite("Stability Tests") {
    group("Retraction during rule firing") {
        it("removes dependent activations") {
            StabilityTestFixture fixture;
            fixture.build(R"(
                declare Item id: int, processed: boolean end
                declare Marker id: int end

                rule "Process Item"
                    salience 100
                when
                    $i: Item(processed == false)
                then
                    update $i { processed = true }
                    insert Marker { id = $i.id }
                end

                rule "Cleanup Marker"
                    salience 50
                when
                    $m: Marker()
                then
                    retract $m
                end
            )");

            auto session = fixture.kb->create_session();

            auto item = std::make_shared<Fact>();
            item->type = "Item";
            item->fields["id"] = (int64_t)1;
            item->fields["processed"] = (int64_t)0;

            session->add_fact(item);

            int fired = session->fire_all_rules();

            check(fired == 2);
            check(session->get_fact_count() == 1);
        }
    }

    group("Multiple retractions in single rule") {
        it("retracts all facts correctly") {
            StabilityTestFixture fixture;
            fixture.build(R"(
                declare A id: int end
                declare B id: int end
                declare C id: int end

                rule "Cleanup Multiple"
                when
                    $a: A()
                    $b: B()
                    $c: C()
                then
                    retract $a
                    retract $b
                    retract $c
                end
            )");

            auto session = fixture.kb->create_session();

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

            check(fired == 1);
            check(session->get_fact_count() == 0);
        }
    }

    group("Update followed by retraction") {
        it("handles update then retract correctly") {
            StabilityTestFixture fixture;
            fixture.build(R"(
                declare Counter value: int end

                rule "Update Then Retract"
                when
                    $c: Counter(value == 0)
                then
                    update $c { value = 1 }
                    retract $c
                end
            )");

            auto session = fixture.kb->create_session();

            auto counter = std::make_shared<Fact>();
            counter->type = "Counter";
            counter->fields["value"] = (int64_t)0;

            session->add_fact(counter);

            int fired = session->fire_all_rules();

            check(fired == 1);
            check(session->get_fact_count() == 0);
        }
    }

    group("Bounded iteration with update loop") {
        it("terminates correctly") {
            StabilityTestFixture fixture;
            fixture.build(R"(
                declare Counter value: int end

                rule "Increment Until 100"
                when
                    $c: Counter(value < 100)
                then
                    update $c { value = $c.value + 1 }
                end
            )");

            auto session = fixture.kb->create_session();

            auto counter = std::make_shared<Fact>();
            counter->type = "Counter";
            counter->fields["value"] = (int64_t)0;

            session->add_fact(counter);

            int fired = session->fire_all_rules();

            check(fired == 100);

            auto val = counter->get_field("value");
            check(val.has_value());
            check(std::get<int64_t>(*val) == 100);
        }
    }

    group("Retract fact used by multiple rules") {
        it("prevents lower priority rule from firing") {
            StabilityTestFixture fixture;
            fixture.build(R"(
                declare Shared value: int end
                declare ResultA end
                declare ResultB end

                rule "Rule A"
                    salience 100
                when
                    $s: Shared()
                then
                    insert ResultA { }
                    retract $s
                end

                rule "Rule B"
                    salience 50
                when
                    $s: Shared()
                then
                    insert ResultB { }
                end
            )");

            auto session = fixture.kb->create_session();

            auto shared = std::make_shared<Fact>();
            shared->type = "Shared";
            shared->fields["value"] = (int64_t)1;

            session->add_fact(shared);

            int fired = session->fire_all_rules();

            check(fired == 1);
            check(session->get_fact_count() == 1);
        }
    }
}
