# JMESPath 与 DSV/CSV 数据源示例

本文档示例全部基于当前 RulesForge DSL 语法。

## 支持的数据源形式

在 LHS 使用 `from`：

```rfl
$var: FactType(...) from jmespath(INPUT, "JMESPATH_EXPRESSION")
$var: FactType(...) from dsv(INPUT, "FILTER_EXPRESSION")
$var: FactType(...) from csv(INPUT, "FILTER_EXPRESSION")
```

`INPUT` 支持：
- 字符串字面量
- `file("...")`

## JSON 字符串数据源

```rfl
rule "从 JSON 字符串提取高价值订单"
when
    $t: Trigger()
    $p: Purchase(amount > 100.0)
        from jmespath("{\"orders\":[{\"orderId\":\"A1\",\"amount\":120.5},{\"orderId\":\"A2\",\"amount\":80.0}]}", "orders[*]")
then
    insert HighValueOrder { orderId = $p.orderId, total = $p.amount }
end
```

## JSON 文件数据源

```rfl
rule "从 JSON 文件提取高价值客户"
when
    $t: Trigger()
    $c: Customer(tier == "premium")
        from jmespath(file("data/customers.json"), "customers[*]")
then
    insert PremiumCustomer { customerId = $c.id, name = $c.name }
end
```

## CSV/DSV 字符串数据源

```rfl
rule "从 CSV 字符串过滤行"
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

## CSV 文件数据源

```rfl
rule "从 CSV 文件过滤行"
when
    $t: Trigger()
    $r: Purchase(amount > 100.0)
        from csv(file("data/orders.csv"), "amount > 100")
then
    insert MatchedOrder { orderId = $r.orderId, amount = $r.amount }
end
```

## 与 accumulate 结合

```rfl
rule "累计高价值订单金额"
when
    $t: Trigger()
    $sum: Number(doubleValue > 1000.0) from accumulate(
        $p: Purchase() from jmespath(file("data/orders.json"), "orders[*]"),
        sum($p.amount)
    )
then
    insert RiskAlert { score = $sum.doubleValue }
end
```

```rfl
rule "从 CSV 累计高价值订单金额"
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

## 说明

- 旧的 RHS 写法 `for $x in jmespath(...)` 已是历史示例，不再推荐。
- 建议统一使用 LHS source pattern，行为更稳定、语义分析更清晰。
