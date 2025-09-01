 
# Drools-inspired Rule Language (DRL) Guide

This guide provides a comprehensive overview of the Drools-like Rule Language (DRL) used by the C++ Rete rule engine. It is designed for developers, rule authors, and advanced users, covering basic syntax, C++ integration, and advanced design patterns.

## Table of Contents
1.  [**Core Concepts & Syntax**](#1-core-concepts--syntax)
    *   [File Structure](#11-file-structure)
    *   [Type Declaration (`declare`)](#12-type-declaration-declare)
    *   [Packages & Imports](#13-packages--imports)
    *   [Globals (`global`)](#14-globals-global)
2.  [**Writing Rules (`rule`)**](#2-writing-rules-rule)
    *   [Basic Structure](#21-basic-structure)
    *   [Rule Attributes](#22-rule-attributes)
    *   [LHS: The `when` Block (Conditions)](#23-lhs-the-when-block-conditions)
    *   [RHS: The `then` Block (Actions)](#24-rhs-the-then-block-actions)
3.  [**Queries (`query`)**](#3-queries-query)
4.  [**Advanced Patterns & C++ Integration**](#4-advanced-patterns--c-integration)
    *   [Advanced Data Manipulation with `from`](#41-advanced-data-manipulation-with-from)
    *   [Logical Insertions and TMS](#42-logical-insertions-and-truth-maintenance-system-tms)
    *   [C++ Integration Details](#43-c-integration-details)
5.  [**DSL Design Patterns**](#5-dsl-design-patterns)
    *   [State Machine / Phased Execution](#51-pattern-1-state-machine--phased-execution-dsl)
    *   [Missing Information Validation](#52-pattern-2-missing-information-validation-dsl)
    *   [Complex Event Processing (CEP)](#53-pattern-3-complex-event-processing-cep-dsl)

---

## 1. Core Concepts & Syntax

A DRL file is a collection of statements that define the data, logic, and queries for the rule engine.

### 1.1 File Structure

A DRL file is a sequence of top-level statements. While the order is not strictly enforced, the conventional structure is:
```drl
package com.example.rules

import com.example.model.Customer

global java.util.List results

// Type declarations
declare Offer
    message: String
end

// Queries
query "Find Customers by Status"
    // ...
end

// Rules
rule "Example Rule"
    // ...
end
```

### 1.2 Type Declaration (`declare`)

The `declare` statement defines the schema for a new fact type. This provides structure and enables compile-time validation.

**Syntax:**
`declare <TypeName> [ <field_name> : <field_type> ]... end`

**Example:**
```drl
declare Customer
    id: int
    name: String
    verified: boolean
    monthlySpend: double
end
```
Supported primitive types are `String`, `int` (64-bit), `double`, and `boolean`.

### 1.3 Packages & Imports

The `package` and `import` statements work together to resolve short type names (e.g., `Customer`) to their fully-qualified names (e.g., `com.example.model.Customer`).

- `package com.example.rules`: Sets the default namespace for all declarations in the current file.
- `import com.example.model.Customer`: Imports a specific type, making `Customer` available as a short name.
- `import com.example.model.*`: A wildcard import makes all types within the `com.example.model` package available via their short names.

### 1.4 Globals (`global`)

The `global` statement declares a variable that can be set by the host C++ application and accessed from within a rule's `then` block. This is ideal for providing services like logging or results collection.

**Syntax:**
`global <type> <variable_name>;`
(Note: The `<type>` is for documentation; the variable is dynamically typed in Lua).

**Example:**
```drl
global com.my_app.LoggerService logger;

rule "Log Errors"
when
    $e: AppError()
then
    -- Globals are accessed directly by name in Lua
    logger.error($e.message)
end
```

---

## 2. Writing Rules (`rule`)

Rules are the heart of the engine, containing conditional logic (`when`) and actions (`then`).

### 2.1 Basic Structure
```drl
rule "<Rule Name>"
    <attributes...>
when
    <conditions...>
then
    <actions...>
end
```

### 2.2 Rule Attributes
Attributes modify a rule's behavior and are placed between the rule name and the `when` block.

- `salience <integer>`: Sets the rule's priority. Higher numbers fire first. (Default: 0).
- `agenda-group "<group_name>"`: Assigns the rule to a specific group, enabling phased execution.
- `extends "<parent_rule_name>"`: Inherits all conditions from a parent rule.

### 2.3 LHS: The `when` Block (Conditions)

The Left-Hand Side (LHS) defines the patterns that facts must match for the rule to activate.

-   **Patterns**: Match facts of a specific type.
    -   `$c: Customer()` - Matches a `Customer` fact and binds it to the variable `$c`.
    -   `Person()` - Matches any `Person` fact without binding it.

-   **Constraints**: Filter matches within a pattern's parentheses `()`.
    -   **Comparisons**: `age > 18`, `status == "Gold"`, `name != "Test"`.
    -   **Joins**: `Order(customerId == $c.id)` - Joins `Order` to a previously bound `Customer` `$c`.
    -   **Inline Binding**: `$c: Customer($id: id)` - Binds the `id` field of the matched `Customer` to `$id`.

-   **Logical Operators**:
    -   **AND**: `,` or `&&` can be used. Constraints on separate lines or within the same pattern are implicitly ANDed.
    -   **OR**: The `or` keyword creates separate, mutually exclusive condition blocks.

-   **Conditional Elements**:
    -   `not ( <pattern> )`: True if the inner pattern finds no matches.
    -   `exists ( <pattern> )`: True if the inner pattern finds at least one match. Does not add the fact to the match result.
    -   `forall ( <p1>, <p2> ... )`: True if for every fact matching `<p1>`, the subsequent patterns also match.
    -   `eval( <lua_expression> )`: Executes a boolean Lua expression. Example: `$p: Person() eval(p.age > 10)`.

-   **Temporal Operators**: For use with event facts that have a `timestamp` field.
    -   `timestamp after $other.timestamp`: Compares timestamps between two events.
    -   `within 10s of $other`: Checks if the event's timestamp is within a duration of another event's timestamp. Units: `ms`, `s`, `m`, `h`.

### 2.4 RHS: The `then` Block (Actions)

The Right-Hand Side (RHS) is a block of **Lua code** that executes when the rule's conditions are met.

-   **Accessing Bound Variables**: Variables bound in the `when` block (e.g., `$c`) are available in Lua without the `$` prefix (e.g., `c`). Access fields with dot notation: `c.name`.

-   **The `drools` API**: A special `drools` object is injected into the Lua environment to interact with the engine:
    -   `drools.insert({type="...", ...})`: Inserts a new fact.
    -   `drools.insertLogical({type="...", ...})`: Inserts a fact that is automatically retracted if the rule's conditions become false.
    -   `drools.retract(fact_variable)`: Retracts the fact bound to `fact_variable`.
    -   `drools.update(fact_variable, {field="new_value", ...})`: Modifies a fact's fields.
    -   `drools.setFocus("<agenda_group>")`: Pushes an agenda group onto the focus stack.
-   **The `modify` Block**: This is syntactic sugar for `drools.update`.
    ```drl
    // This DRL...
    modify($c) { setStatus("Platinum"); }

    // ...is translated to this Lua code:
    drools.update(c, { status = "Platinum" })
    ```

---

## 3. Queries (`query`)

Queries are named, read-only searches for combinations of facts. They can be parameterized and are called from the host C++ application.

**Syntax:**
`query "<Query Name>" [ ( <parameters> ) ] <conditions...> end`

-   **Parameters**: A list of `TypeName $variable` pairs that serve as inputs to the query.

**Example:**
```drl
// Query for orders belonging to a specific customer
query findCustomerOrders(Customer $c)
    $o: Order(customerId == $c.id)
end
```

---

## 4. Advanced Patterns & C++ Integration

### 4.1 Advanced Data Manipulation with `from`
The `from` clause allows a pattern to source its data from a special operation rather than direct fact matching.

-   **`from accumulate`**: Performs an aggregate calculation over a set of facts.
    -   **Functions**: `sum`, `count`, `average`, `min`, `max`, `collectList`, `collectSet`.
    -   **Example**: Calculate total sales. The result is a new fact with a `result` field.
        ```drl
        declare TotalSales result:double end

        rule "Calculate Total Sales"
        when
            $total: TotalSales() from accumulate(
                $p: Purchase(), sum($p.amount)
            )
        then
            print("Total sales: " .. $total.result)
        end
        ```

-   **`from unnest`**: Iterates over a collection inside a fact, creating a match for each item.
    -   **Example**: Process each line item within an order.
        ```drl
        rule "Ship Each Item"
        when
            $order: Order()
            $item: LineItem() from unnest($order.items)
        then
            // This rule fires once for each item in the order's item list
            print("Shipping " .. $item.product .. " for order " .. $order.orderId)
        end
        ```

### 4.2 Logical Insertions and Truth Maintenance System (TMS)
The TMS manages facts inserted via `insertLogical`. These facts are automatically retracted when the conditions that justified their existence are no longer true.

**Use Case:** An alarm should only be active as long as a fire is detected.
```drl
rule "Sound Alarm on Fire"
when
    $f: Fire(active == true)
then
    // The Alarm fact is justified by the existence of an active Fire.
    // If the Fire fact is retracted or its status changes, this Alarm is auto-retracted.
    drools.insertLogical({type="Alarm", on=true})
end
```

### 4.3 C++ Integration Details

-   **Calling Queries**: Use `session->execute_query("query_name", {args...})` to get a `QueryResult` object.
    ```cpp
    // Argument for the query
    auto arg_fact = std::make_shared<Fact>();
    arg_fact->type = "Customer";
    arg_fact->fields["id"] = (int64_t)123;

    // Execute and process results
    QueryResult results = session->execute_query("findCustomerOrders", {arg_fact});
    for (const auto& row : results) {
        // Use the safe, typed accessor
        auto product_name = row.getFieldAs<std::string>("$o", "product");
        if (product_name) {
            std::cout << "Found product: " << *product_name << std::endl;
        }
    }
    ```

-   **Using Globals**: Expose C++ functionality to your rules.
    ```cpp
    // In C++
    sol::state& lua = session->get_lua_state();
    sol::table logger_api = lua.create_table();
    logger_api["error"] = [](const std::string& msg){ std::cerr << "LUA_ERROR: " << msg << std::endl; };
    session->set_global("logger", logger_api);

    // In DRL
    global MyLogger logger;
    rule "Log Errors" when $e:AppError() then logger.error($e.message) end
    ```

---

## 5. DSL Design Patterns

Combine core features to create powerful, readable, and maintainable Domain-Specific Languages (DSLs) using "control facts"—facts inserted not for their data, but to drive rule logic.

### 5.1 Pattern 1: State Machine / Phased Execution DSL
**Problem**: Enforce a strict order of operations (e.g., `Validate -> Enrich -> Process`).

**DSL Approach**: Use a `Phase` control fact to model the current state and `agenda-group` to control execution flow.
```drl
// Control Fact
declare Phase name:String, processId:String end

rule "Start Validation"
    salience 10 // Higher priority to start the process
when
    $app: Application(status == "new")
    not( Phase(processId == $app.id) )
then
    drools.insert({type="Phase", name="validation", processId=$app.id});
    drools.setFocus("validation"); // Switch to the validation phase
end

rule "Perform Validation"
    agenda-group "validation"
when
    // This rule only runs when the "validation" agenda group is active
    $phase: Phase(name == "validation")
    $app: Application(id == $phase.processId)
then
    // ... do validation ...
    // Transition to the next phase
    drools.update($phase, {name="enrichment"});
    drools.setFocus("enrichment");
end
```
**Benefits**: Makes the process flow explicit, readable, and easy to modify.

### 5.2 Pattern 2: Missing Information Validation DSL
**Problem**: Find situations where something *should* have happened but didn't (e.g., a ticket was not acknowledged).

**DSL Approach**: Use `insertLogical` to create a "policy" fact, and a second rule with `not` to find violations.
```drl
// When a high-priority ticket exists...
rule "High-Priority Tickets Require Acknowledgment"
when
    $ticket: TroubleTicket(priority == "high", status != "closed")
then
    // ...it logically requires an acknowledgment.
    drools.insertLogical({type="AcknowledgmentRequired", ticketId=$ticket.id});
end

// Find violations
rule "Flag Unacknowledged Tickets"
when
    $req: AcknowledgmentRequired()
    // Find where NO acknowledgment event has occurred for the required ticket
    not ( TicketAcknowledged(ticketId == $req.ticketId) )
then
    print("Ticket " .. $req.ticketId .. " requires escalation!")
end
```
**Benefits**: Decouples the policy from the violation-handling logic. The TMS handles cleanup automatically.

### 5.3 Pattern 3: Complex Event Processing (CEP) DSL
**Problem**: Detect patterns between events over time (e.g., multiple failed logins).

**DSL Approach**: Use temporal constraints on event facts.
```drl
declare LoginAttempt
    @role(event) // Mark as event for temporal reasoning
    timestamp: long
    username: String
    ipAddress: String
    status: String
end

rule "Detect Repeated Failed Logins"
when
    // Find three failed login attempts from the same IP within 10 seconds
    $e1: LoginAttempt(status == "fail", $ip: ipAddress)
    $e2: LoginAttempt(
        status == "fail", ipAddress == $ip,
        timestamp after $e1.timestamp,
        within 10s of $e1
    )
    $e3: LoginAttempt(
        status == "fail", ipAddress == $ip,
        timestamp after $e2.timestamp,
        within 10s of $e1
    )
then
    drools.insert({type="PotentialFraud", ipAddress=$ip});
end
```
**Benefits**: Expressive and natural syntax for describing complex temporal sequences.
```

 