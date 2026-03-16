#include "rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "tinytest.h"

template <typename T>
T get_field(Fact const& f, std::string const& field) {
    auto val_opt = f.get_field(field);
    if (!val_opt.has_value()) { throw std::runtime_error("Field '" + field + "' not found"); }
    if (auto val_ptr = std::get_if<T>(&*val_opt)) { return *val_ptr; }
    if constexpr (std::is_same_v<T, double>) {
        if (auto int_ptr = std::get_if<int64_t>(&*val_opt)) { return static_cast<double>(*int_ptr); }
    }
    throw std::runtime_error("Field '" + field + "' has an unexpected type.");
}

struct AccumulateTestFixture {
    std::unique_ptr<StatefulSession> session;
    std::shared_ptr<KnowledgeBase> kb;

    void build_session(std::string const& drl) {
        ParsingResult result;
        kb = build_knowledge_base(drl, result);
        if (!result.success) {
            for (auto const& err : result.errors) {
                throw std::runtime_error("RFL parsing failed: " + err.to_string());
            }
            throw std::runtime_error("RFL parsing failed: Unknown error");
        }
        session = kb->create_session();
        if (!session) { throw std::runtime_error("Session is null"); }
    }

    std::shared_ptr<Fact> make_purchase(double value) {
        auto fact = std::make_shared<Fact>();
        fact->type = "Purchase";
        fact->fields["value"] = value;
        return fact;
    }

    Fact* get_result_fact(std::string const& type = "Total") {
        for (int64_t i = 1; i <= session->get_next_fact_id(); ++i) {
            auto fact = session->get_fact_by_id(i);
            if (fact && fact->type == type) { return fact; }
        }
        return nullptr;
    }
};

suite("AccumulateNode") {
    group("sum function with modification") {
        it("calculates sum and updates on changes") {
            AccumulateTestFixture fixture;
            fixture.build_session(R"(
                declare Purchase value: double end
                declare Total result: double end
                rule "Sum Purchases"
                when
                    $t: Total() from accumulate(
                        $p: Purchase(),
                        sum($p.value)
                    )
                then
                end
            )");

            auto initial_count = fixture.session->get_fact_count();
            auto result_fact = fixture.get_result_fact();
            check(result_fact != nullptr);
            check(result_fact->type == "Total");
            check(std::abs(get_field<double>(*result_fact, "result") - 0.0) < 0.001);
            int64_t result_fact_id = result_fact->id;

            auto p1 = fixture.make_purchase(10.5);
            fixture.session->add_fact(p1);

            check(fixture.session->get_fact_count() == initial_count + 1);
            result_fact = fixture.get_result_fact();
            check(result_fact != nullptr);
            check(result_fact->id == result_fact_id);
            check(std::abs(get_field<double>(*result_fact, "result") - 10.5) < 0.001);

            auto p2 = fixture.make_purchase(20.0);
            fixture.session->add_fact(p2);

            result_fact = fixture.get_result_fact();
            check(result_fact != nullptr);
            check(result_fact->id == result_fact_id);
            check(std::abs(get_field<double>(*result_fact, "result") - 30.5) < 0.001);

            fixture.session->retract_fact(p1);

            result_fact = fixture.get_result_fact();
            check(result_fact != nullptr);
            check(result_fact->id == result_fact_id);
            check(std::abs(get_field<double>(*result_fact, "result") - 20.0) < 0.001);
        }
    }

    group("collectList function") {
        it("collects facts into a list") {
            AccumulateTestFixture fixture;
            fixture.build_session(R"(
                declare Order orderId: int end
                declare OrderList result: FactList end
                rule "Collect Orders"
                when
                    $orders: OrderList() from accumulate(
                        $o: Order(),
                        collectList($o)
                    )
                then
                end
            )");

            auto result_fact = fixture.get_result_fact("OrderList");
            check(result_fact != nullptr);

            auto result_opt = result_fact->get_field("result");
            check(result_opt.has_value());
            check(std::holds_alternative<FactList>(*result_opt));
            check(std::get<FactList>(*result_opt).facts.size() == 0);

            auto order1 = std::make_shared<Fact>();
            order1->type = "Order";
            order1->fields["orderId"] = (int64_t)100;
            fixture.session->add_fact(order1);

            result_fact = fixture.get_result_fact("OrderList");
            check(result_fact != nullptr);
            result_opt = result_fact->get_field("result");
            check(result_opt.has_value());
            check(std::holds_alternative<FactList>(*result_opt));
            check(std::get<FactList>(*result_opt).facts.size() == 1);

            auto order2 = std::make_shared<Fact>();
            order2->type = "Order";
            order2->fields["orderId"] = (int64_t)200;
            fixture.session->add_fact(order2);

            result_fact = fixture.get_result_fact("OrderList");
            result_opt = result_fact->get_field("result");
            check(std::holds_alternative<FactList>(*result_opt));
            check(std::get<FactList>(*result_opt).facts.size() == 2);
        }
    }
}
