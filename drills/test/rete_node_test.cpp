#include "catch2/catch_all.hpp"
#include "catch2/matchers/catch_matchers_vector.hpp"   // For UnorderedEquals
#include "drools_parser.hpp"
#include "knowledge_base.hpp"
#include "query_result.hpp"
#include "stateful_session.hpp"

// A dedicated fixture for query tests to handle KB and session creation.
struct QueryTestFixture {
    std::shared_ptr<KnowledgeBase> kb;
    std::unique_ptr<StatefulSession> session;

    void build(std::string const& drl) {
        ParsingResult result;
        kb = build_knowledge_base(drl, result);
        if (!result.success) {
            FAIL("DRL parsing failed: " << (result.errors.empty() ? "Unknown error" : result.errors[0].to_string()));
        }
        REQUIRE(kb != nullptr);
        session = kb->create_session();
        REQUIRE(session != nullptr);
    }

    std::shared_ptr<Fact> createFact(std::string const& type, std::map<std::string, ConstraintValue> const& fields) {
        auto fact = std::make_shared<Fact>();
        fact->type = type;
        fact->fields = fields;
        session->add_fact(fact);
        return fact;
    }
};

TEST_CASE_METHOD(QueryTestFixture, "Query: Non-Parameterized (Live) Queries", "[query][terminal]") {
    build(R"(
        declare Item name: String end
        query "all-items" $i: Item() end
    )");

    SECTION("Initially, the query should return no results") {
        QueryResult results = session->execute_query("all-items");
        REQUIRE(results.empty());
    }

    // Prepare facts for subsequent sections
    auto item1 = createFact("Item", {{"name", "Book"}});
    auto item2 = createFact("Item", {{"name", "Pen"}});

    SECTION("After inserting facts, the live query should find them") {
        QueryResult results = session->execute_query("all-items");
        REQUIRE(results.size() == 2);
    }

    SECTION("After retracting a fact, query results should update") {
        session->retract_fact(item1);
        QueryResult results = session->execute_query("all-items");
        REQUIRE(results.size() == 1);

        // Use the safe and expressive API to check the result
        auto name = results.single().getFieldAs<std::string>("$i", "name");
        REQUIRE(name.has_value());
        CHECK(name.value() == "Pen");
    }
}

TEST_CASE_METHOD(QueryTestFixture, "Query: Parameterized with empty body", "[query][input][edgecase]") {
    build(R"(
        declare Item name: String end
        query "get-item-with-empty-body"(Item $i) end
    )");

    auto itemA = std::make_shared<Fact>();
    itemA->type = "Item";
    itemA->id = 999;
    itemA->fields["name"] = "Apple";

    SECTION("Executing a body-less query finds the input parameter") {
        QueryResult results = session->execute_query("get-item-with-empty-body", {itemA});
        REQUIRE(results.size() == 1);
        auto row = results.single();
        REQUIRE(row.get("$i").has_value());
        CHECK(row.get("$i").value()->id == itemA->id);
    }
}

TEST_CASE_METHOD(QueryTestFixture, "Query: Parameterized with a join", "[query][input][join]") {
    build(R"(
        declare Customer id: int, name: String end
        declare Order customerId: int, product: String end
        query "find-orders-for-customer"(Customer $c)
            $o: Order(customerId == $c.id)
        end
    )");

    createFact("Order", {{"customerId", 101}, {"product", "Laptop"}});
    auto order2 = createFact("Order", {{"customerId", 202}, {"product", "Mouse"}});
    createFact("Order", {{"customerId", 101}, {"product", "Keyboard"}});

    SECTION("Query for a customer with multiple orders") {
        auto cust1_arg = std::make_shared<Fact>();
        cust1_arg->type = "Customer";
        cust1_arg->fields = {{"id", 101}, {"name", "Alice"}};

        QueryResult results = session->execute_query("find-orders-for-customer", {cust1_arg});
        REQUIRE(results.size() == 2);

        for (auto const& row : results) {
            REQUIRE(row.get("$c").has_value());   // Parameter should be present
            REQUIRE(row.get("$o").has_value());
        }

        std::vector<std::string> products = results.getColumnFieldAs<std::string>("$o", "product");
        REQUIRE_THAT(products, Catch::Matchers::UnorderedEquals(std::vector<std::string>{"Laptop", "Keyboard"}));
    }

    // The rest of the test cases are unchanged but will now work with the completed API.
    SECTION("Query for a customer with one order") {
        auto cust2_arg = std::make_shared<Fact>();
        cust2_arg->type = "Customer";
        cust2_arg->fields = {{"id", 202}, {"name", "Bob"}};

        QueryResult results = session->execute_query("find-orders-for-customer", {cust2_arg});
        REQUIRE(results.size() == 1);

        auto fact_opt = results.single().get("$o");
        REQUIRE(fact_opt.has_value());
        CHECK((*fact_opt)->id == order2->id);
    }

    SECTION("Query for a customer with no orders") {
        auto cust3_arg = std::make_shared<Fact>();
        cust3_arg->type = "Customer";
        cust3_arg->fields = {{"id", 303}, {"name", "Charlie"}};

        QueryResult results = session->execute_query("find-orders-for-customer", {cust3_arg});
        REQUIRE(results.empty());
    }
}
