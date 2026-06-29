# RulesForge DSL (RFL) - Implementation-Aligned Guide

This document describes the DSL supported by the current parser and runtime.
If this doc conflicts with code, code wins.

Primary references:
- `parser/parser/rfl_grammar_lemon.y`
- `parser/src/rhs_parser.cpp`
- `parser/src/semantic_analyzer.cpp`
- `parser/src/expression_descriptor.cpp`
- parser tests under `parser/tests/`

## 1. File Structure

A file is a sequence of top-level statements:

- `package`
- `import`
- `global`
- `declare`
- `enum`
- `function` (parsed and stored)
- `query`
- `rule`

Comments supported:
- `// ...`
- `-- ...`
- `# ...`
- `/* ... */`

Example:

```rfl
package com.example.rules
import com.example.model.Customer
global List results

enum Status
    PENDING
    ACTIVE
    CLOSED
end

declare Customer
    id: int
    name: String
end

rule "Example"
when
    $c: Customer()
then
    insert AuditLog { msg = "hit" }
end
```

Notes:
- Qualified names used by `package`, `import`, globals, and type references may contain segments such as `time`, `length`, and `window`.
- Duration literals supported by the parser are `ms`, `s`, `m`, and `h`.

## 1.2 Import Forms

Standard namespace imports are accepted:

```rfl
import com.example.model.Customer
import com.example.model.*
```

External payload parsing is not part of the RulesForge engine runtime. Use TurboScript parser/data_bind, host-side code, or the RulesForge C API schema helpers to parse payloads and insert constructed facts.

## 1.1 Globals

`global` declarations are parsed and now initialized in each session:

```rfl
global List results
global Map metadata
global Set tags
```

Runtime behavior:
- globals are session-scoped (different sessions do not share them)
- default values by type name:
  - `*List` -> empty `TypedList`
  - `*Set` -> empty `ValueSet`
  - `*Map` -> empty `ValueMap`
  - other types -> `nil`
- host code can override values with `StatefulSession::set_global(name, value)`
- RHS variable resolution checks local bindings (`$x`) first, then globals

RHS examples:
- whole value: `items = $results`
- container size (numeric expr): `count = $results.size`
- map key access via dot: `v = $metadata.someKey`

## 2. Declarations

`declare` defines a fact schema:

```rfl
declare Customer
    id: int
    name: String
    score: double
end
```

Built-in `Number` type is available for accumulate results (e.g., `count/sum/avg` output).

Container declaration is supported:

```rfl
declare Basket
    tags: List<String>
    scores: Set<int>
    attrs: Map<String, double>
    owner: Customer
end
```

## 2.1 Enums

`enum` defines a fixed set of named values:

```rfl
enum OrderStatus
    PENDING
    CONFIRMED
    SHIPPED
    DELIVERED
    CANCELLED
end

enum Priority
    LOW
    MEDIUM
    HIGH
end
```

Enum values can be used in constraints and assignments:

```rfl
declare Order
    id: int
    status: String
    priority: Priority
end

rule "HighPriorityOrders"
when
    $o: Order(priority == HIGH, status == "PENDING")
then
    update $o { status = "CONFIRMED" }
end
```

Notes:
- Enum values are symbolic names (identifiers)
- Trailing commas are optional
- Enums are registered as types in the semantic analyzer
- Currently stored as String-compatible values at runtime

## 3. Rule Definition

Basic shape:

```rfl
rule "RuleName"
    // attributes...
when
    // LHS patterns...
then
    // RHS actions...
end
```

### 3.1 Supported Attributes

- `salience <int>`
- `extends "<ParentRuleName>"`
- `agenda-group "<group>"`
- `activation-group "<group>"`
- `timer <initial_ms>`
- `timer <initial_ms>, <repeat_ms>`
- `no-loop`
- `lock-on-active`
- `enabled true|false`
- `auto-focus true|false`
- `duration <ms>`

Notes:
- `extends` merges parent LHS conditions into child rules.
- `duration` and `timer` are separate attributes.

## 4. LHS (`when`) Syntax

The engine supports OR groups of AND patterns:

```rfl
when
    $a: A()
    $b: B()
or
    $c: C()
```

### 4.1 Pattern Types

1. Standard pattern:

```rfl
$p: Person(age > 18)
Order()
```

2. `not` pattern:

```rfl
not (Order(status == "paid"))
not Order(status == "paid")
```

3. `exists` pattern:

```rfl
exists (Order(total > 100))
exists Order(total > 100)
```

4. `forall` pattern:

```rfl
forall( $o: Order(), Order(status == "ok") )
```

5. `eval(...)` pattern:

```rfl
eval($a.value > 10 && $b.value < 20)
```

### 4.2 Constraints

Supported constraint composition:
- AND: `,` or `&&`
- OR: `||`

Inline field binding:

```rfl
Person($n: name, age > 18)
```

### 4.3 Field Reference

Supported field forms:
- `field`
- `$var`
- `$var.field`
- nested: `a.b.c`
- null-safe segment: `a!.b`
- index: `items[0]`, `items[-1]`, `meta["k"]`

### 4.4 Comparison Operators

- `==`, `!=`, `>`, `<`, `>=`, `<=`
- `contains`, `not contains`
- `containsKey`, `not containsKey`
- `matches`, `not matches`
- `startsWith`, `endsWith`, `lengthIs`
- `memberOf`, `not memberOf`
- `in (...)`, `not in (...)`

### 4.5 Temporal Operators

Supported temporal constraints:

```rfl
timestamp after $e1.timestamp
timestamp before $e1.timestamp
timestamp coincides $e1.timestamp
timestamp during $e1.timestamp
within 60s of $e1
```

Duration units: `ms`, `s`, `m`, `h`.

### 4.6 `from` Clause

Supported source clauses:

1. `from accumulate(...)`

```rfl
$total: Number() from accumulate(
    $p: Purchase(),
    sum($p.value)
)
```

Accumulate source patterns also support sliding-window declarations:

```rfl
$count: Number() from accumulate(
    Event() over window:length(5),
    count()
)

$recent: Number() from accumulate(
    Event() over window:time(60s),
    count()
)

$recentMs: Number() from accumulate(
    Event() over window:time(50),
    count()
)
```

Window notes:
- `over window:length(N)` keeps the latest `N` matching facts.
- `over window:time(X)` accepts either a duration literal such as `60s` or a bare integer in milliseconds such as `50`.
- Time windows use the fact `timestamp` field when present.
- `count()` counts matching source facts directly.
- `count(1)` is also accepted and is equivalent for counting matches.

Also supports arithmetic expression in accumulate argument:

```rfl
$total: Number() from accumulate(
    $p: LineItem(),
    sum($p.qty * $p.price)
)
```

2. `from collect(pattern)`

```rfl
$items: AnyType() from collect($o: Order())
```

3. `from unnest($binding.field)`

```rfl
$item: Item() from unnest($order.items)
```

4. `from json(input, "expr")` **[REMOVED]**

JSON parsing is not a RulesForge runtime responsibility. Use TurboScript parser/data_bind or another host-side parser to construct facts before insertion.

```rfl
$r: Row() from json("{\"orders\":[{\"amount\":120.5}]}", "orders[*]")
$r: Row() from json(file("data/orders.json"), "orders[*]")
```

**Recommended approach**:
```cpp
// Parse and bind outside RulesForge, then insert facts.
session->add_fact(row_fact);

// Simplified rule
$r: Row()
```

5. `from dsv/csv(input, "filter-expr")` **[REMOVED]**

CSV/DSV parsing is not a RulesForge runtime responsibility. Use TurboScript parser/data_bind or another host-side parser to construct facts before insertion.

```rfl
$r: Row() from dsv("amount_n,sym_s\n120.5,A\n80.0,B\n", "amount > 100 and sym == \"A\"")
$r: Row() from dsv(file("data/orders.csv"), "amount > 100 and sym == \"A\"")
$r: Row() from csv(file("data/orders.csv"), "amount > 100")
```

**Recommended approach**:
```cpp
// Parse and bind outside RulesForge, then insert facts.
session->add_fact(row_fact);

// Simplified rule
$r: Row()
```

Note:
- `DataSource` is fact-only in RulesForge.
- External file formats should be handled by TurboScript parser/data_bind or host application code.

6. `from entry-point "stream-name"`

```rfl
$e: Event() from entry-point "sensor-stream"
```

For accumulate source pattern, `from entry-point` is also allowed inside source pattern.

Accumulate source pattern does not ingest external data. Insert facts before rule execution:

```cpp
session->add_fact(purchase_fact);
```

### 4.7 Query Call Pattern (LHS)

Query invocation pattern is supported using quoted query name:

```rfl
"FindAdults"($person)
```

## 5. RHS (`then`) Actions

RHS is parsed by `RhsParser` and compiled into the supported RHS execution path.

### 5.1 Action Types

- `insert Type { ... }`
- `insertLogical Type { ... }`
- `update $var { ... }`
- `retract $var`
- `halt`
- `setFocus("group")`
- `invoke functionName(arg1, arg2, ...)`
- `if / else if / else`
- `for`
- `while`
- `switch / case / default`
- `break`
- `continue`

Example:

```rfl
then
    if $o.total > 1000 {
        update $o { tier = "VIP" }
    } else {
        update $o { tier = "STD" }
    }
    invoke emitAudit($o.id, "tier-updated")
end
```

### 5.2 `for` Forms

1. Iterate collection field:

```rfl
for $x in $order.items { ... }
```

2. Iterate variable/container directly:

```rfl
for $x in $results { ... }
```

3. Iterate explicit variable list:

```rfl
for $x in ($a, $b, $c) { ... }
```

### 5.3 Assignment Value Types

Supported assignment values:
- string literal
- boolean literal (`true` / `false`)
- variable reference (`$v`, `$v.field`)
- numeric/expression value
- host function call value, e.g. `metric($o.total)`

Expression syntax in RHS supports:
- arithmetic: `+ - * / % ^`
- comparison: `== != > < >= <=`
- logic: `&& || !` and keywords `and or not xor`
- ternary: `cond ? a : b`
- function call style: `fn(arg1, arg2)`
- constants: `pi`, `e`, `inf`, `epsilon`

Built-in expression functions (current runtime support):
- basic/math: `abs ceil floor round trunc sgn frac sqrt pow root exp log log2 log10`
- trig/hyperbolic: `sin cos tan asin acos atan atan2 sinh cosh tanh asinh acosh atanh`
- comparison/range: `min max clamp inrange`
- aggregate: `avg sum mul`
- special: `erf erfc ncdf hypot mod fmod expm1 log1p logn`
- conditional: `if(cond, a, b)`
- string: `strlen substr trim replace indexOf contains upper lower toUpper toLower`

RHS parser compatibility aliases:
- `concat(a, b, c)` is supported and rewritten to string concatenation: `(a + b + c)`
- `to_upper(x)` is normalized to `upper(x)`
- `to_lower(x)` is normalized to `lower(x)`

Practical note:
- document only functions that the current parser accepts and the current runtime actually registers
- do not assume the entire upstream ExprTk function surface is available unless it is wired here

### 5.4 Host Callback Support

RHS function calls can be backed by explicitly registered host callbacks.

Rule side usage is the same:

```rfl
then
    invoke hostLog($s.id, $s.temperature)
    update $s { score = hostMetric($s.temperature, 2) }
end
```

Host integration path (C API):
- direct registration: `ruleforge_kb_register_native_function(...)`

Runtime resolution:
1. parse RHS `invoke` / assignment call expression
2. resolve function name from the KnowledgeBase callback registry
3. evaluate arguments
4. call host callback

For ownership and failure semantics, see [`C_API_CONTRACT.md`](/C:/projects/cpp/rulesforge/docs/C_API_CONTRACT.md).

## 5.5 Fact Input API

RulesForge engine sessions accept already constructed facts. External files and payloads should be parsed by TurboScript parser/data_bind, host application code, or the C API schema-aware DataBind helpers before insertion.

### C++ API

```cpp
#include "engine/data_source.hpp"

// Add fact object
session->add_data(fact);
session->add_data(DataSource::fact(fact));
```

Current runtime behavior:
- `add_data(DataSource::fact(...))` delegates to `add_fact(...)`
- fact validation still uses declarations loaded from RFL or `import "name.schema"`
- C++ engine runtime is fact-only
- schema-aware C API helpers can use installed `TurboScript::DataBind` to bind JSON/CSV/XML/binary payloads into session-owned facts

### Migration from external data clauses

**Before** (deprecated):
```rfl
rule "ProcessOrders"
when
    $r: Row() from json(file("orders.json"), "orders[*]")
then
    insert Order { id = $r.id }
end
```

**After**:
```cpp
// Host or TurboScript side parses JSON and constructs Row facts.
session->add_fact(row_fact);
```

```rfl
// Simplified rule
rule "ProcessOrders"
when
    $r: Row()
then
    insert Order { id = $r.id }
end
```

### Benefits

- **Unified interface**: Single entry point for all data types
- **Better control**: Load data when needed, not on every rule match
- **Performance**: One-time loading vs repeated file I/O
- **Testability**: Easy to mock data sources in tests

## 6. Queries

Query declaration supports identifier or string name:

```rfl
query findPerson(NameHolder $name)
    $p: Person(name == $name.value)
end
```

```rfl
query "findPerson"(NameHolder $name)
    $p: Person(name == $name.value)
end
```

Query params are typed and bound as variables in query body.

## 7. Important Runtime Notes

1. `update` triggers immediate rete propagation/re-evaluation.
2. `while` has a safety cap (`max_iterations = 1000` in parser output).
3. Semantic analyzer enforces:
   - undeclared bindings are errors
   - undeclared types/fields are errors
   - duplicate bindings are errors
   - `extends` target must exist
4. `function` declarations are parsed into parser state; keep expectations aligned with actual runtime usage in your version.

## 8. Minimal End-to-End Example

```rfl
package com.shop

declare Order
    id: int
    total: double
    tier: String
end

rule "Tiering"
salience 10
when
    $o: Order(total > 0)
then
    if $o.total >= 1000 {
        update $o { tier = "VIP" }
    } else {
        update $o { tier = "STD" }
    }
end

query "VipOrders"
    $o: Order(tier == "VIP")
end
```

