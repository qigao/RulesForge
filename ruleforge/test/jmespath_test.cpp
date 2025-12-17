#include "catch2/catch_all.hpp"
#include "drools_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"
#include "drools_js_manager.hpp"

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

TEST_CASE("JMESPath: Enhanced Array Operations", "[jmespath]") {
    auto session = build_session(R"(
        declare ProductCatalog
            name: String
            data: String
        end
        rule "Process Product Array"
        when
            $catalog : ProductCatalog(name == "electronics")
        then
            // Test array projection: items[*].price
            var prices = jmespath(catalog_json, 'products[*].price');
            console.log("All prices:", JSON.stringify(prices));
            
            // Test array filtering: products[?price > 500]
            var expensive = jmespath(catalog_json, 'products[?price > 500]');
            console.log("Expensive products:", JSON.stringify(expensive));
            
            // Test built-in functions
            var totalProducts = jmespath(catalog_json, 'products | length(@)');
            var maxPrice = jmespath(catalog_json, 'products[*].price | max(@)');
            var totalValue = jmespath(catalog_json, 'products[*].price | sum(@)');
            
            console.log("Total products:", totalProducts);
            console.log("Max price:", maxPrice);
            console.log("Total value:", totalValue);
        end
    )");

    auto catalog = std::make_shared<Fact>();
    catalog->type = "ProductCatalog";
    catalog->fields["name"] = std::string("electronics");
    catalog->fields["data"] = std::string(R"({
        "category": "Electronics",
        "products": [
            {"id": 1, "name": "Laptop", "price": 999.99, "brand": "TechCorp"},
            {"id": 2, "name": "Mouse", "price": 29.99, "brand": "TechCorp"},
            {"id": 3, "name": "Monitor", "price": 599.99, "brand": "DisplayCo"},
            {"id": 4, "name": "Keyboard", "price": 79.99, "brand": "TechCorp"},
            {"id": 5, "name": "Phone", "price": 799.99, "brand": "MobileTech"}
        ],
        "metadata": {
            "last_updated": "2024-01-15",
            "total_items": 5
        }
    })");

    session->add_fact(catalog);
    int fired = session->fire_all_rules();

    CHECK(fired == 1);
}

TEST_CASE("JMESPath: JSON Processing Test", "[jmespath]") {
    auto session = build_session(R"(
        declare JsonEvent
            type: String
            data: String
        end
        rule "Process JSON Event"
        when
            $event : JsonEvent(type == "order")
        then
            // Use JSON data from fact and process with jmespath
            var itemCount = jmespath($event.data, 'items | length(@)');
            console.log("Item count:", itemCount);
        end
    )");

    // Create event with multiple items
    auto event = std::make_shared<Fact>();
    event->type = "JsonEvent";
    event->fields["type"] = std::string("order");
    event->fields["data"] = std::string(R"({
        "customer_id": 456,
        "items": [
            {"id": 1, "name": "Book", "price": 15.99},
            {"id": 2, "name": "Pen", "price": 2.50},
            {"id": 3, "name": "Notebook", "price": 8.75}
        ]
    })");

    session->add_fact(event);
    int fired = session->fire_all_rules();

    CHECK(fired == 1);
}

TEST_CASE("JMESPath: Function Binding Test", "[jmespath]") {
    auto session = build_session(R"(
        declare TestFact
            name: String
        end
        rule "Test JMESPath Function"
        when
            $fact : TestFact(name == "test")
        then
            // Test direct jmespath function call
            var jsonData = '{"user": {"profile": {"age": 25, "name": "Alice"}}, "orders": [{"id": 1, "amount": 100}, {"id": 2, "amount": 200}]}';
            var age = jmespath(jsonData, 'user.profile.age');
            var name = jmespath(jsonData, 'user.profile.name');
            var totalOrders = jmespath(jsonData, 'orders | length(@)');
            var amounts = jmespath(jsonData, 'orders[*].amount');
            
            console.log("Age:", age);
            console.log("Name:", name);
            console.log("Total Orders:", totalOrders);
            console.log("Amounts:", amounts);
        end
    )");

    auto fact = std::make_shared<Fact>();
    fact->type = "TestFact";
    fact->fields["name"] = std::string("test");

    session->add_fact(fact);
    int fired = session->fire_all_rules();

    CHECK(fired == 1);
}

TEST_CASE("JMESPath: Error Handling", "[jmespath]") {
    auto session = build_session(R"(
        declare ErrorTest
            name: String
        end
        rule "Test Error Cases"
        when
            $fact : ErrorTest(name == "test")
        then
            try {
                // Test invalid JSON
                var result1 = jmespath("invalid json", 'user.name');
            } catch (e) {
                console.log("Caught JSON error:", e);
            }
            
            try {
                // Test invalid JMESPath expression
                var result2 = jmespath('{"valid": "json"}', 'invalid[[[syntax');
            } catch (e) {
                console.log("Caught JMESPath error:", e);
            }
        end
    )");

    auto fact = std::make_shared<Fact>();
    fact->type = "ErrorTest";
    fact->fields["name"] = std::string("test");

    session->add_fact(fact);
    int fired = session->fire_all_rules();

    CHECK(fired == 1);
}

TEST_CASE("JMESPath: Real-world E-commerce Analysis", "[jmespath]") {
    auto session = build_session(R"(
        declare OrderAnalysis
            type: String
            payload: String
        end
        rule "Comprehensive Order Analysis"
        when
            $analysis : OrderAnalysis(type == "monthly_report")
        then
            // Extract high-value orders using filters
            var highValueOrders = jmespath(analysis_json, 'orders[?total > 1000]');
            console.log("High value orders count:", jmespath(JSON.stringify(highValueOrders), '@ | length(@)'));
            
            // Get all customer tiers from orders  
            var customerTiers = jmespath(analysis_json, 'orders[*].customer.tier');
            console.log("Customer tiers:", JSON.stringify(customerTiers));
            
            // Calculate revenue metrics
            var totalRevenue = jmespath(analysis_json, 'orders[*].total | sum(@)');
            var avgOrderValue = jmespath(analysis_json, 'orders[*].total | sum(@)');
            var maxOrder = jmespath(analysis_json, 'orders[*].total | max(@)');
            var orderCount = jmespath(analysis_json, 'orders | length(@)');
            
            console.log("Total Revenue:", totalRevenue);
            console.log("Max Single Order:", maxOrder);
            console.log("Total Orders:", orderCount);
            
            // Premium customers analysis
            var premiumCustomers = jmespath(analysis_json, 'orders[?customer.tier == `premium`]');
            var premiumRevenue = jmespath(JSON.stringify(premiumCustomers), '[*].total | sum(@)');
            
            console.log("Premium customers revenue:", premiumRevenue);
            
            // Product category analysis
            var categories = jmespath(analysis_json, 'orders[*].items[*].category');
            console.log("All categories:", JSON.stringify(categories));
        end
    )");

    auto analysis = std::make_shared<Fact>();
    analysis->type = "OrderAnalysis";
    analysis->fields["type"] = std::string("monthly_report");
    analysis->fields["payload"] = std::string(R"({
        "report_period": "2024-01",
        "orders": [
            {
                "id": "ORD-001",
                "date": "2024-01-15",
                "customer": {
                    "id": "CUST-123",
                    "name": "Alice Johnson",
                    "tier": "premium",
                    "location": {
                        "country": "US",
                        "state": "CA",
                        "city": "San Francisco"
                    }
                },
                "items": [
                    {"id": "PROD-001", "name": "Gaming Laptop", "category": "electronics", "price": 1299.99, "quantity": 1},
                    {"id": "PROD-002", "name": "Wireless Mouse", "category": "accessories", "price": 79.99, "quantity": 2}
                ],
                "total": 1459.97,
                "status": "delivered",
                "payment": {
                    "method": "credit_card",
                    "processor": "stripe"
                }
            },
            {
                "id": "ORD-002",
                "date": "2024-01-16",
                "customer": {
                    "id": "CUST-456",
                    "name": "Bob Smith",
                    "tier": "regular",
                    "location": {
                        "country": "US",
                        "state": "NY",
                        "city": "New York"
                    }
                },
                "items": [
                    {"id": "PROD-003", "name": "Office Chair", "category": "furniture", "price": 299.99, "quantity": 1},
                    {"id": "PROD-004", "name": "Standing Desk", "category": "furniture", "price": 599.99, "quantity": 1}
                ],
                "total": 899.98,
                "status": "shipped",
                "payment": {
                    "method": "paypal",
                    "processor": "paypal"
                }
            },
            {
                "id": "ORD-003",
                "date": "2024-01-17",
                "customer": {
                    "id": "CUST-789",
                    "name": "Charlie Brown",
                    "tier": "premium",
                    "location": {
                        "country": "US",
                        "state": "TX",
                        "city": "Austin"
                    }
                },
                "items": [
                    {"id": "PROD-005", "name": "4K Monitor", "category": "electronics", "price": 799.99, "quantity": 2},
                    {"id": "PROD-006", "name": "Mechanical Keyboard", "category": "accessories", "price": 149.99, "quantity": 1}
                ],
                "total": 1749.97,
                "status": "processing",
                "payment": {
                    "method": "credit_card",
                    "processor": "square"
                }
            }
        ],
        "summary": {
            "total_orders": 3,
            "total_revenue": 4109.92,
            "avg_order_value": 1369.97
        }
    })");

    session->add_fact(analysis);
    int fired = session->fire_all_rules();

    CHECK(fired == 1);
}

