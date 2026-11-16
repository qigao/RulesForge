#include "rfl_parser.hpp"
#include "runtime/stateful_session.hpp"
#include "test_helpers.hpp"
#include <tinytest.hpp>

suite("Engine failure policy") {
    it("rejects an invalid regular expression when building the knowledge base") {
        ParsingResult result;
        auto kb = build_knowledge_base(R"(
            declare Item
                name: String
            end
            rule "Invalid regex" when Item(name matches "[") then halt end
        )", result);
        check_false(result.success);
        check_equal(kb.get(), static_cast<KnowledgeBase*>(nullptr));
        check_false(result.errors.empty());
    }
    it("shares a bound RHS plan between OR terminals and distinguishes other rules") {
        ParsingResult result;
        auto kb = build_knowledge_base(R"(
            declare Item value: long end
            declare Marker value: long end
            rule "Choice" when $i: Item(value == 1) or $i: Item(value == 2)
                then insert Marker { value = $i.value }
            end
            rule "Second" when $i: Item(value == 3)
                then insert Marker { value = $i.value * 10 }
            end
        )", result);
        if (!result.success) throw_parse_failure(result);
        check_equal(kb->get_rules().size(), 2u);
        for (size_t index = 0; index < kb->get_rules().size(); ++index)
            check_equal(kb->get_rules()[index].rhs_plan_index, index);
        auto session = kb->create_session();
        for (int64_t value : {1, 2, 3}) {
            auto* fact = session->create_fact("Item");
            fact->fields["value"] = value;
            session->add_fact(fact);
        }
        check_equal(session->fire_all_rules(), 3);
        check_equal(session->get_fact_count(), 6u);
        int64_t sum = 0;
        for (auto* fact : session->facts_snapshot())
            if (fact->type == "Marker") sum += std::get<int64_t>(*fact->get_field("value"));
        check_equal(sum, 33);
    }
    it("propagates RHS failure from both firing names after rolling back the transaction") {
        for (bool compatibility_name : {false, true}) {
            ParsingResult result;
            auto kb = build_knowledge_base(R"(
                declare Item value: long end
                declare Marker value: long end
                rule "Broken" salience 100 when $i: Item() then
                    update $i { value = $i.value / 0 }
                end
                rule "Later" when Item() then insert Marker { value = 1 } end
            )", result);
            if (!result.success) throw_parse_failure(result);
            auto session = kb->create_session();
            auto* fact = session->create_fact("Item");
            fact->fields["value"] = int64_t{7};
            session->add_fact(fact);
            if (compatibility_name) {
                check_throws_as(session->fire_all_rules_fail_fast(), std::runtime_error);
            } else {
                check_throws_as(session->fire_all_rules(), std::runtime_error);
            }
            check_true(session->is_consistent());
            check_equal(session->get_fact_count(), 1u);
            check_equal(std::get<int64_t>(*fact->get_field("value")), 7);
        }
    }
}
