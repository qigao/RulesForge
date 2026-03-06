#include "parser/rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/query_result.hpp"
#include "engine/stateful_session.hpp"
#include "tinytest.h"

using namespace rulesforge;

struct QueryTestFixture {
    std::shared_ptr<KnowledgeBase> kb;
    std::unique_ptr<StatefulSession> session;
    std::vector<std::shared_ptr<Fact>> facts_;  // keep facts alive

    void build(std::string const& drl) {
        ParsingResult result;
        kb = build_knowledge_base(drl, result);
        if (!result.success) {
            throw std::runtime_error("RFL parsing failed: " + (result.errors.empty() ? "Unknown error" : result.errors[0].to_string()));
        }
        if (!kb) { throw std::runtime_error("KnowledgeBase is null"); }
        session = kb->create_session();
        if (!session) { throw std::runtime_error("Session is null"); }
    }

    std::shared_ptr<Fact> createFact(std::string const& type, std::initializer_list<std::pair<std::string, ConstraintValue>> fields) {
        auto fact = std::make_shared<Fact>();
        fact->type = type;
        for (auto const& [k, v] : fields) {
            fact->fields[k] = v;
        }
        session->add_fact(fact);
        facts_.push_back(fact);
        return fact;
    }
};

suite("Query Terminal Node") {
    group("Non-Parameterized (Live) Queries") {
        it("returns no results initially") {
            QueryTestFixture fixture;
            fixture.build(R"(
                declare Item name: String end
                query "all-items" $i: Item() end
            )");

            QueryResult results = fixture.session->execute_query("all-items");
            check(results.empty());
        }

        it("finds facts after insertion") {
            QueryTestFixture fixture;
            fixture.build(R"(
                declare Item name: String end
                query "all-items" $i: Item() end
            )");

            fixture.createFact("Item", {{"name", "Book"}});
            fixture.createFact("Item", {{"name", "Pen"}});

            QueryResult results = fixture.session->execute_query("all-items");
            check(results.size() == 2);
        }

        it("updates results after retraction") {
            QueryTestFixture fixture;
            fixture.build(R"(
                declare Item name: String end
                query "all-items" $i: Item() end
            )");

            auto item1 = fixture.createFact("Item", {{"name", "Book"}});
            auto item2 = fixture.createFact("Item", {{"name", "Pen"}});

            fixture.session->retract_fact(item1);
            QueryResult results = fixture.session->execute_query("all-items");
            check(results.size() == 1);

            auto name = results.single().getFieldAs<std::string>("$i", "name");
            check(name.has_value());
            check(name.value() == "Pen");
        }
    }

    group("Parameterized with empty body") {
        it("finds the input parameter") {
            QueryTestFixture fixture;
            fixture.build(R"(
                declare Item name: String end
                query "get-item-with-empty-body"(Item $i) end
            )");

            auto itemA = std::make_shared<Fact>();
            itemA->type = "Item";
            itemA->id = 999;
            itemA->fields["name"] = "Apple";

            QueryResult results = fixture.session->execute_query("get-item-with-empty-body", {itemA.get()});
            check(results.size() == 1);
            auto row = results.single();
            check(row.get("$i").has_value());
            check(row.get("$i").value()->id == itemA->id);
        }
    }

    group("Parameterized with a join") {
        it("finds orders for customer with multiple orders") {
            QueryTestFixture fixture;
            fixture.build(R"(
                declare Customer id: int, name: String end
                declare Order customerId: int, product: String end
                query "find-orders-for-customer"(Customer $c)
                    $o: Order(customerId == $c.id)
                end
            )");

            fixture.createFact("Order", {{"customerId", (int64_t)101}, {"product", "Laptop"}});
            fixture.createFact("Order", {{"customerId", (int64_t)202}, {"product", "Mouse"}});
            fixture.createFact("Order", {{"customerId", (int64_t)101}, {"product", "Keyboard"}});

            auto cust1_arg = std::make_shared<Fact>();
            cust1_arg->type = "Customer";
            cust1_arg->fields = {{"id", (int64_t)101}, {"name", "Alice"}};

            QueryResult results = fixture.session->execute_query("find-orders-for-customer", {cust1_arg.get()});
            check(results.size() == 2);

            for (auto const& row : results) {
                check(row.get("$c").has_value());
                check(row.get("$o").has_value());
            }

            std::vector<std::string> products = results.getColumnFieldAs<std::string>("$o", "product");
            bool has_laptop = std::find(products.begin(), products.end(), "Laptop") != products.end();
            bool has_keyboard = std::find(products.begin(), products.end(), "Keyboard") != products.end();
            check(has_laptop);
            check(has_keyboard);
        }

        it("finds single order for customer") {
            QueryTestFixture fixture;
            fixture.build(R"(
                declare Customer id: int, name: String end
                declare Order customerId: int, product: String end
                query "find-orders-for-customer"(Customer $c)
                    $o: Order(customerId == $c.id)
                end
            )");

            fixture.createFact("Order", {{"customerId", (int64_t)101}, {"product", "Laptop"}});
            auto order2 = fixture.createFact("Order", {{"customerId", (int64_t)202}, {"product", "Mouse"}});
            fixture.createFact("Order", {{"customerId", (int64_t)101}, {"product", "Keyboard"}});

            auto cust2_arg = std::make_shared<Fact>();
            cust2_arg->type = "Customer";
            cust2_arg->fields = {{"id", (int64_t)202}, {"name", "Bob"}};

            QueryResult results = fixture.session->execute_query("find-orders-for-customer", {cust2_arg.get()});
            check(results.size() == 1);

            auto fact_opt = results.single().get("$o");
            check(fact_opt.has_value());
            check((*fact_opt)->id == order2->id);
        }

        it("returns empty for customer with no orders") {
            QueryTestFixture fixture;
            fixture.build(R"(
                declare Customer id: int, name: String end
                declare Order customerId: int, product: String end
                query "find-orders-for-customer"(Customer $c)
                    $o: Order(customerId == $c.id)
                end
            )");

            fixture.createFact("Order", {{"customerId", (int64_t)101}, {"product", "Laptop"}});

            auto cust3_arg = std::make_shared<Fact>();
            cust3_arg->type = "Customer";
            cust3_arg->fields = {{"id", (int64_t)303}, {"name", "Charlie"}};

            QueryResult results = fixture.session->execute_query("find-orders-for-customer", {cust3_arg.get()});
            check(results.empty());
        }
    }
}
