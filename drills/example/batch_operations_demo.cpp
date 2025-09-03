#include "knowledge_base.hpp"
#include "stateful_session.hpp"

#include <chrono>
#include <iostream>
#include <vector>

int main() {
    std::string drl = R"(
        declare Customer
            id: int
            status: String
            balance: double
        end
        declare VipCustomer
            id: int
        end
        rule "Promote to VIP"
        when
            $c : Customer(balance > 10000.0, status == "Active")
        then
            drools.insert({type: "VipCustomer", id: $c.id});
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

        // Create test data
        std::vector<std::shared_ptr<Fact>> customers;
        for (int i = 1; i <= 1000; ++i) {
            auto customer = std::make_shared<Fact>();
            customer->type = "Customer";
            customer->fields["id"] = (int64_t)i;
            customer->fields["status"] = (i % 5 == 0) ? "Inactive" : "Active";
            customer->fields["balance"] = 5000.0 + (i * 10.5);
            customers.push_back(customer);
        }

        std::cout << "=== Batch Operations Demo ===\n";
        std::cout << "Adding " << customers.size() << " customers...\n";

        auto start = std::chrono::high_resolution_clock::now();
        session->add_facts(customers);
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "Batch insertion took: " << duration.count() << " μs\n";

        int rules_fired = session->fire_all_rules();
        std::cout << "Rules fired: " << rules_fired << "\n";
        std::cout << "Total facts in session: " << session->get_fact_count() << "\n";

        // Demonstrate batch retraction
        std::vector<std::shared_ptr<Fact>> to_remove(customers.begin(), customers.begin() + 100);
        
        start = std::chrono::high_resolution_clock::now();
        session->retract_facts(to_remove);
        end = std::chrono::high_resolution_clock::now();
        
        duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "Batch retraction of 100 facts took: " << duration.count() << " μs\n";
        std::cout << "Remaining facts: " << session->get_fact_count() << "\n";

        return 0;

    } catch (std::exception const& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}