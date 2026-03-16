# JMESPath 与 DSV/CSV Source 集成

## 概述

RulesForge 当前支持在 LHS 使用外部数据源模式：

- `from json(...)`：JSON 提取
- `from dsv(...)` / `from csv(...)`：CSV/DSV 过滤

这些 source 产生的是网络中的临时行 fact，不会持久保存在工作内存中。

实现说明：

- 内部 rete 节点名为 `SourceExtractNode` 。
- DSL 语法不变，仍然使用 `from json(...)` / `from dsv(...)` / `from csv(...)`。

## JMESPath Source

语法：

```rfl
$row: RowType() from json(INPUT, "JMESPATH_EXPRESSION")
```

`INPUT` 支持：

1. JSON 字符串字面量
2. `file("...")` JSON 文件

示例：

```rfl
rule "高价值订单"
when
    $t: Trigger()
    $p: Purchase(amount > 100.0)
        from json(file("data/orders.json"), "orders[*]")
then
    insert Matched { }
end
```

## DSV/CSV Source

语法：

```rfl
$row: RowType() from dsv(INPUT, "FILTER_EXPRESSION")
$row: RowType() from csv(INPUT, "FILTER_EXPRESSION")
```

`INPUT` 支持：

1. CSV/DSV 字符串字面量
2. `file("...")` 文件

示例：

```rfl
rule "过滤 CSV 行"
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

## 字段映射与过滤

- 表头后缀 `_n/_s/_b` 会在映射时去掉。
  例如：`amount_n -> amount`
- `FILTER_EXPRESSION` 使用 TurboNet DSV filter 语法。

## 运行时说明

1. Source 行是 transient fact。
2. 可在同一 pattern 中写约束（例如 `Purchase(amount > 100)`）。
3. `fire_all_rules()` 的 stale activation 检查会忽略 `id == 0` 的 transient fact。

## 在 accumulate 中使用 Source Pattern

`accumulate(...)` 的 source pattern 也支持 `from json(...)` / `from dsv(...)` / `from csv(...)`。

```rfl
rule "累计高价值订单金额"
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

## 迁移说明

以下旧写法已废弃：

```rfl
$r: Row() from json(PAYLOAD_REF, "orders[*]")
```

请改为 string/file 输入：

```rfl
$r: Row() from json(file("data/orders.json"), "orders[*]")
```
