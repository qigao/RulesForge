#include "rfl_parser.hpp"
#include "runtime/stateful_session.hpp"
#include "diagnostics/exceptions.hpp"
#include "test_helpers.hpp"
#include <tinytest.hpp>
#include <limits>

suite("MIR expressions in the rule engine") {
    it("requires reset after synchronous or batched propagation fails") {
        for (bool batch : {false, true}) {
            ParsingResult parsed;
            auto kb = build_knowledge_base(R"(
                declare Item value: long end
                declare Marker value: long end
                rule "Divide" when $i: Item() eval(100 / $i.value > 1)
                    then insert Marker { value = $i.value } end
            )", parsed);
            if (!parsed.success) throw_parse_failure(parsed);
            auto session = kb->create_session();
            auto* invalid = session->create_fact("Item");
            invalid->fields["value"] = int64_t{0};
            check_throws_as(([&] {
                if (batch) session->add_facts(std::vector<Fact*>{invalid});
                else session->add_fact(invalid);
                session->fire_all_rules();
            }()), std::domain_error);
            check_false(session->is_consistent());
            check_throws_as(session->fire_all_rules(), SessionInconsistentException);
            check_throws_as(session->fire_all_rules_fail_fast(), SessionInconsistentException);
            check_throws_as(session->add_fact(invalid), SessionInconsistentException);
            check_throws_as(session->execute_query("unused"), SessionInconsistentException);
            session->reset();
            check_true(session->is_consistent());
            auto* valid = session->create_fact("Item");
            valid->fields["value"] = int64_t{10};
            session->add_fact(valid);
            check_equal(session->fire_all_rules(), 1);
        }
    }
    it("writes exact arithmetic results through the existing RHS transaction") {
        ParsingResult parsed;
        auto kb = build_knowledge_base(R"(
            declare Item base: long value: long done: boolean end
            rule "Compute" when $i: Item(done == false) then
                update $i { value = $i.base + 2, done = true }
            end
        )", parsed);
        if (!parsed.success) throw_parse_failure(parsed);
        auto session = kb->create_session();
        auto* item = session->create_fact("Item");
        item->fields["base"] = int64_t{9007199254740993};
        item->fields["value"] = int64_t{0};
        item->fields["done"] = false;
        session->add_fact(item);
        check_equal(session->fire_all_rules(), 1);
        auto result = item->get_field("value");
        check_equal(std::get<int64_t>(*result), int64_t{9007199254740995});
    }
    it("rolls back an earlier write when native arithmetic overflows") {
        ParsingResult parsed;
        auto kb = build_knowledge_base(R"(
            declare Item base: long value: long end
            rule "Overflow" when $i: Item() then
                update $i { value = 99 }
                update $i { value = $i.base * 3 }
            end
        )", parsed);
        if (!parsed.success) throw_parse_failure(parsed);
        auto session = kb->create_session();
        auto* item = session->create_fact("Item");
        item->fields["base"] = std::numeric_limits<int64_t>::max();
        item->fields["value"] = int64_t{7};
        session->add_fact(item);
        check_throws_as(session->fire_all_rules(), std::runtime_error);
        auto result = item->get_field("value");
        check_equal(std::get<int64_t>(*result), int64_t{7});
        check_true(session->is_consistent());
    }
    it("uses native short circuit branches in RETE eval predicates") {
        ParsingResult parsed;
        auto kb = build_knowledge_base(R"(
            declare Item value: long end
            declare Marker value: long end
            rule "Guarded" when $i: Item()
                eval($i.value != 0 && 100 / $i.value > 3)
            then insert Marker { value = $i.value } end
        )", parsed);
        if (!parsed.success) throw_parse_failure(parsed);
        auto session = kb->create_session();
        for (int64_t value : {0, 10, 50}) {
            auto* item = session->create_fact("Item");
            item->fields["value"] = value;
            session->add_fact(item);
        }
        check_equal(session->fire_all_rules(), 1);
        check_equal(session->get_fact_count(), 4u);
    }
    it("compiles correlated arithmetic constraints before publishing the network") {
        ParsingResult parsed;
        auto kb = build_knowledge_base(R"(
            declare Threshold value: long end
            declare Item value: long end
            declare Marker value: long end
            rule "Correlated" when $t: Threshold()
                $i: Item(value > ($t.value + 2))
            then insert Marker { value = $i.value } end
        )", parsed);
        if (!parsed.success) throw_parse_failure(parsed);
        auto session = kb->create_session();
        auto* threshold = session->create_fact("Threshold");
        threshold->fields["value"] = int64_t{9007199254740993};
        session->add_fact(threshold);
        auto* item = session->create_fact("Item");
        item->fields["value"] = int64_t{9007199254740996};
        session->add_fact(item);
        check_equal(session->fire_all_rules(), 1);
    }
    it("rejects an unsupported expression while loading rules") {
        ParsingResult parsed;
        auto kb = build_knowledge_base(R"(
            declare Item value: long end
            rule "Unsupported" when $i: Item() then
                update $i { value = unknownfunc($i.value) }
            end
        )", parsed);
        check_false(parsed.success);
        check_null(kb.get());
        check_false(parsed.errors.empty());
    }
}
