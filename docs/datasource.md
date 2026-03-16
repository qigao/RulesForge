# JMESPath and DSV Source Integration

## Overview

RulesForge supports JSON/CSV-style external data sources on the LHS through `from` clauses.

- `from json(...)` for JSON extraction
- `from dsv(...)` / `from csv(...)` for CSV/DSV row filtering

These sources create transient row facts in the rete network (not persisted in working memory).

Implementation note:
- Internal rete node name is `SourceExtractNode` (renamed from `JmesPathNode`).
- DSL syntax is unchanged: continue using `from json(...)`, `from dsv(...)`, `from csv(...)`.

## JMESPath Source

Syntax:

```rfl
$row: RowType() from json(INPUT, "JMESPATH_EXPRESSION")
```

Supported `INPUT` forms:

1. JSON string literal

```rfl
$p: Purchase() from json("{\"orders\":[{\"amount\":120.5}]}", "orders[*]")
```

2. JSON file

```rfl
$p: Purchase() from json(file("data/orders.json"), "orders[*]")
```

Example with constraints on extracted rows:

```rfl
rule "High Value Orders"
when
    $t: Trigger()
    $p: Purchase(amount > 100.0) from json(file("data/orders.json"), "orders[*]")
then
    insert Matched { }
end
```

## DSV/CSV Source

Syntax:

```rfl
$row: RowType() from dsv(INPUT, "FILTER_EXPRESSION")
$row: RowType() from csv(INPUT, "FILTER_EXPRESSION")
```

Supported `INPUT` forms:

1. CSV/DSV string literal
2. CSV/DSV file via `file("...")`

Example:

```rfl
rule "Filtered CSV Rows"
when
    $t: Trigger()
    $r: Purchase(amount > 100.0) from dsv(
        "amount_n,sym_s\n120.5,A\n80.0,B\n",
        "amount > 100 and sym == \"A\""
    )
then
    insert Matched { }
end
```

Notes:

- Header suffixes `_n/_s/_b` are accepted and stripped when mapping to fact fields
  Example: `amount_n` -> `amount`
- `FILTER_EXPRESSION` follows TurboNet DSV filter syntax

## Runtime Notes

1. Source rows are transient network facts.
2. They can be constrained in the same pattern (`Purchase(amount > 100)`).
3. `fire_all_rules()` ignores `id == 0` transient facts in stale-activation checks.

## Accumulate with Source Pattern

`accumulate(...)` source pattern can also use `from json(...)` / `from dsv(...)` / `from csv(...)`.

```rfl
rule "Total High Value Amount"
when
    $t: Trigger()
    $sum: Number(doubleValue > 1000.0) from accumulate(
        $p: Purchase() from json(file("data/orders.json"), "orders[*]"),
        sum($p.amount)
    )
then
    insert RiskAlert { score = $sum.doubleValue }
end
```

## Migration Notes

Old style examples like this are obsolete:

```rfl
// old
$r: Row() from json(PAYLOAD_REF, "orders[*]")
```

Use string/file input style instead:

```rfl
// new
$r: Row() from json(file("data/orders.json"), "orders[*]")
```
