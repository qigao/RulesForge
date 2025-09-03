#include "memory_optimized_types.hpp"
#include "object_pool.hpp"
#include "optimized_fact_builder.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

#include <iostream>
#include <chrono>
#include <vector>

class PerformanceTimer {
public:
    explicit PerformanceTimer(std::string name) : name_(std::move(name)) {
        start_ = std::chrono::high_resolution_clock::now();
    }
    
    ~PerformanceTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
        std::cout << name_ << ": " << duration.count() << "μs" << std::endl;
    }

private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_;
};

int main() {
    std::string drl = R"(
        declare Order
            id: int
            customerId: int
            amount: double
            item: String
            status: String
        end
        declare Customer
            id: int
            name: String
            totalSpent: double
            tier: String
        end
        declare HighValueOrder
            orderId: int
            amount: double
        end
        declare LoyalCustomer
            customerId: int
            totalSpent: double
        end
        
        rule "High Value Order"
        salience 10
        when
            $o : Order(amount > 1000.0, status == "Completed")
        then
            drools.insert({type: "HighValueOrder", orderId: $o.id, amount: $o.amount});
        end
        
        rule "Loyal Customer"
        salience 5
        when
            $c : Customer(totalSpent > 5000.0)
        then
            drools.insert({type: "LoyalCustomer", customerId: $c.id, totalSpent: $c.totalSpent});
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

        std::cout << "=== Memory Optimization Demo ===\n\n";
        
        // Pre-warm the memory pools
        std::cout << "Pre-warming memory pools...\n";
        GlobalPools::reserve_all(1000);
        
        // Initialize string interner with common field names
        auto& interner = StringInterner::instance();
        interner.intern_persistent("id");
        interner.intern_persistent("customerId");
        interner.intern_persistent("amount");
        interner.intern_persistent("item");
        interner.intern_persistent("status");
        interner.intern_persistent("name");
        interner.intern_persistent("totalSpent");
        interner.intern_persistent("tier");
        
        constexpr int NUM_CUSTOMERS = 500;
        constexpr int NUM_ORDERS = 2000;
        
        auto session = kb->create_session();
        session->enable_tracing(false); // Disable for performance test
        
        std::cout << "\n=== Phase 1: Standard Fact Creation ===\n";
        std::vector<std::shared_ptr<Fact>> std_facts;
        {
            PerformanceTimer timer("Standard fact creation");
            std_facts.reserve(NUM_CUSTOMERS + NUM_ORDERS);
            
            // Create customers
            for (int i = 1; i <= NUM_CUSTOMERS; ++i) {
                auto customer = std::make_shared<Fact>();
                customer->type = "Customer";
                customer->fields["id"] = static_cast<int64_t>(i);
                customer->fields["name"] = "Customer" + std::to_string(i);
                customer->fields["totalSpent"] = 1000.0 + (i * 15.0);
                customer->fields["tier"] = (i % 4 == 0) ? "Gold" : "Silver";
                std_facts.push_back(customer);
            }
            
            // Create orders
            for (int i = 1; i <= NUM_ORDERS; ++i) {
                auto order = std::make_shared<Fact>();
                order->type = "Order";
                order->fields["id"] = static_cast<int64_t>(i);
                order->fields["customerId"] = static_cast<int64_t>(1 + (i % NUM_CUSTOMERS));
                order->fields["amount"] = 100.0 + (i * 2.5);
                order->fields["item"] = "Product" + std::to_string(i % 50);
                order->fields["status"] = "Completed";
                std_facts.push_back(order);
            }
        }
        
        std::cout << "\n=== Phase 2: Optimized Fact Creation ===\n";
        std::vector<std::shared_ptr<Fact>> opt_facts;
        {
            PerformanceTimer timer("Optimized fact creation");
            opt_facts.reserve(NUM_CUSTOMERS + NUM_ORDERS);
            
            // Create customers with optimized builder
            for (int i = 1; i <= NUM_CUSTOMERS; ++i) {
                auto customer = FAST_CUSTOMER()
                    .id(i)
                    .name("Customer" + std::to_string(i))
                    .set("totalSpent", 1000.0 + (i * 15.0))
                    .tier((i % 4 == 0) ? "Gold" : "Silver")
                    .build();
                opt_facts.push_back(customer);
            }
            
            // Create orders with optimized builder
            for (int i = 1; i <= NUM_ORDERS; ++i) {
                auto order = FAST_FACT("Order")
                    .set("id", i)
                    .set("customerId", 1 + (i % NUM_CUSTOMERS))
                    .set("amount", 100.0 + (i * 2.5))
                    .set("item", "Product" + std::to_string(i % 50))
                    .set("status", "Completed")
                    .build();
                opt_facts.push_back(order);
            }
        }
        
        std::cout << "\n=== Phase 3: Rules Engine Performance ===\n";
        
        // Test with standard facts
        {
            PerformanceTimer timer("Standard facts processing");
            session->add_facts(std_facts);
            int rules_fired = session->fire_all_rules();
            std::cout << "  Rules fired: " << rules_fired << std::endl;
            std::cout << "  Total facts: " << session->get_fact_count() << std::endl;
        }
        
        // Clear session for second test
        session = kb->create_session();
        
        // Test with optimized facts
        {
            PerformanceTimer timer("Optimized facts processing");
            session->add_facts(opt_facts);
            int rules_fired = session->fire_all_rules();
            std::cout << "  Rules fired: " << rules_fired << std::endl;
            std::cout << "  Total facts: " << session->get_fact_count() << std::endl;
        }
        
        std::cout << "\n=== Memory Statistics ===\n";
        auto stats = PoolStatsCollector::collect();
        std::cout << PoolStatsCollector::format_stats(stats);
        
        std::cout << "\n=== String Interning Statistics ===\n";
        std::cout << "Interned strings: " << interner.size() << std::endl;
        
        std::cout << "\n=== Demo: FastFact vs Regular Fact ===\n";
        
        // Demonstrate FastFact usage
        auto fast_fact = std::make_unique<FastFact>("TestType");
        fast_fact->set_field("field1", "value1");
        fast_fact->set_field("field2", 42);
        fast_fact->set_field("field3", 3.14);
        
        std::cout << "FastFact type: " << fast_fact->type << std::endl;
        std::cout << "FastFact fields: " << fast_fact->fields.size() << std::endl;
        
        // Convert to regular Fact
        auto regular_fact = fast_fact->to_fact();
        std::cout << "Converted Fact type: " << regular_fact->type << std::endl;
        std::cout << "Converted Fact fields: " << regular_fact->fields.size() << std::endl;
        
        std::cout << "\n=== Memory Pool Demo ===\n";
        
        {
            PerformanceTimer timer("Pool allocation stress test");
            std::vector<decltype(GlobalPools::make_pooled_fact())> pooled_facts;
            pooled_facts.reserve(1000);
            
            for (int i = 0; i < 1000; ++i) {
                auto fact = GlobalPools::make_pooled_fact();
                fact->type = "PooledFact";
                fact->fields["id"] = static_cast<int64_t>(i);
                fact->fields["data"] = "test_data_" + std::to_string(i);
                pooled_facts.push_back(std::move(fact));
            }
            
            std::cout << "  Created " << pooled_facts.size() << " pooled facts" << std::endl;
        } // Facts automatically returned to pool when destructed
        
        // Show pool recovery
        auto final_stats = PoolStatsCollector::collect();
        std::cout << "\nFinal " << PoolStatsCollector::format_stats(final_stats);
        
        return 0;

    } catch (std::exception const& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}