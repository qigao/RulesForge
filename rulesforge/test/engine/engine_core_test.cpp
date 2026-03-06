#include "parser/rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "tinytest.h"

std::unique_ptr<StatefulSession> build_session(std::string const& drl) {
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

suite("RFL Engine") {
    group("Simple Rule Fire") {
        it("fires rule and inserts fact") {
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
                    insert Adult { name = $p.name }
                end
            )");

            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = "John";
            person->fields["age"] = (int64_t)25;

            session->add_fact(person);
            int fired = session->fire_all_rules();

            check(fired == 1);
            check(session->get_fact_count() == 2);
        }
    }

    group("Join Condition") {
        it("joins facts correctly") {
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

            check(session->fire_all_rules() == 1);
        }
    }

    group("'not' Pattern") {
        it("fires when negated pattern is absent") {
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

            auto person = std::make_shared<Fact>(Fact{0, "Person"});
            session->add_fact(person);
            check(session->fire_all_rules() == 1);

            auto holiday = std::make_shared<Fact>(Fact{0, "Holiday"});
            session->add_fact(holiday);
            check(session->fire_all_rules() == 0);
        }
    }

    group("Salience") {
        it("respects rule salience order") {
            auto session = build_session(R"(
                declare Trigger end
                declare Result name:String end
                rule "High Salience" salience 10 when Trigger() then
                    insert Result { name = "High" }
                end
                rule "Low Salience" salience 5 when Trigger() then
                    insert Result { name = "Low" }
                end
            )");

            auto fact = std::make_shared<Fact>();
            fact->type = "Trigger";
            session->add_fact(fact);
            int fired = session->fire_all_rules();

            check(fired == 2);
            check(session->get_fact_count() == 3);
        }
    }

    group("Logical Insertions (TMS)") {
        it("retracts logical facts when support is removed") {
            auto session = build_session(R"(
                declare Alarm reason:String end
                declare Fire active:bool end
                rule "Sound Alarm on Fire"
                when
                    $f : Fire(active == true)
                then
                    insertLogical Alarm { reason = "fire" }
                end
            )");

            auto fire_fact = std::make_shared<Fact>(Fact{0, "Fire", {{"active", (int64_t)1}}});
            session->add_fact(fire_fact);
            session->fire_all_rules();

            check(session->get_fact_count() == 2);

            session->retract_fact(fire_fact);
            check(session->get_fact_count() == 0);
        }
    }

    group("'or' Condition") {
        it("fires due to Gold status") {
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
                    insert PremiumCustomer { }
                end
            )");

            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = (int64_t)1;
            customer->fields["status"] = "Gold";

            session->add_fact(customer);
            check(session->fire_all_rules() == 1);
            check(session->get_fact_count() == 2);
        }

        it("fires due to high-value order") {
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
                    insert PremiumCustomer { }
                end
            )");

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
            check(session->fire_all_rules() == 1);
            check(session->get_fact_count() == 3);
        }

        it("does not fire for non-premium") {
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
                    insert PremiumCustomer { }
                end
            )");

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
            check(session->fire_all_rules() == 0);
            check(session->get_fact_count() == 2);
        }
    }
}
