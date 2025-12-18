# Drills Rules Engine - Complete User Guide

*Master the art of declarative business logic*

## Table of Contents

1. [**Core Concepts**](#1-core-concepts)
2. [**RFL Language Reference**](#2-rfl-language-reference)
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

```rfl
rule "Price Alert"
when
    $product: Product(price < 100)
    $user: User(interests contains $product.category)
then
    // Only fires when BOTH conditions are met
    // Automatically retracts alert if price goes above 100
    rfl.insert({
        type: "PriceAlert",
        userId: user.id,
        productId: product.id
    });
end
```

### Working Memory vs Knowledge Base

```cpp
// Knowledge Base = Immutable compiled rules (thread-safe)
auto kb = build_knowledge_base(rfl_source, result);

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
    *   Your rules, whether defined in RFL files, decision tables (like CSVs), or other formats, are first parsed and compiled into an optimized internal representation, primarily a Rete network.
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

## 2. RFL Language Reference

### 2.1 File Structure

Every RFL file follows this structure:

```rfl
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

```rfl
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

```rfl
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

        rfl.insert({
            type: "VipStatus",
            customerId: customer.id,
            level: "Gold",
            expires: Date.now() + 365 * 24 * 60 * 60 * 1000
        });
end
```

### 2.4 Rule Attributes

| Attribute | Description | Example |
|-----------|-------------|---------|
| `salience` | Rule priority (higher = fires first) | `salience 100` |
| `no-loop` | Prevents rule from re-firing on self-modified facts | `no-loop` |
| `agenda-group` | Groups rules for focused execution | `agenda-group "validation"` |
| `activation-group` | Only one rule in the group can fire | `activation-group "exclusive"` |
| `lock-on-active` | Prevents re-activation while agenda-group is active | `lock-on-active` |
| `enabled` | Enable/disable rule at parse time (default: true) | `enabled false` |
| `auto-focus` | Auto-focus agenda-group when rule activates | `auto-focus true` |
| `duration` | Delay rule execution by milliseconds after activation | `duration 1000` |
| `extends` | Inherit conditions from another rule | `extends "BaseRule"` |

```rfl
rule "Priority Rule"
    salience 100              // High priority
    agenda-group "validation" // Part of validation group
    no-loop                   // Don't re-fire on own updates
    when
        $order: Order(status == "new")
    then
        rfl.update(order, {status: "validated"});
end

rule "Exclusive Handler"
    activation-group "handlers"  // Only one handler fires
    auto-focus true              // Auto-focus when activated
    when
        $event: Event(type == "click")
    then
        console.log("Handling click event");
end

rule "Delayed Processing"
    duration 5000              // Wait 5 seconds before firing
    when
        $alert: Alert(priority == "low")
    then
        console.log("Processing low priority alert after delay");
end
```

### 2.5 Constraint Operators

| Operator | Description | Example |
|----------|-------------|---------|
| `==` | Equality | `status == "Active"` |
| `!=` | Inequality | `age != 0` |
| `<`, `<=`, `>`, `>=` | Comparison | `balance > 1000` |
| `contains` | String/collection contains value | `name contains "John"` |
| `not contains` | Negation of contains | `tags not contains "spam"` |
| `matches` | Regex pattern match | `email matches ".*@company\\.com"` |
| `not matches` | Negation of matches | `name not matches "^Test.*"` |
| `memberOf` | Value is member of collection variable | `code memberOf $validCodes` |
| `not memberOf` | Negation of memberOf | `code not memberOf $invalidCodes` |
| `startsWith` | String starts with prefix | `name startsWith "Dr."` |
| `endsWith` | String ends with suffix | `email endsWith ".com"` |
| `lengthIs` | String length equals | `code lengthIs 5` |
| `in` | Value in list | `status in ("Active", "Pending")` |
| `not in` | Value not in list | `country not in ("US", "CA")` |

### 2.6 Pattern Matching

#### Basic Pattern
```rfl
$customer: Customer(balance > 1000)
```

#### Multiple Constraints
```rfl
$order: Order(
    amount > 100,
    status == "Completed",
    customerId == $customer.id
)
```

#### Nested Field Access
```rfl
$user: User(profile.preferences.newsletter == true)
```

#### Null-Safe Field Access (`!.`)
Safely navigate through potentially null fields. Returns `nil` if any segment is null:
```rfl
// Won't error if address is null - just won't match
$user: User(address!.city == "NYC")

// Chain multiple null-safe accesses
$order: Order(customer!.preferences!.priority == "high")
```

#### Index Access (`[]`)
Access list elements by index or map values by key:
```rfl
// Access first item in list
$order: Order(items[0].name == "Widget")

// Negative index for last item (Python-style)
$order: Order(items[-1].price > 100)

// Map access by string key
$config: Config(settings["theme"] == "dark")
```

#### Variable Binding
```rfl
$customer: Customer($customerId: id, balance > 1000)
$orders: Order(customerId == $customerId)
```

### 2.7 Advanced Patterns

#### Accumulate - Data Aggregation
```rfl
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
```rfl
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
```rfl
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

### 2.8 Queries

Parameterized queries for data retrieval:

```rfl
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

### 3.1 Critical: Variable Scoping

> ⚠️ **IMPORTANT**: All rules share the same JavaScript context. Use `var` instead of `let` for variable declarations.

```javascript
// ✅ CORRECT - use var
var subtotal = order.quantity * order.unitPrice;
var discount = subtotal * 0.1;

// ❌ WRONG - let causes "redeclaration" errors across rules
let subtotal = order.quantity * order.unitPrice;  // SyntaxError on second rule!
```

**Why?** When multiple rules fire, they execute in the same QuickJS global context. `let` doesn't allow redeclaration, causing `SyntaxError: redeclaration of 'variableName'`.

### 3.2 Available JavaScript APIs

#### rfl Object
```javascript
// Insert a new fact (type must be fully qualified from package declaration)
rfl.insert({type: "com.example.Customer", name: "John", balance: 1000});

// Update an existing fact
rfl.update(order, {finalPrice: 99.99, status: "processed"});

// Retract (delete) a fact
rfl.retract(oldFact);

// Logical insertion (auto-retracted when rule conditions no longer match)
rfl.insertLogical({type: "com.example.Alert", message: "Low stock"});

// Stop rule execution immediately
rfl.halt();

// Set agenda focus to a specific group
rfl.setFocus("cleanup");

// Get information about the current rule
var ruleName = rfl.getRule().name;
console.log("Executing rule: " + ruleName);
```

#### Modify Block Syntax
A RuleForge-style structured way to update facts:
```rfl
// Instead of rfl.update(), you can use modify block:
modify($person) {
    setAge(30),
    setStatus("updated"),
    setScore(person.score + 100)
}
// This transforms to: rfl.update(person, {age: 30, status: "updated", score: person.score + 100})
```

#### Console Logging
```javascript
console.log("Info message");
console.warn("Warning message");
console.error("Error message");
```

#### JavaScript Built-in Objects

The following JavaScript globals are available in RHS:

| Category | Available Objects/Functions |
|----------|----------------------------|
| **Math** | `Math.floor()`, `Math.ceil()`, `Math.round()`, `Math.max()`, `Math.min()`, `Math.abs()`, `Math.pow()`, `Math.sqrt()`, `Math.random()` |
| **Date** | `Date.now()`, `new Date()`, date methods |
| **JSON** | `JSON.parse()`, `JSON.stringify()` |
| **Type Conversion** | `parseInt()`, `parseFloat()`, `isNaN()`, `isFinite()` |
| **String** | `String()`, string methods, template literals |
| **Array** | `Array()`, `Array.isArray()`, array methods |
| **Object** | `Object.keys()`, `Object.values()`, `Object.assign()` |
| **Other** | `Boolean()`, `Number()`, `RegExp()`, `Error()`, `Map`, `Set` |

```javascript
// Examples
var rounded = Math.floor(order.finalPrice);
var now = Date.now();
var config = JSON.parse(customer.preferences);
var total = parseInt(order.total);
var message = `Customer ${customer.name} has ${orders.length} orders`;
```

### 3.3 Variable Binding Rules

RFL bindings (with `$` prefix) become JavaScript variables (without `$`):

```rfl
rule "Example"
when
    $customer: Customer($name: name, $balance: balance)
    $order: Order(customerId == $customer.id, $amount: amount)
then
    // RFL $customer becomes JS customer ($ stripped)
    console.log("Customer: " + customer.name);

    // Field bindings also lose the $ prefix
    console.log("Name: " + name + ", Balance: " + balance);
    console.log("Order amount: " + amount);

    // Access nested fields directly
    if (customer.profile && customer.profile.email) {
        console.log("Email: " + customer.profile.email);
    }
end
```

**Binding Reference:**
| RFL (LHS) | JavaScript (RHS) |
|-----------|------------------|
| `$customer` | `customer` |
| `$order` | `order` |
| `$name: name` | `name` |
| `$totalAmount` | `totalAmount` |

### 3.4 Type Names in rfl.insert()

When inserting facts, the `type` field must use the **fully qualified name** from the package declaration:

```rfl
package com.example.pricing

declare Order
    quantity: int
    unitPrice: double
end

rule "Create Order"
when
    // ...
then
    // ✅ CORRECT - fully qualified type name
    rfl.insert({
        type: "com.example.pricing.Order",
        quantity: 5,
        unitPrice: 19.99
    });

    // ❌ WRONG - unqualified name won't match alpha network
    rfl.insert({
        type: "Order",  // This fact won't trigger rules!
        quantity: 5,
        unitPrice: 19.99
    });
end
```

### 3.5 Preventing Infinite Loops

When a rule updates a fact that matches its own conditions, it can trigger infinitely. Use `no-loop` to prevent this:

```rfl
rule "Round Down Price"
    no-loop  // Prevents re-triggering on self-modified facts
    when
        $order: Order(finalPrice > 0)
    then
        var rounded = Math.floor(order.finalPrice);
        rfl.update(order, {finalPrice: rounded});
        // Without no-loop: would fire again because finalPrice > 0 still true!
end
```

**Alternative:** Design conditions that become false after the action:

```rfl
rule "Apply Discount Once"
    when
        $order: Order(discountApplied == false)  // Condition becomes false after update
    then
        var discounted = order.total * 0.9;
        rfl.update(order, {total: discounted, discountApplied: true});
end
```

### 3.6 Complete Example

```rfl
package com.example.pricing

declare Order
    quantity: int
    unitPrice: double
    finalPrice: double
end

// Apply 10% discount for bulk orders
rule "Bulk Discount"
    when
        $order: Order(quantity >= 10, finalPrice < 0.01)
    then
        console.log("Applying bulk discount for qty=" + order.quantity);
        var subtotal = order.quantity * order.unitPrice;
        var discounted = subtotal * 0.90;
        rfl.update(order, {finalPrice: discounted});
end

// Round down final price
rule "Round Down"
    salience -10  // Run after discount rules
    no-loop       // Prevent infinite loop
    when
        $order: Order(finalPrice > 0.01)
    then
        var rounded = Math.floor(order.finalPrice);
        rfl.update(order, {finalPrice: rounded});
end

query "ProcessedOrders"
    $order: Order(finalPrice > 0)
end
```

---

## 4. Advanced Patterns

### 4.1 State Machine Pattern

Model complex workflows:

```rfl
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
    rfl.insert({
        type: "ProcessState",
        processId: request.id,
        currentState: "VALIDATION",
        data: {startTime: Date.now()}
    });

    rfl.update(request, {status: "PROCESSING"});
end

rule "Validation Complete"
when
    $state: ProcessState(currentState == "VALIDATION")
    $validation: ValidationResult(processId == $state.processId, valid == true)
then
    rfl.update(state, {
        currentState: "APPROVAL",
        data: {...state.data, validatedAt: Date.now()}
    });
end

rule "Process Approved"
when
    $state: ProcessState(currentState == "APPROVAL")
    $approval: ApprovalResult(processId == $state.processId, approved == true)
then
    rfl.update(state, {
        currentState: "COMPLETE",
        data: {...state.data, completedAt: Date.now()}
    });
end
```

### 4.2 Complex Event Processing (CEP)

Track patterns across time using temporal operators and entry points.

#### Temporal Operators
| Operator | Description | Example |
|----------|-------------|---------|
| `after` | Event occurs after another | `timestamp after $e1.timestamp` |
| `before` | Event occurs before another | `timestamp before $e1.timestamp` |
| `within` | Events within time window | `within 60s of $e1` |
| `coincides` | Events at same time | `timestamp coincides $e1.timestamp` |
| `during` | Event during another | `timestamp during $e1.timestamp` |

**Duration literals:** `300ms`, `5s`, `10m`, `1h`

#### Entry Points for Event Streams
Insert events into named entry points for stream-based processing:

```cpp
// C++ - Insert into named entry point
auto event = std::make_shared<Fact>();
event->type = "SensorReading";
event->fields["value"] = 42.0;
event->fields["timestamp"] = getCurrentTimestamp();

session->insert_into("sensor-stream", event);
```

```rfl
rule "Process Sensor Events"
when
    // Only matches facts from the "sensor-stream" entry point
    $reading: SensorReading(value > threshold) from entry-point "sensor-stream"
then
    console.log("Sensor alert: " + reading.value);
end
```

#### Temporal Pattern Example

```rfl
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
    rfl.insert({
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
    var country1 = geoLookup(login1.ipAddress);
    var country2 = geoLookup(login2.ipAddress);

    if (country1 !== country2) {
        rfl.insert({
            type: "SuspiciousActivity",
            userId: user.id,
            reason: `Logins from ${country1} and ${country2} within 1 hour`
        });
    }
end
```

### 4.3 Data Validation Framework

```rfl
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
    rfl.insert({
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
    rfl.insert({
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
    rfl.insert({
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
```rfl
// Good - most selective constraint first
when
    $customer: Customer(tier == "VIP", status == "Active")

// Bad - less selective constraint first
when
    $customer: Customer(status == "Active", tier == "VIP")
```

#### ✅ Use Appropriate Salience
```rfl
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
```rfl
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

### 5.5 Thread Safety

Understanding thread safety is critical for production deployments.

#### KnowledgeBase: Thread-Safe (Immutable)

The `KnowledgeBase` is **fully thread-safe** because it is immutable after construction:

```cpp
// Build once, share everywhere
auto kb = build_knowledge_base(rfl_source, result);

// Safe: multiple threads can create sessions from the same KB
std::thread t1([&kb]() {
    auto session = kb->create_session();
    // Use session...
});

std::thread t2([&kb]() {
    auto session = kb->create_session();
    // Use session...
});
```

The knowledge base contains:
- Compiled RETE network structure (read-only)
- Parsed rule definitions (read-only)
- Type declarations (read-only)

**Key principle:** Build the `KnowledgeBase` once during application startup, then share it across all threads.

#### StatefulSession: NOT Thread-Safe

Each `StatefulSession` is **NOT thread-safe** and must be used from a single thread:

```cpp
// CORRECT: Each thread owns its session
void process_request(KnowledgeBase const& kb, Request const& req) {
    auto session = kb.create_session();  // Thread-local session
    session->add_fact(create_fact(req));
    session->fire_all_rules();
    // Results...
}

// WRONG: Never share sessions between threads!
auto shared_session = kb->create_session();

std::thread t1([&]() {
    shared_session->add_fact(fact1);  // DATA RACE!
});

std::thread t2([&]() {
    shared_session->fire_all_rules();  // DATA RACE!
});
```

The session contains mutable state:
- Working memory (facts)
- Agenda (pending activations)
- Token/WME caches
- Transaction state
- Metrics counters

#### Recommended Pattern: Session-per-Request

For web servers and concurrent workloads:

```cpp
class RuleEngineService {
    std::shared_ptr<KnowledgeBase const> kb_;

public:
    void initialize(std::string const& rules) {
        ParseResult result;
        kb_ = build_knowledge_base(rules, result);
        if (!result.success) {
            throw std::runtime_error("Rule compilation failed");
        }
    }

    // Thread-safe: each request gets its own session
    ProcessResult process(Request const& request) {
        auto session = kb_->create_session();

        // Insert request data
        session->add_fact(create_fact(request));

        // Fire rules
        session->fire_all_rules();

        // Query results
        auto results = session->execute_query("results");

        // Session destroyed at end of scope
        return build_response(results);
    }
};
```

#### Metrics Collection Across Threads

When collecting metrics from multiple sessions:

```cpp
#include "metrics_exporter.hpp"

// Thread-safe metrics aggregation
std::atomic<int64_t> global_rules_fired{0};
std::atomic<int64_t> global_facts_processed{0};

void process_with_metrics(KnowledgeBase const& kb, Request const& req) {
    auto session = kb.create_session();
    session->add_fact(create_fact(req));
    session->fire_all_rules();

    // Collect session metrics (thread-safe)
    auto metrics = session->get_metrics();
    global_rules_fired += metrics.rules_fired_total;
    global_facts_processed += metrics.facts_inserted_total;
}

// Export metrics (e.g., for Prometheus endpoint)
std::string get_prometheus_metrics() {
    PrometheusExporter exporter("myapp");
    SessionMetrics aggregated;
    aggregated.rules_fired_total = global_rules_fired.load();
    aggregated.facts_inserted_total = global_facts_processed.load();
    return exporter.export_metrics(aggregated);
}
```

#### Summary Table

| Component | Thread-Safe | Reason |
|-----------|-------------|--------|
| `KnowledgeBase` | ✅ Yes | Immutable after construction |
| `StatefulSession` | ❌ No | Contains mutable working memory |
| `create_session()` | ✅ Yes | Creates independent session |
| `get_metrics()` | ✅ Yes | Returns copy of metrics |
| `fire_all_rules()` | ❌ No | Modifies session state |
| `add_fact()` | ❌ No | Modifies working memory |
| `execute_query()` | ❌ No | Reads session state |

---

## 6. Troubleshooting

### 6.1 Common Issues

#### Rule Not Firing
```rfl
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
        var result = complexCalculation(customer.data);
        rfl.insert({type: "Result", value: result});
    } catch (error) {
        console.error("Calculation failed: " + error.message);
        rfl.insert({
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

- **[RFL Language Guide](dsl.md)** - Grammar specification and reference
- **[Deployment Guide](DEPLOYMENT.md)** - Production deployment, monitoring
- **[Quick Start](../README.md)** - Basic examples and C++ API usage

---

*"Good programmers worry about data structures and their relationships. Bad programmers worry about the code."* - Linus Torvalds

The Drills engine is built around elegant data structures that make complex business logic simple to express and blazingly fast to execute.
