# Drools-Inspired Rule Language (DRL) Grammar

## Introduction

This document specifies the grammar for a custom, Drools-inspired rule language designed for a high-performance Rete-based rule engine. The language allows users to define data structures, business rules, and queries in a declarative, SQL-like syntax.

The primary goal is to provide a powerful yet readable way to express complex conditional logic and data aggregations. This implementation is a functional subset of standard Java Drools, focusing on the most common and powerful features.

The language is composed of several top-level statements:

- `package` and `import`: For namespacing and type resolution.
- `global`: To define global variables accessible within rule consequences.
- `declare`: To define the schema for new fact types.
- `query`: To define reusable, parameterized lookups into the engine's working memory.
- `rule`: The core construct for defining conditional logic.

The consequence of a rule (the `then` block) is written in **JavaScript** (via QuickJS), providing a flexible and powerful scripting environment. A custom `drools` API is injected into the JavaScript context to allow the rule to interact with the engine (e.g., by inserting or retracting facts).

## Language Constructs

### File Structure

A DRL file is a sequence of zero or more top-level statements. The typical order is `package`, `import`s, `global`s, `declare`s, `query`s, and finally `rule`s.

```drl
package com.example.rules

import com.example.model.Customer

global java.util.List results

declare Offer
    message: String
end

rule "Example"
when
    // ... conditions
then
    // ... actions
end
```

### Type Declaration (`declare`)

The `declare` statement defines a new fact type and its fields.

**Syntax:** `declare <TypeName> [ <field_name> : <field_type> ]... end`

| Type      | Description                               |
|-----------|-------------------------------------------|
| `String`  | A string of text.                        |
| `int`     | A 64-bit signed integer.                 |
| `double`  | A 64-bit floating-point number.          |
| `boolean` | A true/false value.                      |
| Other     | Can be a previously declared custom type.|

**Example:**

```drl
declare Customer
    id: int
    name: String
    verified: boolean
end
```

### Rule Definition (`rule`)

The `rule` is the core of the language. It consists of a name, optional attributes, a `when` block (conditions), and a `then` block (actions).

**Syntax:** `rule "<RuleName>" [attributes...] when [conditions...] then [actions...] end`

#### Attributes

Attributes control the rule's execution behavior.

| Attribute | Description | Example |
|-----------|-------------|---------|
| `salience <int>` | Sets the rule's priority. Higher salience rules fire first. Default is 0. | `salience 100` |
| `agenda-group "<name>"` | Assigns the rule to a specific agenda group. The rule will only fire when its group has focus. | `agenda-group "validation"` |
| `activation-group "<name>"` | Only one rule in an activation-group can fire. When one fires, all other activations in the group are cancelled. | `activation-group "exclusive"` |
| `no-loop` | Prevents the rule from re-activating itself after modifying facts that triggered it. | `no-loop` |
| `lock-on-active` | Prevents re-activation while the rule's agenda-group is active. Stronger than `no-loop`. | `lock-on-active` |
| `enabled <bool>` | Enables or disables the rule at parse time. Default is `true`. | `enabled false` |
| `auto-focus <bool>` | When `true`, automatically sets focus to the rule's agenda-group when the rule is activated. | `auto-focus true` |
| `duration <ms>` | Delays the rule's consequence execution by the specified milliseconds after activation. | `duration 1000` |
| `extends "<ParentRule>"` | The rule inherits all conditions from the parent rule. | `extends "BaseRule"` |

**Example:**

```drl
rule "High Priority Exclusive Rule"
    salience 100
    activation-group "exclusive-group"
    no-loop
    auto-focus true
when
    $c: Customer(status == "VIP")
then
    // actions
end
```

#### LHS (The `when` Block)

The Left-Hand Side (LHS) contains a set of patterns that must be satisfied for the rule to activate.

##### Basic Patterns

- **Patterns:** A pattern matches a fact of a specific type. It can optionally bind the matched fact to a variable.
  - `$c: Customer()` - Matches a `Customer` fact and binds it to the variable `$c`.
  - `Person()` - Matches any `Person` fact without binding it.

- **Constraints:** Constraints are placed inside parentheses `()` after the fact type to filter matches.
  - `age > 18`
  - `status == "Gold"`
  - `orderId == $c.id` (Joining to another bound fact)

- **Inline Bindings:** A constraint can also bind a field's value to a variable for use later.
  - `$c: Customer( $id: id, age > 18 )` - Binds the `id` field of the matched customer to `$id`.

##### Field Access

- **Nested Fields:** Access nested properties using dot notation.
  - `$p: Person(address.city == "NYC")`

- **Null-Safe Dereference (`!.`):** Safely navigate through potentially null fields. Returns `nil` if any segment is null.
  - `$p: Person(address!.city == "NYC")` - Won't error if `address` is null.

- **Index Access (`[]`):** Access list elements by index or map values by key.
  - `$o: Order(items[0].name == "Widget")` - First item in list
  - `$o: Order(items[-1].price > 100)` - Last item (negative index)
  - `$o: Order(metadata["priority"] == "high")` - Map access by string key

##### Comparison Operators

| Operator | Description | Example |
|----------|-------------|---------|
| `==`, `!=` | Equality / Inequality | `status == "active"` |
| `<`, `>`, `<=`, `>=` | Numeric/String comparison | `age >= 18` |
| `in`, `not in` | Value in list | `status in ("Gold", "Platinum")` |
| `contains` | String contains substring, or collection contains value | `name contains "John"` |
| `not contains` | Negation of contains | `tags not contains "spam"` |
| `matches` | Regex pattern matching | `email matches ".*@company\\.com"` |
| `not matches` | Negation of matches | `name not matches "^Test.*"` |
| `memberOf` | Value is member of collection variable | `code memberOf $validCodes` |
| `not memberOf` | Negation of memberOf | `code not memberOf $invalidCodes` |
| `startsWith` | String starts with prefix | `name startsWith "Dr."` |
| `endsWith` | String ends with suffix | `email endsWith ".com"` |
| `lengthIs` | String length equals | `code lengthIs 5` |

##### Arithmetic Expressions

Arithmetic expressions can be used on the right-hand side of comparisons for dynamic calculations:

```drl
// Compare against calculated value
Transaction(timestamp > ($startTime - 60000))  // Within last minute

// Time window calculations
Event(timestamp > ($ts - 3600000), timestamp < $ts)  // Last hour
```

Supported operators: `+`, `-`, `*`, `/`

##### Temporal Operators

For Complex Event Processing (CEP) with timestamp fields:

| Operator | Description | Example |
|----------|-------------|---------|
| `after` | Event A occurs after event B | `timestamp after $e1.timestamp` |
| `before` | Event A occurs before event B | `timestamp before $e1.timestamp` |
| `within` | Events occur within a time window | `within 60s of $e1` |
| `coincides` | Events occur at the same time | `timestamp coincides $e1.timestamp` |
| `during` | Event A occurs during event B | `timestamp during $e1.timestamp` |

**Duration literals:** `300ms`, `5s`, `10m`, `1h`

##### Logical Operators

- **AND:** Constraints within the same pattern (separated by `,` or `&&`) or patterns on separate lines are implicitly ANDed together.
- **OR:** The `or` keyword separates mutually exclusive blocks of patterns. The rule will fire if *any* of the `or` blocks are satisfied.

##### Conditional Elements

- `not <pattern>` or `not ( <pattern> )`: Succeeds only if no fact matches the nested pattern.
- `exists <pattern>` or `exists ( <pattern> )`: Succeeds if at least one fact matches the nested pattern.
- `forall ( <base_pattern>, <restriction_pattern> )`: Succeeds if for all facts that match `base_pattern`, they *also* match `restriction_pattern`.
- `eval( <javascript_expression> )`: Executes a boolean JS expression in the context of the current match.

**Examples:**

```drl
// Both syntaxes are supported for not/exists:
not Order(customerId == $c.id)                    // Without parentheses
not (Order(customerId == $c.id))                  // With parentheses

exists PremiumMembership(customerId == $c.id)     // Without parentheses
exists (PremiumMembership(customerId == $c.id))   // With parentheses
```

##### `from` Clause

Modifies the data source for a pattern.

- **`from accumulate`**: Aggregates data from facts matching the source pattern.
  - **Functions:** `sum`, `count`, `average`, `min`, `max`, `collectList`, `collectSet`
  - **Result Type:** Use `Number` as the result type. Access the value with `.intValue` or `.doubleValue`.
  - **Shorthand:** Use `count(1)` to count all matching facts.
  - **CEP Support:** The source pattern can include `from entry-point` for streaming data.
  - **Examples:**
    ```drl
    // Basic sum
    $total: Number() from accumulate( $p: Purchase(), sum($p.value) )

    // Count with shorthand
    $count: Number(intValue > 5) from accumulate( $t: Transaction(), count(1) )

    // With entry-point for CEP
    $count: Number() from accumulate(
        Transaction(accountId == $acct) from entry-point "stream",
        count(1)
    )
    ```

- **`from unnest`**: Creates a match for each item in a fact's collection field.
  - **Example:** `$item: Item() from unnest( $order.items )`

- **`from entry-point`**: Matches facts from a named entry point stream (for CEP).
  - **Example:** `$e: Event() from entry-point "sensor-stream"`

#### RHS (The `then` Block)

The Right-Hand Side (RHS) contains the actions to be executed when the rule fires. The RHS is **JavaScript code** (via QuickJS).

##### Bound Variables

Variables bound in the `when` block (e.g., `$c`) are available in JavaScript without the `$` prefix (e.g., `c`). You can access their fields like `c.name`.

##### Local JavaScript Variables

You can declare local variables using standard JavaScript syntax (`var`, `let`, or `const`):

```javascript
var baseRate = 5.5;
let monthlyIncome = app.annualIncome / 12;
const MAX_DTI = 0.43;

if (credit.category == "Excellent") {
    baseRate = baseRate - 0.5;
}
```

These local variables are scoped to the rule's RHS and can be used for intermediate calculations.

##### `drools` API

A special `drools` object is available to interact with the engine:

| Method | Description |
|--------|-------------|
| `drools.insert({type: "...", ...})` | Inserts a new fact into working memory. |
| `drools.insertLogical({type: "...", ...})` | Inserts a fact that is logically dependent on the activating facts. Auto-retracted when conditions become false. |
| `drools.update(fact, {field: value, ...})` | Updates an existing fact's fields and propagates changes through the RETE network. |
| `drools.retract(fact)` | Retracts a fact from working memory. |
| `drools.halt()` | Immediately stops rule execution. No more rules will fire in the current `fireAllRules()` cycle. |
| `drools.setFocus("group")` | Sets the agenda focus to the specified agenda-group. |
| `drools.getRule()` | Returns an object with information about the current rule (e.g., `{name: "RuleName"}`). |

##### `modify` Block Syntax

A structured way to update facts using Drools-style setter calls:

```drl
modify($person) {
    setAge(30),
    setStatus("updated")
}
```

This is automatically transformed to:
```javascript
drools.update(person, {age: 30, status: "updated"})
```

##### Examples

```javascript
// Insert a new fact
drools.insert({type: "Offer", message: "Welcome, " + c.name});

// Logical insertion - auto-retracted when conditions no longer match
drools.insertLogical({type: "Alert", reason: "High value customer"});

// Update an existing fact
drools.update(c, {status: "Gold", points: c.points + 100});

// Retract an existing fact
drools.retract(c);

// Stop rule execution
if (criticalError) {
    drools.halt();
}

// Change agenda focus
drools.setFocus("cleanup");

// Get current rule info
let ruleName = drools.getRule().name;
```

### Queries

Queries are named, reusable sets of patterns that can be called from C++. They can be parameterized.

**Syntax:** `query "<QueryName>" [ ( <param_type> <$param_name> ) ] [patterns...] end`

**Example:**

```drl
// A query with one parameter
query findCustomer(NameHolder $name)
    $c: Customer(name == $name.value)
end

// A query with no parameters
query findVips
    $v: VipCustomer()
end
```

---

## Formal ABNF-like Grammar

This section provides a formal definition of the language syntax using a simplified Backus-Naur Form.

```abnf
; ---------------------------------------------
; 1. Core Primitives
; ---------------------------------------------
rulelist      = *statement
statement     = package-stmt / import-stmt / global-stmt / declaration-stmt / query-stmt / rule-stmt
OWS           = *(WSP / comment) ; Optional Whitespace & Comments
S             = 1*(WSP / comment) ; Required Whitespace & Comments
WSP           = " " / HTAB / EOL
comment       = ("//" / "#" / "--") *any-char EOL / "/*" *any-char "*/"

; ---------------------------------------------
; 2. Identifiers and Literals
; ---------------------------------------------
identifier    = ALPHA *(ALPHA / DIGIT / "_")
qualified-name = identifier *("." identifier)
binding       = "$" identifier
integer       = [ "-"] 1*DIGIT
double        = [ "-"] 1*DIGIT "." 1*DIGIT
string-literal = DQUOTE *any-char-but-quote DQUOTE
duration      = 1*DIGIT ("ms" / "s" / "m" / "h")

; ---------------------------------------------
; 3. Top-Level Statements
; ---------------------------------------------
package-stmt  = "package" S qualified-name [";"]
import-stmt   = "import" S qualified-name ["." "*"] [";"]
global-stmt   = "global" S qualified-name S identifier [";"]

declaration-stmt = "declare" S identifier OWS *(field-def OWS) "end"
field-def        = identifier OWS ":" OWS qualified-name

query-stmt = "query" S (string-literal / identifier) [query-params] OWS lhs "end"
query-params = "(" OWS [query-param * (OWS "," OWS query-param)] OWS ")"
query-param  = qualified-name S binding

rule-stmt = [annotation-list] "rule" S string-literal OWS [attributes] OWS "when" OWS lhs OWS "then" OWS rhs OWS "end"

; ---------------------------------------------
; 4. Rule Structure
; ---------------------------------------------
attributes = 1*(attribute OWS)
attribute  = salience-attr / agenda-group-attr / activation-group-attr / extends-attr
           / no-loop-attr / lock-on-active-attr / enabled-attr / auto-focus-attr / duration-attr

salience-attr         = "salience" S integer
agenda-group-attr     = "agenda-group" S string-literal
activation-group-attr = "activation-group" S string-literal
extends-attr          = "extends" S string-literal
no-loop-attr          = "no-loop"
lock-on-active-attr   = "lock-on-active"
enabled-attr          = "enabled" S ("true" / "false")
auto-focus-attr       = "auto-focus" S ("true" / "false")
duration-attr         = "duration" S integer

; --- LHS (when block) ---
lhs = pattern-group *(OWS "or" OWS pattern-group)
pattern-group = 1*(pattern OWS)

pattern = [binding OWS ":" OWS] (std-pattern / not-pattern / exists-pattern / forall-pattern / eval-pattern)

std-pattern = qualified-name [ "(" OWS [expression] OWS ")" ] [from-clause]
not-pattern = "not" OWS ( "(" OWS pattern OWS ")" / std-pattern )
exists-pattern = "exists" OWS ( "(" OWS pattern OWS ")" / std-pattern )
forall-pattern = "forall" OWS "(" OWS pattern *(OWS "," OWS pattern) OWS ")"
eval-pattern = "eval" OWS "(" *any-char ")" ; content is opaque JavaScript

; --- LHS Constraints ---
expression = or-expr
or-expr    = and-expr *(OWS "||" OWS and-expr)
and-expr   = constraint *(OWS ("," / "&&") OWS constraint)
constraint = [binding ":"] field-access [ comp-clause / in-clause / temporal-clause ]

field-access = field-part *(field-sep field-part)
field-part   = identifier *index-access
field-sep    = "." / "!."  ; regular or null-safe
index-access = "[" (integer / string-literal) "]"

comp-clause = comp-op OWS value
in-clause   = ["not" S] "in" OWS value-list
temporal-clause = temporal-op OWS value

value      = literal / field-access / binding
literal    = integer / double / string-literal / "true" / "false" / "nil"
value-list = "(" OWS value *(OWS "," OWS value) OWS ")"

comp-op    = "==" / "!=" / ">" / "<" / ">=" / "<="
           / "contains" / "not contains"
           / "matches" / "not matches"
           / "memberOf" / "not memberOf"
           / "startsWith" / "endsWith" / "lengthIs"

temporal-op = "after" / "before" / "coincides" / "during" / ("within" S duration S "of")

; --- LHS `from` clause ---
from-clause = "from" S (accumulate-clause / unnest-clause / entry-point-clause)
accumulate-clause = "accumulate" OWS "(" OWS pattern "," OWS accum-func OWS ")"
unnest-clause = "unnest" OWS "(" OWS field-access OWS ")"
entry-point-clause = "entry-point" S string-literal
accum-func = identifier "(" OWS [field-access] OWS ")"

; --- RHS (then block) ---
rhs = *(modify-stmt / code-chunk)
modify-stmt = "modify" OWS "(" OWS binding OWS ")" OWS "{" OWS *setter OWS "}"
setter = identifier "(" *any-char ")" [OWS ("," / ";")]
code-chunk = *any-char ; JavaScript code until 'end' or 'modify'
```

---

## C++ API Integration

### Inserting Facts into Entry Points

For CEP scenarios, facts can be inserted into named entry points:

```cpp
auto fact = std::make_shared<Fact>();
fact->type = "SensorEvent";
fact->fields["value"] = 42.0;
fact->fields["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();

session->insert_into("sensor-stream", fact);
```

### Custom Accumulate Functions

Register custom aggregation functions:

```cpp
class MedianAccumulator : public IAccumulator {
    std::vector<double> values_;
public:
    void accumulate(ConstraintValue const& value) override {
        if (auto* d = std::get_if<double>(&value)) {
            values_.push_back(*d);
        }
    }
    ConstraintValue get_result() const override {
        if (values_.empty()) return 0.0;
        auto sorted = values_;
        std::sort(sorted.begin(), sorted.end());
        return sorted[sorted.size() / 2];
    }
    std::unique_ptr<IAccumulator> clone() const override {
        return std::make_unique<MedianAccumulator>();
    }
};

kb->register_accumulator("median", std::make_unique<MedianAccumulator>());
```

Then use in DRL:
```drl
$m: Median() from accumulate( $s: Sample(), median($s.value) )
```

---

## Version History

- **v1.0**: Initial release with basic patterns, constraints, and accumulate
- **v1.1**: Added `no-loop`, `activation-group`, `lock-on-active` attributes
- **v1.2**: Added `contains`, `matches`, `memberOf` operators
- **v1.3**: Added null-safe dereference (`!.`), index access (`[]`)
- **v1.4**: Added entry points, custom accumulators, `drools.halt()`, `drools.setFocus()`, `drools.getRule()`
- **v1.5**: Added `enabled`, `auto-focus`, `duration` attributes; `coincides`, `during` temporal operators; `modify` block syntax
- **v1.6**: Added support for `not Pattern(...)` and `exists Pattern(...)` without parentheses; full JavaScript local variable support (`var`, `let`, `const`) in RHS
- **v1.7**: Added support for `from entry-point` inside accumulate patterns for CEP use cases
- **v1.8**: Added built-in `Number` type for accumulate results; `count(1)` shorthand syntax; arithmetic expressions in constraints (`$ts - 60000`); improved JS comment handling in RHS
