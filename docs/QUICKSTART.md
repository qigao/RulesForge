# Drills Rules Engine - Quick Start Guide

*Get up and running in 15 minutes*

## What is Drills?

Drills is a high-performance C++ rules engine implementing the **Rete algorithm** with **JavaScript** integration. Think "business logic as code" - write your complex conditions in a declarative language, execute actions in JavaScript.

## 5-Minute Example

### 1. Write Your Business Rules

Create `my_rules.rfl`:
```rfl
// Define your data model
declare Customer
    id: int
    name: String
    age: int
    balance: double
    status: String
end

declare VipCustomer
    customerId: int
    reason: String
end

// Business rule: High-value customers become VIP
rule "Promote to VIP"
salience 10
when
    $c: Customer(balance > 10000, status == "Active")
    not VipCustomer(customerId == $c.id)
then
    // JavaScript action - 'c' is the matched customer
    console.log(`Promoting ${c.name} to VIP status`);
    rfl.insert({
        type: "VipCustomer", 
        customerId: c.id,
        reason: "High Balance: $" + c.balance
    });
end

// Query to find all VIP customers
query "find_vip_customers"
    $vip: VipCustomer()
    $customer: Customer(id == $vip.customerId)
end
```

### 2. Use in Your C++ Application

```cpp
#include "knowledge_base.hpp"
#include "stateful_session.hpp"
#include "fact_builder.hpp"

int main() {
    // Load and compile rules
    std::string rfl = read_file("my_rules.rfl");
    ParsingResult result;
    auto kb = build_knowledge_base(rfl, result);
    
    if (!result.success) {
        for (auto& err : result.errors) {
            std::cerr << err.to_string() << std::endl;
        }
        return 1;
    }

    // Create session and add facts
    auto session = kb->create_session();
    
    // Add customer data using type-safe builder
    auto customer = CUSTOMER()
        .id(1001)
        .name("Alice Johnson")
        .age(35)
        .balance(15000.0)
        .status("Active")
        .build();
        
    session->add_fact(customer);
    
    // Fire rules and see the magic happen
    int rules_fired = session->fire_all_rules();
    std::cout << "Fired " << rules_fired << " rules\n";
    
    // Query results
    auto vips = session->execute_query("find_vip_customers");
    std::cout << "Found " << vips.size() << " VIP customers\n";
    
    return 0;
}
```

### 3. Expected Output
```
Promoting Alice Johnson to VIP status
Fired 1 rules
Found 1 VIP customers
```

## Key Concepts in 2 Minutes

### Facts = Your Data
```cpp
// Traditional approach - manual fact creation
auto fact = std::make_shared<Fact>();
fact->type = "Customer";
fact->fields["name"] = "John";
fact->fields["balance"] = 5000.0;

// Drills approach - type-safe builders
auto customer = CUSTOMER()
    .name("John")
    .balance(5000.0)
    .build();
```

### Rules = Your Business Logic
```rfl
rule "Rule Name"
when
    // Conditions - what data pattern to match?
    $customer: Customer(balance > 1000, status == "Active")
then
    // Actions - what to do when matched?
    console.log("High-value customer: " + customer.name);
    rfl.insert({type: "HighValueCustomer", id: customer.id});
end
```

### Sessions = Your Working Memory
```cpp
auto session = kb->create_session();
session->add_fact(customer_data);      // Add data
int fired = session->fire_all_rules(); // Process rules
auto results = session->execute_query("my_query"); // Query results
```

## Common Patterns

### Pattern 1: Data Validation
```rfl
rule "Validate Customer"
when
    $c: Customer(age < 18)
then
    console.error(`Customer ${c.name} is underage: ${c.age}`);
    rfl.insert({type: "ValidationError", message: "Customer must be 18+"});
end
```

### Pattern 2: Data Transformation
```rfl
rule "Calculate Credit Score"
when
    $c: Customer(balance > 0)
    not CreditScore(customerId == $c.id)
then
    let score = Math.min(850, Math.max(300, c.balance / 100 + 600));
    rfl.insert({
        type: "CreditScore", 
        customerId: c.id, 
        score: score
    });
end
```

### Pattern 3: Complex Conditions
```rfl
rule "Loyal Customer Reward"
when
    $c: Customer(status == "Active")
    $orders: Number() from accumulate(
        Order(customerId == $c.id, amount > 100),
        count()
    )
    eval($orders >= 5)
then
    console.log(`${c.name} has ${orders} large orders - reward time!`);
    rfl.insert({type: "Reward", customerId: c.id, points: 1000});
end
```

## Build and Run

### Prerequisites
- CMake 3.20+
- C++20 compiler (MSVC 2022, GCC 11+, Clang 13+)
- vcpkg (for dependencies)

### Quick Build
```bash
git clone <your-repo>
cd drills
cmake --preset=default
cmake --build build
```

### Run Examples
```bash
# Basic example
./build/bin/drills_engine examples/basic.rfl

# Performance demo
./build/bin/memory_optimization_demo
```

## What's Next?

- **Simple rules?** → Continue with the [User Guide](USER_GUIDE.md)
- **Production deployment?** → See [Deployment Guide](DEPLOYMENT.md)

## Need Help?

- 📖 **Documentation**: All guides are in `/docs/`
- 🐛 **Issues**: GitHub Issues for bug reports
- 💡 **Examples**: Check `/drills/example/` directory
- ⚡ **Performance**: See memory optimization examples

---
*Built with ❤️ using modern C++20, QuickJS, and the Rete algorithm*