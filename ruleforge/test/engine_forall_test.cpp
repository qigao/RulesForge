#include "catch2/catch_test_macros.hpp"
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

// Test fixture to create a KnowledgeBase with the 'forall' rule.
// Each test section will create a fresh session from this KB.
struct ForallTestFixture {
    std::shared_ptr<KnowledgeBase> kb;
    std::unique_ptr<StatefulSession> session;

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
                // This is equivalent to: "there is NOT an order for this customer that is NOT shipped"
                // The transformer will convert this to:
                // not ( Order(customerId == $id, isShipped != true) )
                forall (
                    Order(customerId == $id),
                    Order(isShipped == true)
                )
            then
                rfl.insert({type: "FullyShippedCustomer", id: $id});
            end
        )";

        ParsingResult result;
        kb = build_knowledge_base(drl, result);
        if (!result.success) {
            for (auto const& err : result.errors) { FAIL(err.to_string()); }
        }
        REQUIRE(result.success);
        REQUIRE(kb != nullptr);
    }

    // Helper to create a Customer fact
    std::shared_ptr<Fact> make_customer(int id) {
        auto c = std::make_shared<Fact>();
        c->type = "Customer";
        c->fields["id"] = (int64_t)id;
        return c;
    }

    // Helper to create an Order fact
    std::shared_ptr<Fact> make_order(int custId, bool shipped) {
        auto o = std::make_shared<Fact>();
        o->type = "Order";
        o->fields["customerId"] = (int64_t)custId;
        // RFL 'true'/'false' is represented as int64_t 1/0
        o->fields["isShipped"] = (int64_t)(shipped ? 1 : 0);
        return o;
    }
};

TEST_CASE_METHOD(ForallTestFixture, "Engine: Forall Operator", "[engine][forall]") {

    SECTION("Customer with all orders shipped should fire") {
        session = kb->create_session();
        session->add_fact(make_customer(1));
        session->add_fact(make_order(1, true));
        session->add_fact(make_order(1, true));

        int fired = session->fire_all_rules();
        CHECK(fired == 1);
        CHECK(session->get_fact_count() == 4);   // 3 original + 1 new
    }

    SECTION("Customer with one unshipped order should NOT fire") {
        session = kb->create_session();
        session->add_fact(make_customer(2));
        session->add_fact(make_order(2, true));
        session->add_fact(make_order(2, false));   // The violating order

        int fired = session->fire_all_rules();
        CHECK(fired == 0);
        CHECK(session->get_fact_count() == 3);
    }

    SECTION("Customer with no orders should fire (vacuously true)") {
        session = kb->create_session();
        // `forall` is vacuously true if the base condition (finding an order) is false.
        // "There does not exist an order for this customer that is not shipped" is true.
        session->add_fact(make_customer(3));

        int fired = session->fire_all_rules();
        CHECK(fired == 1);
        CHECK(session->get_fact_count() == 2);
    }

    SECTION("Mixed customers") {
        session = kb->create_session();
        session->add_fact(make_customer(10));   // All shipped -> should fire
        session->add_fact(make_order(10, true));

        session->add_fact(make_customer(11));   // One not shipped -> should NOT fire
        session->add_fact(make_order(11, true));
        session->add_fact(make_order(11, false));

        session->add_fact(make_customer(12));   // No orders -> should fire

        int fired = session->fire_all_rules();
        CHECK(fired == 2);   // Customer 10 and 12 should be promoted.
    }
}


