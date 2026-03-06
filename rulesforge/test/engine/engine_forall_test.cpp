#include "parser/rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "tinytest.h"

struct ForallTestFixture {
    std::shared_ptr<KnowledgeBase> kb;
    std::unique_ptr<StatefulSession> session;
    std::vector<std::shared_ptr<Fact>> facts_;

    ForallTestFixture() {
        char const* drl = R"(
            declare Customer
                id : int
            end
            declare Order
                customerId : int
                isShipped : boolean
            end
            declare FullyShippedCustomer
                id : int
            end

            rule "Identify Customers with All Orders Shipped"
            when
                $cust : Customer($id : id)
                forall (
                    Order(customerId == $id),
                    Order(isShipped == true)
                )
            then
                insert FullyShippedCustomer { id = $id }
            end
        )";

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

    std::shared_ptr<Fact> make_customer(int id) {
        auto c = std::make_shared<Fact>();
        c->type = "Customer";
        c->fields["id"] = (int64_t)id;
        facts_.push_back(c);
        return c;
    }

    std::shared_ptr<Fact> make_order(int custId, bool shipped) {
        auto o = std::make_shared<Fact>();
        o->type = "Order";
        o->fields["customerId"] = (int64_t)custId;
        o->fields["isShipped"] = (int64_t)(shipped ? 1 : 0);
        facts_.push_back(o);
        return o;
    }
};

suite("Engine Forall Operator") {
    group("Forall behavior") {
        it("fires when customer has all orders shipped") {
            ForallTestFixture fixture;
            fixture.session = fixture.kb->create_session();
            fixture.session->add_fact(fixture.make_customer(1));
            fixture.session->add_fact(fixture.make_order(1, true));
            fixture.session->add_fact(fixture.make_order(1, true));

            int fired = fixture.session->fire_all_rules();
            check(fired == 1);
            check(fixture.session->get_fact_count() == 4);
        }

        it("does not fire when customer has one unshipped order") {
            ForallTestFixture fixture;
            fixture.session = fixture.kb->create_session();
            fixture.session->add_fact(fixture.make_customer(2));
            fixture.session->add_fact(fixture.make_order(2, true));
            fixture.session->add_fact(fixture.make_order(2, false));

            int fired = fixture.session->fire_all_rules();
            check(fired == 0);
            check(fixture.session->get_fact_count() == 3);
        }

        it("fires for customer with no orders (vacuously true)") {
            ForallTestFixture fixture;
            fixture.session = fixture.kb->create_session();
            fixture.session->add_fact(fixture.make_customer(3));

            int fired = fixture.session->fire_all_rules();
            check(fired == 1);
            check(fixture.session->get_fact_count() == 2);
        }

        it("handles mixed customers correctly") {
            ForallTestFixture fixture;
            fixture.session = fixture.kb->create_session();
            fixture.session->add_fact(fixture.make_customer(10));
            fixture.session->add_fact(fixture.make_order(10, true));

            fixture.session->add_fact(fixture.make_customer(11));
            fixture.session->add_fact(fixture.make_order(11, true));
            fixture.session->add_fact(fixture.make_order(11, false));

            fixture.session->add_fact(fixture.make_customer(12));

            int fired = fixture.session->fire_all_rules();
            check(fired == 2);
        }
    }
}
