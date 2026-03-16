# JMESPath and DSV/CSV Source Examples

This document contains examples that match the current RulesForge DSL source syntax.

## Supported Source Forms

Use source extraction on the LHS:

```rfl
$var: FactType(...) from json(INPUT, "JMESPATH_EXPRESSION")
$var: FactType(...) from dsv(INPUT, "FILTER_EXPRESSION")
$var: FactType(...) from csv(INPUT, "FILTER_EXPRESSION")
```

`INPUT` can be:
- a string literal
- `file("...")`

## JSON String Source

```rfl
rule "High Value Orders From JSON String"
when
    $t: Trigger()
    $p: Purchase(amount > 100.0)
        from json("{\"orders\":[{\"orderId\":\"A1\",\"amount\":120.5},{\"orderId\":\"A2\",\"amount\":80.0}]}", "orders[*]")
then
    insert HighValueOrder { orderId = $p.orderId, total = $p.amount }
end
```

## JSON File Source

```rfl
rule "Premium Customers From JSON File"
when
    $t: Trigger()
    $c: Customer(tier == "premium")
        from json(file("data/customers.json"), "customers[*]")
then
    insert PremiumCustomer { customerId = $c.id, name = $c.name }
end
```

## CSV/DSV String Source

```rfl
rule "Filter CSV Rows From String"
when
    $t: Trigger()
    $r: Purchase()
        from dsv(
            "orderId_s,amount_n,sym_s\nA1,120.5,USD\nA2,80.0,USD\n",
            "amount > 100 and sym == \"USD\""
        )
then
    insert MatchedOrder { orderId = $r.orderId, amount = $r.amount }
end
```

## CSV File Source

```rfl
rule "Filter CSV Rows From File"
when
    $t: Trigger()
    $r: Purchase(amount > 100.0)
        from csv(file("data/orders.csv"), "amount > 100")
then
    insert MatchedOrder { orderId = $r.orderId, amount = $r.amount }
end
```

## Combine Source + Accumulate

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

```rfl
rule "Total High Value Amount From CSV"
when
    $t: Trigger()
    $sum: Number(doubleValue > 1000.0) from accumulate(
        $p: Purchase() from csv(file("data/orders.csv"), "amount > 100"),
        sum($p.amount)
    )
then
    insert RiskAlert { score = $sum.doubleValue }
end
```

## Notes

- Old RHS syntax `for $x in json(...)` is legacy and no longer the primary DSL path.
- Prefer LHS source patterns for predictable rete behavior and semantic analysis.
