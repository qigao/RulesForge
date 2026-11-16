# RulesForge DSL (RFL) - Implementation-Aligned Guide

This document describes the DSL supported by the current rulesforge/parser and runtime.
If this doc conflicts with code, code wins.


## Toad RHS CALL

Programs bind static names to explicitly admitted Toad exports. Ordinary KB
loading rejects CALL without a Program binding. For example:

```rfl
call "pricing.calculate"($order.amount) into $price
update $order { total = $price.total }
```

Arguments are typed references or literals; whole Facts and implicit IDs are
not accepted. Results are read-only local values, with lexical branch/loop
scopes, and cannot be UPDATE/RETRACT targets. Missing bindings, arity and known
literal-type errors fail loading. Runtime type/field/quota errors roll back the
current RHS. There is no implicit numeric coercion or ambient script I/O.
Deployment, API ownership and supported value shapes are documented in
[Script Programs](SCRIPT_PROGRAM.md).

Primary references:
- `core/parser/parser/rfl_grammar_lemon.y`
- `core/parser/src/rhs_parser.cpp`
- `core/parser/src/semantic_analyzer.cpp`
- `core/parser/src/expression_descriptor.cpp`
- rulesforge/parser tests under `core/parser/tests/`

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
import schema "customer.schema"
global List results

declare AuditLog
    msg: String
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
- Duration literals supported by the rulesforge/parser are `ms`, `s`, `m`, and `h`.

## 1.1 Import Forms

Standard namespace imports are accepted:

```rfl
import com.example.model.Customer
import com.example.model.*
```

Schema imports are accepted for external input types:

```rfl
import schema "customer.schema"
import "orders.schema"
```

RulesForge uses one schema source for external input data:
- JSON, YAML, CSV, XML, and binary payloads must be described by a `.schema` file.
- RFL `declare` is still supported, but it is for internal derived facts inserted by rules or host code.
- A RFL `declare` or `enum` cannot reuse the same short type name as an imported schema type.
- Schema import paths are resolved relative to the RFL file that contains the import, then through configured base directories.
- Standard namespace imports resolve RFL files such as `com.example.Type` -> `com/example/Type.rfl` through configured base directories.

## 1.2 Globals

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

`declare` defines an internal fact schema:

```rfl
declare Customer
    id: int
    name: String
    score: double
end
```

Use `declare` for facts produced inside the rule session, for example alerts, decisions, audit rows, or intermediate calculation facts. Do not use it as the schema for external JSON/YAML/CSV/XML/binary input. External input types must come from `import schema "name.schema"`.

External input example:

```rfl
package com.example.pricing

import schema "payments.schema"

declare PricingAudit
    message: String
end

rule "Audit Order"
when
    $order: Order(finalPrice > 0.01)
then
    insert PricingAudit { message = "priced" }
end
```

Here `Order` is defined in `payments.schema`; `PricingAudit` is an internal derived fact.

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

4. `from entry-point "stream-name"`

```rfl
$e: Event() from entry-point "sensor-stream"
```

For accumulate source pattern, `from entry-point` is also allowed inside source pattern.

`from` does not parse files or network payloads. Bind external data through the
host API before matching it; see [`DATA_INGESTION.md`](./DATA_INGESTION.md).

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
- registered expression function call, e.g. `max($o.total, 0)`

Numeric RHS assignment literals must fit their representation: integers use
signed 64-bit values and floating literals must be finite doubles. Overflow fails
parsing; it is never retried as an approximate expression.

The native scalar executor provides arithmetic `+ - * /`, scalar comparisons
`== != > < >= <=`, and the `pi` constant. Its shared numeric function vocabulary is:

- unary: `floor ceil abs round sqrt sin cos tan acos asin atan log exp`
- binary: `min max pow fmod`

Core scalar expressions compile to MIR native code during loading. They support
short-circuit `&& ||` and unary `!` at the scalar compiler boundary, with the
existing RFL/RHS grammar determining how those forms enter a rule. Binary
arithmetic is left-associative and respects precedence. Signed integer `+ - *`
is exact and checked for overflow; division and numeric functions return doubles.
Non-finite arithmetic and division by zero fail execution and roll back the RHS.
Unsupported functions, including historical `concat`, `to_upper` and `to_lower`,
and wrong argument counts fail loading. There is no alternate evaluator after
failure. Use typed Toad `CALL` bindings for complex application computation.

Each scalar program admits at most 16 KiB of source, 256 operations, 64 variables
and 64 levels of nesting. Field paths are parsed once, values are read only on
executed branches, and repeated variables are cached for that invocation.
Missing LHS fields retain their non-match semantics; present nonnumeric
arithmetic operands fail rather than silently suppressing a match.

An expression failure during nontransactional RETE propagation marks the session
inconsistent. Firing, querying or mutating that session fails until an explicit
reset succeeds; the failed mutation does not promise rollback. RHS expression
failures continue through the existing transaction rollback contract.

### 5.4 Predicate Boundary

Rule conditions use built-in expressions and typed predicates only. The C API
does not register host predicates or dynamically loaded function tables, and
rules cannot invoke external services. The host consumes rule results and owns
all external side effects.

## 5.5 Fact Input Boundary

RulesForge engine sessions accept already constructed facts. External files and payloads must be schema-bound when they enter through the public C API.

### Internal C++ engine behavior

```cpp
#include "runtime/data_source.hpp"

// Add fact object
session->add_data(fact);
session->add_data(DataSource::fact(fact));
```

Current runtime behavior:
- `add_data(DataSource::fact(...))` delegates to `add_fact(...)`
- fact validation uses declarations loaded from RFL internal `declare` or schema imports
- C++ engine runtime is fact-only
- schema-aware C API helpers use `Salts::DataBind` to bind JSON/YAML/CSV/XML/binary payloads into session-owned facts
- the target external fact type must be imported into the KnowledgeBase from `.schema`

These interfaces support the in-tree implementation and tests. Applications,
including C++ applications, use the installed C ABI in `rules_forge.h`.

Complete-document and incremental input APIs are documented in
[`DATA_INGESTION.md`](./DATA_INGESTION.md). Input and file I/O remain host
responsibilities rather than pattern-source behavior.

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
2. `while` has a safety cap (`max_iterations = 1000` in rulesforge/parser output).
3. Semantic analyzer enforces:
   - undeclared bindings are errors
   - undeclared types/fields are errors
   - duplicate bindings are errors
   - `extends` target must exist
4. `function` declarations are parsed into rulesforge/parser state; keep expectations aligned with actual runtime usage in your version.

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
