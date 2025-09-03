#include "fact_builder.hpp"
#include "typed_fact_builders.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

#include <iostream>
#include <vector>

int main() {
    std::string drl = R"(
        declare Customer
            id: int
            name: String
            age: int
            balance: double
            tier: String
            status: String
        end
        declare Order
            id: int
            customerId: int
            item: String
            amount: double
            status: String
        end
        declare HighValueCustomer
            customerId: int
            totalValue: double
        end
        declare SeniorDiscount
            customerId: int
            discount: double
        end
        
        rule "High Value Customer"
        salience 10
        when
            $c : Customer($cid : id, balance > 100000.0, status == "Active")
            $total : Number() from accumulate(
                Order(customerId == $cid, status == "Completed", $amt : amount),
                sum($amt)
            )
            eval($total > 50000.0)
        then
            drools.insert({type: "HighValueCustomer", customerId: $cid, totalValue: $total});
        end
        
        rule "Senior Discount"
        salience 5
        when
            $c : Customer(age >= 65, status == "Active")
        then
            drools.insert({type: "SeniorDiscount", customerId: $c.id, discount: 0.15});
        end
    )";

    try {
        ParsingResult result;
        auto kb = build_knowledge_base(drl, result);
        if (!result.success) {
            for (auto const& err : result.errors) {
                std::cerr << "Error: " << err.to_string() << std::endl;
            }
            return 1;
        }

        auto session = kb->create_session();
        session->enable_tracing(true);

        std::cout << "=== Fact Builder Demo ===\n\n";
        
        // === Old way (verbose, error-prone) ===
        std::cout << "Old way (verbose):\n";
        auto old_customer = std::make_shared<Fact>();
        old_customer->type = "Customer";
        old_customer->fields["id"] = (int64_t)1;
        old_customer->fields["name"] = "Old Style Customer";
        old_customer->fields["age"] = (int64_t)45;
        old_customer->fields["balance"] = 150000.0;
        old_customer->fields["tier"] = "Gold";
        old_customer->fields["status"] = "Active";
        
        std::cout << "  Created customer: " << std::get<std::string>(old_customer->fields.at("name")) << "\n\n";
        
        // === New way (clean, type-safe) ===
        std::cout << "New way (with builders):\n";
        
        // Create customers using typed builders
        auto customers = std::vector<std::shared_ptr<Fact>>{
            CUSTOMER()
                .id(1)
                .name("Alice Johnson")
                .age(35)
                .balance(125000.0)
                .gold()
                .active()
                .build(),
                
            CUSTOMER()
                .id(2)
                .name("Bob Wilson") 
                .age(72)
                .balance(85000.0)
                .silver()
                .active()
                .build(),
                
            CUSTOMER()
                .id(3)
                .name("Charlie Brown")
                .age(28)
                .balance(25000.0)
                .bronze()
                .active()
                .build()
        };
        
        // Create orders using builder pattern
        auto orders = std::vector<std::shared_ptr<Fact>>{
            ORDER().id(101).customer_id(1).item("Premium Service").amount(25000.0).delivered().build(),
            ORDER().id(102).customer_id(1).item("Consulting").amount(35000.0).delivered().build(), 
            ORDER().id(103).customer_id(2).item("Standard Package").amount(5000.0).delivered().build(),
            ORDER().id(104).customer_id(3).item("Basic Service").amount(1500.0).delivered().build()
        };
        
        std::cout << "Created " << customers.size() << " customers and " << orders.size() << " orders\n\n";
        
        // Batch operations with type-safe builders
        std::cout << "Creating additional test data using batch builders...\n";
        
        auto additional_customers = FactBuilders::create_multiple("Customer", 3,
            [](auto& builder, size_t index) {
                builder.set("id", static_cast<int64_t>(10 + index))
                       .set("name", "Batch Customer " + std::to_string(index + 1))
                       .set("age", static_cast<int64_t>(30 + index * 10))
                       .set("balance", 20000.0 + index * 30000.0)
                       .set("tier", index == 2 ? "Gold" : "Silver")
                       .set("status", "Active");
            });
        
        customers.insert(customers.end(), additional_customers.begin(), additional_customers.end());
        
        // Add all facts to session
        session->add_facts(customers);
        session->add_facts(orders);
        
        // Fire rules
        int rules_fired = session->fire_all_rules();
        
        std::cout << "\nResults:\n";
        std::cout << "Rules fired: " << rules_fired << "\n";
        std::cout << "Total facts: " << session->get_fact_count() << "\n\n";
        
        // Show execution trace
        std::cout << session->get_execution_trace() << "\n";
        std::cout << session->get_rule_performance_summary() << "\n";
        
        // === Demonstrate builder reusability ===
        std::cout << "=== Builder Template Pattern ===\n";
        
        auto active_customer_template = CUSTOMER()
            .active()
            .tier("Silver")
            .balance(50000.0);
        
        // Reuse template for multiple customers
        auto templated_customers = std::vector<std::shared_ptr<Fact>>{
            active_customer_template.set("id", 201).set("name", "Template Customer 1").build(),
            active_customer_template.set("id", 202).set("name", "Template Customer 2").age(45).build(),
            active_customer_template.set("id", 203).set("name", "Template Customer 3").gold().build()
        };
        
        std::cout << "Created " << templated_customers.size() << " customers from template\n";
        for (auto const& customer : templated_customers) {
            auto name = std::get<std::string>(customer->fields.at("name"));
            auto tier = std::get<std::string>(customer->fields.at("tier"));
            std::cout << "  - " << name << " (" << tier << " tier)\n";
        }
        
        return 0;

    } catch (std::exception const& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}