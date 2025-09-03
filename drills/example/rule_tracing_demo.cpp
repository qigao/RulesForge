#include "knowledge_base.hpp"
#include "stateful_session.hpp"

#include <iostream>

int main() {
    std::string drl = R"(
        declare Customer
            id: int
            name: String
            age: int
            balance: double
            status: String
        end
        declare VipCustomer
            id: int
            name: String
        end
        declare SeniorDiscount
            customerId: int
            discount: double
        end
        
        rule "Promote to VIP"
        salience 10
        when
            $c : Customer(balance > 50000.0, status == "Active")
        then
            drools.insert({type: "VipCustomer", id: $c.id, name: $c.name});
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
        
        // Enable rule execution tracing
        session->enable_tracing(true);

        std::cout << "=== Rule Execution Tracing Demo ===\n\n";

        // Create test customers
        std::vector<std::shared_ptr<Fact>> customers;
        
        // Young wealthy customer
        auto alice = std::make_shared<Fact>();
        alice->type = "Customer";
        alice->fields["id"] = (int64_t)1;
        alice->fields["name"] = "Alice Johnson";
        alice->fields["age"] = (int64_t)35;
        alice->fields["balance"] = 75000.0;
        alice->fields["status"] = "Active";
        customers.push_back(alice);
        
        // Senior customer with modest balance
        auto bob = std::make_shared<Fact>();
        bob->type = "Customer";
        bob->fields["id"] = (int64_t)2;
        bob->fields["name"] = "Bob Smith";
        bob->fields["age"] = (int64_t)67;
        bob->fields["balance"] = 25000.0;
        bob->fields["status"] = "Active";
        customers.push_back(bob);
        
        // Young customer with low balance
        auto charlie = std::make_shared<Fact>();
        charlie->type = "Customer";
        charlie->fields["id"] = (int64_t)3;
        charlie->fields["name"] = "Charlie Brown";
        charlie->fields["age"] = (int64_t)28;
        charlie->fields["balance"] = 15000.0;
        charlie->fields["status"] = "Active";
        customers.push_back(charlie);
        
        // Wealthy senior customer (triggers both rules)
        auto diana = std::make_shared<Fact>();
        diana->type = "Customer";
        diana->fields["id"] = (int64_t)4;
        diana->fields["name"] = "Diana Wilson";
        diana->fields["age"] = (int64_t)72;
        diana->fields["balance"] = 85000.0;
        diana->fields["status"] = "Active";
        customers.push_back(diana);

        std::cout << "Adding " << customers.size() << " customers to the session...\n\n";

        // Add facts and fire rules
        session->add_facts(customers);
        int rules_fired = session->fire_all_rules();

        std::cout << "Rules fired: " << rules_fired << "\n";
        std::cout << "Total facts in session: " << session->get_fact_count() << "\n\n";

        // Display execution trace
        std::cout << session->get_execution_trace() << "\n";
        
        // Display performance summary
        std::cout << session->get_rule_performance_summary() << "\n";
        
        // Analyze specific customer
        std::cout << "=== Events for Diana (ID: " << diana->id << ") ===\n";
        auto diana_events = session->get_tracer().get_fact_events(diana->id);
        for (size_t i = 0; i < diana_events.size(); ++i) {
            auto const& event = diana_events[i];
            std::cout << "[" << i << "] ";
            
            switch (event.type) {
                case RuleTraceEvent::Type::FACT_ADDED:
                    std::cout << "ADDED: " << event.fact_type;
                    break;
                case RuleTraceEvent::Type::RULE_FIRED:
                    std::cout << "RULE_FIRED: " << event.rule_name;
                    if (event.duration_us > 0) {
                        std::cout << " (" << event.duration_us << "μs)";
                    }
                    break;
                case RuleTraceEvent::Type::RULE_MATCHED:
                    std::cout << "RULE_MATCHED: " << event.rule_name;
                    break;
                default:
                    std::cout << "OTHER: " << event.description;
                    break;
            }
            std::cout << "\n";
        }

        return 0;

    } catch (std::exception const& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}