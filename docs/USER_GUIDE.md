# Drills Rules Engine - Complete User Guide

*Master the art of declarative business logic*

## Table of Contents

1. [**Core Concepts**](#1-core-concepts)
2. [**DRL Language Reference**](#2-drl-language-reference)
3. [**JavaScript Integration**](#3-javascript-integration)
4. [**Advanced Patterns**](#4-advanced-patterns)
5. [**Performance Guidelines**](#5-performance-guidelines)
6. [**Troubleshooting**](#6-troubleshooting)
7. [**Deployment Guide**](#7-deployment-guide)

---

## 1. Core Concepts

### The Rete Algorithm in Practice

Drills implements the **Rete algorithm**, a powerful pattern-matching technique that:

- ✅ **Incremental Processing** - Only recompute what changed
- ✅ **Memory Networks** - Store intermediate results for speed  
- ✅ **Conflict Resolution** - Handle multiple rule matches intelligently
- ✅ **Truth Maintenance** - Automatically retract derived facts when premises change

```drl
rule "Price Alert"
when
    $product: Product(price < 100)
    $user: User(interests contains $product.category)
then
    // Only fires when BOTH conditions are met
    // Automatically retracts alert if price goes above 100
    drools.insert({
        type: "PriceAlert", 
        userId: user.id, 
        productId: product.id
    });
end
```

### Working Memory vs Knowledge Base

```cpp
// Knowledge Base = Immutable compiled rules (thread-safe)
auto kb = build_knowledge_base(drl_source, result);

// Session = Mutable working memory (one per thread)
auto session1 = kb->create_session(); // Thread 1
auto session2 = kb->create_session(); // Thread 2

// Multiple sessions can share the same knowledge base
session1->add_fact(customer1);
session2->add_fact(customer2); // Independent data
```

### Data Flow: Facts, Rules, and the Engine

Understanding how data moves through the Drills engine is crucial. It's a continuous cycle of **Facts** (your input data) interacting with **Rules** (your defined logic) within the engine's **Working Memory**.

1.  **Rules Ingested into Knowledge Base:**
    *   Your rules, whether defined in DRL files, decision tables (like CSVs), or other formats, are first parsed and compiled into an optimized internal representation, primarily a Rete network.
    *   This compiled rule set is stored in a `KnowledgeBase`. The `KnowledgeBase` is immutable and thread-safe, acting as the blueprint for your business logic.

2.  **Facts Inserted into Working Memory:**
    *   Your application's data, referred to as "Facts," are objects (e.g., `Fact` instances in C++) that represent the current state of your system.
    *   These facts are inserted into a `StatefulSession` (the engine's working memory). Each session is mutable and typically tied to a single thread or transaction.

3.  **Engine Execution and Pattern Matching:**
    *   Once facts are in the `StatefulSession`, the Rete algorithm continuously evaluates them against the rules loaded from the `KnowledgeBase`.
    *   When a fact (or a combination of facts) matches the conditions (LHS - Left-Hand Side) of a rule, that rule is activated.

4.  **Rule Actions and Data Modification:**
    *   Activated rules execute their actions (RHS - Right-Hand Side), which are typically JavaScript code. These actions can:
        *   **Modify existing facts:** Change the properties of facts already in working memory.
        *   **Insert new facts:** Add new facts into the working memory, potentially triggering other rules.
        *   **Retract facts:** Remove facts from working memory.
        *   **Trigger external effects:** Interact with your application (e.g., logging, sending notifications, updating databases) via callbacks or external APIs.

5.  **Querying Results:**
    *   After rules have fired and the working memory has reached a stable state, you can query the `StatefulSession` to retrieve specific facts or the results of rule execution.

**In essence:** Rules are compiled once into a `KnowledgeBase`. Facts are dynamically inserted into a `StatefulSession`. The engine then continuously matches facts against rules, executing actions that can modify the facts themselves or trigger external effects, and finally, you query the session for the outcome.

---

## 2. DRL Language Reference

### 2.1 File Structure

Every DRL file follows this structure:

```drl
package com.example.business.rules

import com.example.model.Customer
import com.example.util.DateUtils

global java.util.List notifications

declare CustomEvent
    timestamp: long
    userId: int
    action: String
end

function calculateScore(balance, age) {
    return balance / age * 10;
}

query "active_customers"
    $c: Customer(status == "Active")
end

rule "Business Rule 1"
// attributes
when
    // conditions
then
    // actions
end

rule "Business Rule 2"
// more rules...
```

### 2.2 Type Declarations

Define your data schema:

```drl
declare Customer
    id: int                    // Required field
    name: String              // String type
    balance: double           // Numeric types
    active: boolean           // Boolean
    registrationDate: long    // Unix timestamp
    tags: String[]            // Array (not fully implemented)
    metadata: Object          // Generic object
end

declare VipStatus
    customerId: int
    level: String
    expires: long
end
```

**Supported Types:**
- `int` (64-bit signed integer)
- `double` (64-bit floating point)
- `String` (UTF-8 string)
- `boolean` (true/false)
- `long` (alias for int)
- `Object` (generic variant type)

### 2.3 Rule Syntax

```drl
rule "Rule Name"
    salience 10              // Priority (higher = earlier)
    agenda-group "validation" // Rule group
    when
        // Left-Hand Side (LHS) - Conditions
        $customer: Customer(
            age >= 18,           // Simple constraint
            status == "Active",  // String comparison
            balance > 1000.0     // Numeric constraint
        )
        
        // Negative condition
        not VipStatus(customerId == $customer.id)
        
        // Existential check
        exists Order(customerId == $customer.id)
        
    then
        // Right-Hand Side (RHS) - JavaScript Actions
        console.log(`Processing customer: ${customer.name}`);
        
        drools.insert({
            type: "VipStatus",
            customerId: customer.id,
            level: "Gold",
            expires: Date.now() + 365 * 24 * 60 * 60 * 1000
        });
end
```

### 2.4 Constraint Operators

| Operator | Description | Example |
|----------|-------------|---------|
| `==` | Equality | `status == "Active"` |
| `!=` | Inequality | `age != 0` |
| `<`, `<=`, `>`, `>=` | Comparison | `balance > 1000` |
| `contains` | String/array contains | `name contains "John"` |
| `matches` | Regex match | `email matches ".*@company\\.com"` |
| `in` | Value in list | `status in ("Active", "Pending")` |
| `not in` | Value not in list | `country not in ("US", "CA")` |

### 2.5 Pattern Matching

#### Basic Pattern
```drl
$customer: Customer(balance > 1000)
```

#### Multiple Constraints
```drl
$order: Order(
    amount > 100,
    status == "Completed",
    customerId == $customer.id
)
```

#### Nested Field Access
```drl
$user: User(profile.preferences.newsletter == true)
```

#### Variable Binding
```drl
$customer: Customer($customerId: id, balance > 1000)
$orders: Order(customerId == $customerId)
```

### 2.6 Advanced Patterns

#### Accumulate - Data Aggregation
```drl
rule "High Volume Customer"
when
    $customer: Customer()
    $totalSpent: Number() from accumulate(
        Order(customerId == $customer.id, $amount: amount),
        sum($amount)
    )
    eval($totalSpent > 10000)
then
    console.log(`${customer.name} spent $${totalSpent}`);
end
```

**Accumulate Functions:**
- `count()` - Count matching items
- `sum($field)` - Sum numeric field
- `min($field)` - Minimum value
- `max($field)` - Maximum value  
- `average($field)` - Average value

#### Collect - Gather Facts
```drl
rule "Bundle Orders"
when
    $customer: Customer()
    $orders: List() from collect(
        Order(customerId == $customer.id, status == "Pending")
    )
    eval($orders.size() >= 3)
then
    console.log(`Customer ${customer.name} has ${orders.length} pending orders`);
    // Bundle them for discount
end
```

#### Forall - Universal Quantification
```drl
rule "All Orders Completed"
when
    $customer: Customer()
    forall(
        $order: Order(customerId == $customer.id)
        Order(this == $order, status == "Completed")
    )
then
    console.log(`All orders for ${customer.name} are completed`);
end
```

### 2.7 Queries

Parameterized queries for data retrieval:

```drl
query "customers_by_status"(String requiredStatus)
    $customer: Customer(status == requiredStatus)
end

query "orders_in_range"(double minAmount, double maxAmount) 
    $order: Order(amount >= minAmount, amount <= maxAmount)
end

query "customer_orders"(int customerId)
    $customer: Customer(id == customerId)
    $order: Order(customerId == customerId)
end
```

**Usage in C++:**
```cpp
// Simple query
auto activeCustomers = session->execute_query("customers_by_status", {"Active"});

// Range query  
auto midRangeOrders = session->execute_query("orders_in_range", {100.0, 500.0});

// Process results
for (auto& row : activeCustomers) {
    if (auto customer = row.get("$customer")) {
        std::cout << "Found: " << customer->fields.at("name") << std::endl;
    }
}
```

---

## 3. JavaScript Integration

The RHS (then-block) of rules uses **QuickJS** - a fast, lightweight JavaScript engine.

### 3.1 Available APIs

#### drools Object
```javascript
// Fact manipulation
drools.insert({type: "NewFact", field: "value"});
drools.retract(existingFact);
drools.modify(existingFact, {field: "newValue"});

// Logical assertions (auto-retracted when rule no longer matches)
drools.insertLogical({type: "DerivedFact", source: customer.id});

// Working memory queries
let facts = drools.getFactsOfType("Customer");
let fact = drools.getFactById(123);
```

#### Console Logging
```javascript
console.log("Info message");
console.warn("Warning message");  
console.error("Error message");
```

#### Standard JavaScript
```javascript
// Math operations
let score = Math.max(0, Math.min(100, balance / 1000));

// Date operations
let now = Date.now();
let expire = now + (30 * 24 * 60 * 60 * 1000); // 30 days

// String operations
let message = `Customer ${customer.name} has balance $${customer.balance}`;

// JSON operations
let config = JSON.parse(customer.preferences);
```

### 3.2 Variable Binding

DRL variables become JavaScript objects:

```drl
rule "Example"
when
    $customer: Customer($name: name, $balance: balance)
    $order: Order(customerId == $customer.id, $amount: amount)
then
    // DRL $customer becomes JS customer
    console.log(`Customer: ${customer.name}`);
    
    // Field bindings become JS variables
    console.log(`Name: ${name}, Balance: ${balance}`);
    console.log(`Order amount: ${amount}`);
    
    // Access nested fields
    if (customer.profile && customer.profile.email) {
        console.log(`Email: ${customer.profile.email}`);
    }
end
```

### 3.3 Complex JavaScript Actions

```drl
rule "Complex Business Logic"
when
    $customer: Customer()
    $orders: List() from collect(Order(customerId == $customer.id))
then
    // Calculate metrics
    let totalAmount = 0;
    let orderCount = orders.length;
    
    for (let order of orders) {
        totalAmount += order.amount;
        
        // Check for patterns
        if (order.category === "Premium" && order.amount > 1000) {
            drools.insert({
                type: "PremiumPurchase",
                customerId: customer.id,
                orderId: order.id,
                amount: order.amount
            });
        }
    }
    
    // Determine customer tier
    let tier = "Bronze";
    if (totalAmount > 10000) tier = "Gold";
    else if (totalAmount > 5000) tier = "Silver";
    
    // Update customer
    drools.modify(customer, {
        totalSpent: totalAmount,
        orderCount: orderCount,
        tier: tier,
        lastUpdated: Date.now()
    });
    
    // Send notifications
    if (tier !== customer.tier) {
        drools.insert({
            type: "TierChangeNotification",
            customerId: customer.id,
            oldTier: customer.tier,
            newTier: tier
        });
    }
end
```

---

## 4. Advanced Patterns

### 4.1 State Machine Pattern

Model complex workflows:

```drl
declare ProcessState
    processId: String
    currentState: String
    data: Object
end

rule "Start Process"
when
    $request: ProcessRequest(status == "NEW")
    not ProcessState(processId == $request.id)
then
    drools.insert({
        type: "ProcessState",
        processId: request.id,
        currentState: "VALIDATION",
        data: {startTime: Date.now()}
    });
    
    drools.modify(request, {status: "PROCESSING"});
end

rule "Validation Complete"
when
    $state: ProcessState(currentState == "VALIDATION")
    $validation: ValidationResult(processId == $state.processId, valid == true)
then
    drools.modify(state, {
        currentState: "APPROVAL",
        data: {...state.data, validatedAt: Date.now()}
    });
end

rule "Process Approved"
when  
    $state: ProcessState(currentState == "APPROVAL")
    $approval: ApprovalResult(processId == $state.processId, approved == true)
then
    drools.modify(state, {
        currentState: "COMPLETE",
        data: {...state.data, completedAt: Date.now()}
    });
end
```

### 4.2 Complex Event Processing (CEP)

Track patterns across time:

```drl
declare LoginEvent
    userId: int
    timestamp: long
    ipAddress: String
end

declare SuspiciousActivity
    userId: int
    reason: String
end

rule "Multiple Failed Logins"
when
    $user: User()
    $failedLogins: Number() from accumulate(
        LoginEvent(
            userId == $user.id, 
            success == false,
            timestamp > (Date.now() - 300000) // Last 5 minutes
        ),
        count()
    )
    eval($failedLogins >= 5)
    not SuspiciousActivity(userId == $user.id)
then
    drools.insert({
        type: "SuspiciousActivity",
        userId: user.id,
        reason: `${failedLogins} failed logins in 5 minutes`
    });
    
    console.warn(`SECURITY ALERT: User ${user.id} has ${failedLogins} failed logins`);
end

rule "Geographic Anomaly"
when
    $user: User()
    $login1: LoginEvent(userId == $user.id, $ip1: ipAddress)
    $login2: LoginEvent(
        userId == $user.id, 
        ipAddress != $ip1,
        timestamp > $login1.timestamp,
        timestamp < ($login1.timestamp + 3600000) // Within 1 hour
    )
then
    // Check if IPs are from different countries
    let country1 = geoLookup(login1.ipAddress);
    let country2 = geoLookup(login2.ipAddress);
    
    if (country1 !== country2) {
        drools.insert({
            type: "SuspiciousActivity",
            userId: user.id,
            reason: `Logins from ${country1} and ${country2} within 1 hour`
        });
    }
end
```

### 4.3 Data Validation Framework

```drl
declare ValidationError
    entityType: String
    entityId: int
    field: String
    message: String
    severity: String
end

rule "Validate Customer Email"
when
    $customer: Customer($email: email)
    eval($email === null || $email === "" || !$email.includes("@"))
then
    drools.insert({
        type: "ValidationError",
        entityType: "Customer",
        entityId: customer.id,
        field: "email",
        message: "Email address is required and must be valid",
        severity: "ERROR"
    });
end

rule "Validate Customer Age"
when
    $customer: Customer(age < 18)
then
    drools.insert({
        type: "ValidationError", 
        entityType: "Customer",
        entityId: customer.id,
        field: "age",
        message: "Customer must be 18 or older",
        severity: "ERROR"
    });
end

rule "Validate Balance Consistency"
when
    $customer: Customer($customerId: id, $balance: balance)
    $totalOrders: Number() from accumulate(
        Order(customerId == $customerId, status == "Completed", $amount: amount),
        sum($amount)
    )
    eval(Math.abs($balance - $totalOrders) > 0.01) // Account for rounding
then
    drools.insert({
        type: "ValidationError",
        entityType: "Customer", 
        entityId: customer.id,
        field: "balance",
        message: `Balance mismatch: ${balance} vs calculated ${totalOrders}`,
        severity: "WARNING"
    });
end
```

---

## 5. Performance Guidelines

### 5.1 Rule Design Best Practices

#### ✅ Put Selective Constraints First
```drl
// Good - most selective constraint first  
when
    $customer: Customer(tier == "VIP", status == "Active")

// Bad - less selective constraint first
when
    $customer: Customer(status == "Active", tier == "VIP")
```

#### ✅ Use Appropriate Salience
```drl
rule "Data Validation"
salience 1000  // Run first
when
    $data: InputData()
then
    // Validate data
end

rule "Business Logic"  
salience 100   // Run after validation
when
    $data: InputData(valid == true)
then
    // Process data
end
```

#### ✅ Minimize eval() Usage
```drl
// Good - native constraints
when
    $customer: Customer(balance > 1000, age >= 21)

// Avoid - eval is slower
when
    $customer: Customer()
    eval($customer.balance > 1000 && $customer.age >= 21)
```

### 5.2 Memory Optimization

#### Use Typed Builders for Better Performance
```cpp
// Optimized approach - uses object pools and string interning
auto customer = FAST_CUSTOMER()
    .id(1001)
    .name("John Doe")
    .balance(5000.0)
    .build();

// Standard approach  
auto customer = std::make_shared<Fact>();
customer->type = "Customer";
customer->fields["id"] = static_cast<int64_t>(1001);
customer->fields["name"] = "John Doe";
customer->fields["balance"] = 5000.0;
```

#### Batch Operations for High Throughput
```cpp
// Good - batch processing
std::vector<std::shared_ptr<Fact>> customers;
for (int i = 0; i < 1000; ++i) {
    customers.push_back(create_customer(i));
}
session->add_facts(customers); // Single batch operation

// Avoid - individual operations
for (int i = 0; i < 1000; ++i) {
    session->add_fact(create_customer(i)); // 1000 separate operations
}
```

### 5.3 Monitoring Rule Performance

```cpp
// Enable rule tracing  
session->enable_tracing(true);

// Execute rules
session->fire_all_rules();

// Get performance report
auto trace = session->get_execution_trace();
auto summary = session->get_rule_performance_summary();

for (auto& [rule_name, stats] : summary) {
    std::cout << rule_name << ": " 
              << stats.execution_count << " executions, "
              << stats.total_time_ms << "ms total\n";
}
```

### 5.4 Avoid Storing Large or Complex Data in Session

The `StatefulSession` is the engine\'s working memory, designed for efficient pattern matching by the Rete algorithm, not as a general-purpose data store. Inserting large volumes of data or complex, passive objects that are not actively involved in rule matching can severely degrade performance and lead to excessive memory consumption.

**Why it\'s a bad idea:**

*   **Memory Bloat:** Every fact inserted consumes memory. Large facts or a high number of facts can quickly exhaust available memory.
*   **Performance Degradation:** The Rete network is incremental. Every insertion, modification, or retraction of a fact can trigger significant re-evaluation across the network. More data means exponentially higher processing overhead, leading to slow `fire_all_rules()` calls.
*   **Unnecessary Complexity:** Treating the session as a database introduces complexity in managing data lifecycle and filtering irrelevant data within rules.

**Best Practice:**

*   **Only insert "active" facts:** The `StatefulSession` should only contain facts that are directly involved in the pattern matching of your rules\' `when` conditions.
*   **Store large/complex data externally:** For data that is large, complex, or not directly participating in rule conditions, store it in external systems (e.g., databases, caches).
*   **Load relevant subsets on demand:** When rules need access to this external data, retrieve only the necessary, minimal subset into the session (e.g., via rule actions, global variables, or external service calls) just before it\'s needed for processing.

**In essence:** The rules engine processes logic, it does not store your application\'s entire dataset. Respect the engine\'s design to ensure optimal performance and maintainability.

--- 

## 6. Troubleshooting

### 6.1 Common Issues

#### Rule Not Firing
```drl
rule "Debug Rule"
when
    $customer: Customer(balance > 1000)
    $order: Order(customerId == $customer.id)
then
    console.log("Rule fired for customer: " + customer.name);
end
```

**Debugging steps:**
1. Check that both facts exist: `session->get_facts_of_type("Customer")`
2. Verify constraints match: `customer.balance > 1000`
3. Check foreign key relationship: `order.customerId == customer.id`
4. Use `session->enable_tracing(true)` for detailed execution log

#### JavaScript Runtime Errors
```javascript
// Add error handling in RHS
then
    try {
        let result = complexCalculation(customer.data);
        drools.insert({type: "Result", value: result});
    } catch (error) {
        console.error("Calculation failed: " + error.message);
        drools.insert({
            type: "ProcessingError", 
            entityId: customer.id,
            error: error.message
        });
    }
```

#### Memory Issues
```cpp
// Monitor memory usage
auto stats = PoolStatsCollector::collect();
std::cout << PoolStatsCollector::format_stats(stats) << std::endl;

// Clear unused facts periodically  
session->retract_facts_of_type("TemporaryData");

// Use object pools for high-frequency operations
auto pooled_fact = GlobalPools::make_pooled_fact();
```

### 6.2 Performance Debugging

#### Identify Slow Rules
```cpp
session->enable_tracing(true);
auto start = std::chrono::high_resolution_clock::now();

session->fire_all_rules();

auto end = std::chrono::high_resolution_clock::now(); 
auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

std::cout << "Rules execution took: " << duration.count() << "ms\n";

// Analyze per-rule performance
auto summary = session->get_rule_performance_summary();
for (auto& [rule, stats] : summary) {
    if (stats.average_time_ms > 10.0) { // Flag slow rules
        std::cout << "SLOW RULE: " << rule << " - " << stats.average_time_ms << "ms avg\n";
    }
}
```

#### Optimize Network Propagation
```cpp
// Use fact type batching for better Rete network efficiency
std::vector<std::shared_ptr<Fact>> customers = load_customers();
std::vector<std::shared_ptr<Fact>> orders = load_orders();

// Add by type to minimize network propagations
session->add_facts(customers);
session->add_facts(orders);

// Better than mixed addition
```

--- 

## Next Steps

Ready for advanced topics?

-   **[Developer Guide](DEVELOPER_GUIDE.md)** - Extending the engine, custom functions
-   **[API Reference](API_REFERENCE.md)** - Complete C++ API documentation  
-   **[Deployment Guide](DEPLOYMENT.md)** - Production deployment, monitoring
-   **[Architecture Guide](ARCHITECTURE.md)** - Understanding the Rete implementation

--- 

*"Good programmers worry about data structures and their relationships. Bad programmers worry about the code."* - Linus Torvalds

The Drills engine is built around elegant data structures that make complex business logic simple to express and blazingly fast to execute.


## Next Steps

Ready for advanced topics?

- **[Developer Guide](DEVELOPER_GUIDE.md)** - Extending the engine, custom functions
- **[API Reference](API_REFERENCE.md)** - Complete C++ API documentation  
- **[Deployment Guide](DEPLOYMENT.md)** - Production deployment, monitoring
- **[Architecture Guide](ARCHITECTURE.md)** - Understanding the Rete implementation

---

*"Good programmers worry about data structures and their relationships. Bad programmers worry about the code."* - Linus Torvalds

The Drills engine is built around elegant data structures that make complex business logic simple to express and blazingly fast to execute.