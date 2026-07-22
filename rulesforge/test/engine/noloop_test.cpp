#include "rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "test_helpers.hpp"
#include "tinytest.h"

struct NoLoopTestFixture {
    std::shared_ptr<KnowledgeBase> kb;

    void build(std::string const& drl) {
        ParsingResult result;
        kb = build_knowledge_base(drl, result);
        if (!result.success) throw_parse_failure(result);
        if (!kb) { throw std::runtime_error("KnowledgeBase is null"); }
    }

    std::shared_ptr<Fact> make_counter(int value) {
        auto f = std::make_shared<Fact>();
        f->type = "Counter";
        f->fields["value"] = (int64_t)value;
        return f;
    }
};

suite("Engine no-loop") {
    group("no-loop prevents self-reactivation") {
        it("fires only once per fact due to no-loop") {
            NoLoopTestFixture fixture;
            fixture.build(R"(
                declare Counter value: int end

                rule "Increment Counter"
                    no-loop
                when
                    $c: Counter(value < 10)
                then
                    update $c { value = $c.value + 1 }
                end
            )");

            auto session = fixture.kb->create_session();
            auto counter = fixture.make_counter(0);
            session->add_fact(counter);

            int fired = session->fire_all_rules();

            check(fired == 1);

            auto val = counter->get_field("value");
            check(val.has_value());
            check(std::get<int64_t>(*val) == 1);
        }
    }

    group("no-loop allows different facts to trigger") {
        it("each fact can trigger the rule once") {
            NoLoopTestFixture fixture;
            fixture.build(R"(
                declare Item processed: boolean end
                declare Result count: int end

                rule "Process Item"
                    no-loop
                when
                    $i: Item(processed == false)
                then
                    update $i { processed = true }
                    insert Result { count = 1 }
                end
            )");

            auto session = fixture.kb->create_session();

            auto item1 = std::make_shared<Fact>();
            item1->type = "Item";
            item1->fields["processed"] = false;

            auto item2 = std::make_shared<Fact>();
            item2->type = "Item";
            item2->fields["processed"] = false;

            session->add_fact(item1);
            session->add_fact(item2);

            int fired = session->fire_all_rules();

            check(fired == 2);

            check(std::get<bool>(*item1->get_field("processed")));
            check(std::get<bool>(*item2->get_field("processed")));
        }
    }

    group("Without no-loop, rule can fire repeatedly") {
        it("fires multiple times without no-loop") {
            NoLoopTestFixture fixture;
            fixture.build(R"(
                declare Counter value: int end

                rule "Increment Without NoLoop"
                when
                    $c: Counter(value < 3)
                then
                    update $c { value = $c.value + 1 }
                end
            )");

            auto session = fixture.kb->create_session();
            auto counter = fixture.make_counter(0);
            session->add_fact(counter);

            int fired = session->fire_all_rules();

            check(fired == 3);

            auto val = counter->get_field("value");
            check(val.has_value());
            check(std::get<int64_t>(*val) == 3);
        }
    }
}
