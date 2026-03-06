# RulesForge DSL (RFL) - Implementation-Aligned Guide

This document describes the DSL supported by the current parser and runtime.
If this doc conflicts with code, code wins.

Primary references:
- `rulesforge/src/parser/rfl_grammar_lemon.y`
- `rulesforge/src/parser/rhs_parser.cpp`
- `rulesforge/src/parser/semantic_analyzer.cpp`
- `rulesforge/src/parser/expression_evaluator.cpp`
- parser tests under `rulesforge/test/parser/`

## 1. File Structure

A file is a sequence of top-level statements:

- `package`
- `import`
- `global`
- `declare`
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

4. `from jmespath(input, "expr")`

```rfl
$r: Row() from jmespath("{\"orders\":[{\"amount\":120.5}]}", "orders[*]")
$r: Row() from jmespath(file("data/orders.json"), "orders[*]")
```

5. `from dsv/csv(input, "filter-expr")`

```rfl
$r: Row() from dsv("amount_n,sym_s\n120.5,A\n80.0,B\n", "amount > 100 and sym == \"A\"")
$r: Row() from dsv(file("data/orders.csv"), "amount > 100 and sym == \"A\"")
$r: Row() from csv(file("data/orders.csv"), "amount > 100")
```

6. `from entry-point "stream-name"`

```rfl
$e: Event() from entry-point "sensor-stream"
```

For accumulate source pattern, `from entry-point` is also allowed inside source pattern.

Accumulate source pattern also supports the same data-source clauses:

```rfl
$sum: Number() from accumulate(
    $p: Purchase() from jmespath(file("data/orders.json"), "orders[*]"),
    sum($p.amount)
)

$sum2: Number() from accumulate(
    $r: Purchase() from csv(file("data/orders.csv"), "amount > 100"),
    sum($r.amount)
)
```

### 4.7 Query Call Pattern (LHS)

Query invocation pattern is supported using quoted query name:

```rfl
"FindAdults"($person)
```

## 5. RHS (`then`) Native Actions

RHS is parsed by `RhsParser` and compiled into native actions.

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
- native function call value (registered from host/plugin), e.g. `metric($o.total)`

Expression syntax in RHS supports:
- arithmetic: `+ - * / % ^`
- comparison: `== != > < >= <=`
- logic: `&& || !` and keywords `and or not xor`
- ternary: `cond ? a : b`
- function call style: `fn(arg1, arg2)`
- constants: `pi`, `e`, `inf`, `epsilon`

Built-in expression functions (from `ExpressionEvaluator`):
- basic/math: `abs ceil floor round trunc sgn frac sqrt pow root exp log log2 log10`
- trig/hyperbolic: `sin cos tan asin acos atan atan2 sinh cosh tanh asinh acosh atanh`
- comparison/range: `min max clamp inrange`
- aggregate: `avg sum mul`
- special: `erf erfc ncdf hypot mod fmod expm1 log1p logn`
- conditional: `if(cond, a, b)`

### 5.4 Native Function and DLL Function Table Support

RHS function calls can be backed by host-registered native functions or plugin DLL/so function tables.

Rule side usage is the same:

```rfl
then
    invoke pluginLog($s.id, $s.temperature)
    update $s { score = pluginMetric($s.temperature, 2) }
end
```

Host integration paths (C API):
- direct registration: `ruleforge_kb_register_native_function(...)`
- DLL/so table loading: `ruleforge_kb_load_native_function_table(...)`

Runtime resolution:
1. parse RHS `invoke` / assignment call expression
2. resolve function name from KnowledgeBase native registry
3. evaluate arguments
4. call native callback

See full C API examples:
- `capi/examples/CAPI_NATIVE_DLL_EN.md`
- `capi/examples/NATIVE_FUNCTIONS.md`

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
