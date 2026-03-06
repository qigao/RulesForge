#include "parser/rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "tinytest.h"
#include <fstream>

std::unique_ptr<StatefulSession> build_session_for_jmespath(std::string const& drl) {
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

suite("JMESPath Source") {
    it("extracts rows from JSON string literal") {
        auto session = build_session_for_jmespath(R"(
            declare Trigger id:int end
            declare Purchase amount:double end
            declare Matched end
            rule "Extract from JSON string"
            when
                $t : Trigger()
                $p : Purchase(amount > 100.0) from jmespath("{\"orders\":[{\"amount\":120.5},{\"amount\":80.0}]}", "orders[*]")
            then
                insert Matched { }
            end
        )");

        auto source = std::make_shared<Fact>();
        source->type = "Trigger";
        source->fields["id"] = static_cast<int64_t>(1);

        session->add_fact(source);
        int fired = session->fire_all_rules();

        check(fired == 1);
        // JMESPath row facts are transient network facts (not inserted into working memory).
        // WM contains only the original source fact + inserted Matched.
        check(session->get_fact_count() == 2);
    }

    it("extracts rows from JSON file") {
        auto const test_file = "jmespath_source_test_orders.json";
        {
            std::ofstream out(test_file, std::ios::out | std::ios::trunc);
            out << R"({"orders":[{"amount":150.0},{"amount":90.0}]})";
        }

        auto session = build_session_for_jmespath(R"(
            declare Trigger id:int end
            declare Purchase amount:double end
            declare Matched end
            rule "Extract from JSON file"
            when
                $t : Trigger()
                $p : Purchase(amount > 100.0) from jmespath(file("jmespath_source_test_orders.json"), "orders[*]")
            then
                insert Matched { }
            end
        )");

        auto source = std::make_shared<Fact>();
        source->type = "Trigger";
        source->fields["id"] = static_cast<int64_t>(1);

        session->add_fact(source);
        int fired = session->fire_all_rules();

        check(fired == 1);
        check(session->get_fact_count() == 2);
    }

    it("extracts rows from DSV string with filter expression") {
        auto session = build_session_for_jmespath(R"(
            declare Trigger id:int end
            declare Purchase amount:double sym:String end
            declare Matched end
            rule "Extract from DSV string"
            when
                $t : Trigger()
                $p : Purchase(amount > 100.0) from dsv("amount_n,sym_s\n120.5,A\n80.0,B\n", "amount > 100 and sym == \"A\"")
            then
                insert Matched { }
            end
        )");

        auto source = std::make_shared<Fact>();
        source->type = "Trigger";
        source->fields["id"] = static_cast<int64_t>(1);
        session->add_fact(source);

        int fired = session->fire_all_rules();
        check(fired == 1);
        check(session->get_fact_count() == 2);
    }

    it("extracts rows from DSV file with filter expression") {
        auto const test_file = "jmespath_source_test_rows.csv";
        {
            std::ofstream out(test_file, std::ios::out | std::ios::trunc);
            out << "amount_n,sym_s\n150.0,A\n90.0,B\n";
        }

        auto session = build_session_for_jmespath(R"(
            declare Trigger id:int end
            declare Purchase amount:double sym:String end
            declare Matched end
            rule "Extract from DSV file"
            when
                $t : Trigger()
                $p : Purchase(amount > 100.0) from dsv(file("jmespath_source_test_rows.csv"), "amount > 100 and sym == \"A\"")
            then
                insert Matched { }
            end
        )");

        auto source = std::make_shared<Fact>();
        source->type = "Trigger";
        source->fields["id"] = static_cast<int64_t>(1);
        session->add_fact(source);

        int fired = session->fire_all_rules();
        check(fired == 1);
        check(session->get_fact_count() == 2);
    }

    it("extracts rows from CSV alias with filter expression") {
        auto const test_file = "jmespath_source_test_rows_alias.csv";
        {
            std::ofstream out(test_file, std::ios::out | std::ios::trunc);
            out << "orderId_s,amount_n,sym_s\nA1,150.0,USD\nA2,90.0,USD\n";
        }

        auto session = build_session_for_jmespath(R"(
            declare Trigger id:int end
            declare Purchase orderId:String amount:double sym:String end
            declare Matched orderId:String amount:double end
            rule "Extract from CSV alias file"
            when
                $t : Trigger()
                $p : Purchase() from csv(file("jmespath_source_test_rows_alias.csv"), "amount > 100 and sym == \"USD\"")
            then
                insert Matched { orderId = $p.orderId, amount = $p.amount }
            end
        )");

        auto source = std::make_shared<Fact>();
        source->type = "Trigger";
        source->fields["id"] = static_cast<int64_t>(1);
        session->add_fact(source);

        int fired = session->fire_all_rules();
        check(fired == 1);
        check(session->get_fact_count() == 2);
    }

    it("supports jmespath source inside accumulate") {
        auto const test_file = "jmespath_source_test_accumulate.json";
        {
            std::ofstream out(test_file, std::ios::out | std::ios::trunc);
            out << R"({"orders":[{"amount":800.0},{"amount":500.0}]})";
        }

        auto session = build_session_for_jmespath(R"(
            declare Trigger id:int end
            declare Purchase amount:double end
            declare RiskAlert score:double end
            rule "Accumulate from jmespath"
            when
                $t : Trigger()
                $sum : Number(doubleValue > 1000.0) from accumulate(
                    $p : Purchase() from jmespath(file("jmespath_source_test_accumulate.json"), "orders[*]"),
                    sum($p.amount)
                )
            then
                insert RiskAlert { score = $sum.doubleValue }
            end
        )");

        auto source = std::make_shared<Fact>();
        source->type = "Trigger";
        source->fields["id"] = static_cast<int64_t>(1);
        session->add_fact(source);

        int fired = session->fire_all_rules();
        check(fired == 1);
        // Trigger + accumulate Number result + inserted RiskAlert
        check(session->get_fact_count() == 3);
    }
}
