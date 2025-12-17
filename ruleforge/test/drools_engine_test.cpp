#include "catch2/catch_all.hpp"
#include "drools_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

std::unique_ptr<StatefulSession> build_session(std::string const& drl) {
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    REQUIRE(result.success);
    for (auto const& err : result.errors) FAIL(err.to_string());
    REQUIRE(kb != nullptr);
    auto session = kb->create_session();
    REQUIRE(session != nullptr);
    return session;
}

TEST_CASE("Engine: Simple Rule Fire", "[engine]") {
    auto session = build_session(R"(
        declare Person
            name: String
            age: int
        end
        declare Adult
            name: String
        end
        rule "Find Adults"
        when
            $p : Person(age >= 18)
        then
            drools.insert({type: "Adult", name: $p.name});
        end
    )");

    auto person = std::make_shared<Fact>();
    person->type = "Person";
    person->fields["name"] = "John";
    person->fields["age"] = (int64_t)25;

    session->add_fact(person);
    int fired = session->fire_all_rules();

    CHECK(fired == 1);
    CHECK(session->get_fact_count() == 2);
}

TEST_CASE("Engine: Join Condition", "[engine]") {
    auto session = build_session(R"(
        declare Customer
            id: int
            name: String
        end
        declare Order
            customerId: int
            item: String
        end
        rule "Find Customer Orders"
        when
            $c : Customer($id : id)
            $o : Order(customerId == $id)
        then
        end
    )");

    auto customer = std::make_shared<Fact>();
    customer->type = "Customer";
    customer->fields["id"] = (int64_t)123;
    customer->fields["name"] = "Acme Inc.";

    auto order = std::make_shared<Fact>();
    order->type = "Order";
    order->fields["customerId"] = (int64_t)123;
    order->fields["item"] = "Anvil";

    session->add_fact(customer);
    session->add_fact(order);

    CHECK(session->fire_all_rules() == 1);
}

TEST_CASE("Engine: `not` Pattern", "[engine]") {
    auto session = build_session(R"(
        declare Person name:String end
        declare Holiday name:String end
        rule "Work Day"
        when
            Person()
            not (Holiday())
        then
        end
    )");

    session->add_fact(std::make_shared<Fact>(Fact{0, "Person"}));
    CHECK(session->fire_all_rules() == 1);

    session->add_fact(std::make_shared<Fact>(Fact{0, "Holiday"}));
    CHECK(session->fire_all_rules() == 0);
}

TEST_CASE("Engine: Salience", "[engine]") {
    auto session = build_session(R"(
        declare Trigger end
        declare Result name:String end
        rule "High Salience" salience 10 when Trigger() then 
            drools.insert({type: "Result", name: "High"}); 
        end
        rule "Low Salience" salience 5 when Trigger() then 
            drools.insert({type: "Result", name: "Low"}); 
        end
    )");

    auto fact = std::make_shared<Fact>();
    fact->type = "Trigger";
    session->add_fact(fact);
    int fired = session->fire_all_rules();

    CHECK(fired == 2);
    CHECK(session->get_fact_count() == 3);
}

TEST_CASE("Engine: Logical Insertions (TMS)", "[engine]") {
    auto session = build_session(R"(
        declare Alarm reason:String end
        declare Fire active:bool end
        rule "Sound Alarm on Fire"
        when
            $f : Fire(active == true)
        then
            drools.insertLogical({type: "Alarm", reason: "fire"});
        end
    )");

    auto fire_fact = std::make_shared<Fact>(Fact{0, "Fire", {{"active", (int64_t)1}}});
    session->add_fact(fire_fact);
    session->fire_all_rules();

    CHECK(session->get_fact_count() == 2);

    session->retract_fact(fire_fact);
    CHECK(session->get_fact_count() == 0);
}

TEST_CASE("Engine: `or` Condition", "[engine][or]") {
    auto session = build_session(R"(
        declare Customer
            id : int
            status : String
        end
        declare Order
            customerId : int
            amount : double
        end
        declare PremiumCustomer
            id : int
        end

        rule "Identify Premium Customers"
        when
            $c1 : Customer( status == "Gold" )
            or
            $c2 : Customer( $id : id )
            Order( customerId == $id, amount > 500.0 )
        then
            drools.insert({type: "PremiumCustomer"});
        end
    )");

    SECTION("Fires due to Gold status") {
        auto customer = std::make_shared<Fact>();
        customer->type = "Customer";
        customer->fields["id"] = (int64_t)1;
        customer->fields["status"] = "Gold";

        session->add_fact(customer);
        CHECK(session->fire_all_rules() == 1);
        CHECK(session->get_fact_count() == 2);
    }

    SECTION("Fires due to high-value order") {
        auto customer = std::make_shared<Fact>();
        customer->type = "Customer";
        customer->fields["id"] = (int64_t)2;
        customer->fields["status"] = "Silver";

        auto order = std::make_shared<Fact>();
        order->type = "Order";
        order->fields["customerId"] = (int64_t)2;
        order->fields["amount"] = 600.0;

        session->add_fact(customer);
        session->add_fact(order);
        CHECK(session->fire_all_rules() == 1);
        CHECK(session->get_fact_count() == 3);
    }

    SECTION("Does not fire for non-premium") {
        auto customer = std::make_shared<Fact>();
        customer->type = "Customer";
        customer->fields["id"] = (int64_t)3;
        customer->fields["status"] = "Bronze";

        auto order = std::make_shared<Fact>();
        order->type = "Order";
        order->fields["customerId"] = (int64_t)3;
        order->fields["amount"] = 100.0;

        session->add_fact(customer);
        session->add_fact(order);
        CHECK(session->fire_all_rules() == 0);
        CHECK(session->get_fact_count() == 2);
    }
}


