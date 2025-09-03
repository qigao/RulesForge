# JMESPath 与 Drills 规则引擎的集成

## 概述

Drills 规则引擎现在集成了 **完整的** JMESPath 功能，支持在 JavaScript 代码中进行强大的 JSON 数据查询和转换操作。我们已将底层实现替换为健壮的 `jsoncons` 库，确保了功能完整性和稳定性。

## JMESPath 如何让您的生活更轻松

在处理复杂的 JSON 数据时，手动编写 JavaScript 代码来提取、过滤或转换数据会变得异常繁琐、容易出错且难以维护。JMESPath 正是为了解决这些痛点而生。

想象一下，您需要从一个深层嵌套的 JSON 对象中提取特定信息，或者从一个数组中筛选出符合条件的元素，再或者对某个字段进行聚合计算。如果没有 JMESPath，您可能需要：

-   **冗长的点语法链**：`data.users[0].profile.address.city`
-   **复杂的循环和条件判断**：遍历数组，使用 `if` 语句进行过滤。
-   **手动聚合**：编写循环来计算 `sum`、`max`、`min` 等。

而 JMESPath 允许您用简洁、声明式的表达式完成这些任务，大大减少了代码量，提高了可读性，并降低了出错的概率。它就像是 JSON 数据的“SQL”，让您能够以更直观的方式与数据交互。

## 当前支持的功能

### ✅ 已实现的 JMESPath 功能 (基于 jsoncons)

Drills 现在支持 JMESPath 规范的绝大部分功能，包括：

1.  **基本属性访问**：`user.name`, `user.profile.age`
2.  **数组投影**：`products[*].price`
3.  **数组过滤**：`orders[?total > 100]`, `customers[?membership.tier == 'premium']`
4.  **数组切片**：`items[1:3]`
5.  **对象值**：`user.profile.*`
6.  **多选列表**：`[name, age]`
7.  **多选哈希**：`{name: name, age: age}`
8.  **管道表达式**：`items | length(@)`
9.  **内置函数**：`length(@)`, `sum(@)`, `max(@)`, `min(@)`, `avg(@)`, `keys(@)`, `values(@)`, `join(' ', @)` 等。
10. **条件表达式**：`foo || bar`, `foo && bar`
11. **比较操作符**：`==`, `!=`, `<`, `<=`, `>`, `>=`
12. **字面量**：支持字符串、数字、布尔值和 null 字面量。
13. **基础错误处理**：无效查询或 JSON 格式错误会抛出 JavaScript 异常。

## 基本用法

### JavaScript 中的 `jmespath` 函数

```javascript
// 基本语法
var result = jmespath(json_string, query_expression);
```

`json_string` 必须是有效的 JSON 字符串。`query_expression` 必须是有效的 JMESPath 表达式。

## 支持的查询示例

### 1. 基本属性访问与嵌套

```javascript
// JSON 数据
var jsonData = '{"user": {"profile": {"age": 25, "name": "Alice", "address": {"city": "New York"}}}}';

// 提取嵌套属性
var age = jmespath(jsonData, 'user.profile.age');   // 结果: 25
var name = jmespath(jsonData, 'user.profile.name'); // 结果: "Alice"
var city = jmespath(jsonData, 'user.profile.address.city'); // 结果: "New York"
```

### 2. 数组投影与过滤

```javascript
// JSON 数据
var productsData = '[{"name": "Laptop", "price": 1200}, {"name": "Mouse", "price": 25}, {"name": "Keyboard", "price": 75}]';

// 数组投影：提取所有产品的价格
var prices = jmespath(productsData, '[*].price'); // 结果: [1200, 25, 75]

// 数组过滤：查找价格高于 100 的产品
var expensiveProducts = jmespath(productsData, '[?price > `100`]'); 
// 结果: [{"name": "Laptop", "price": 1200}]
```

### 3. 内置函数与管道操作

```javascript
// JSON 数据
var ordersData = '{"orders": [{"amount": 100}, {"amount": 200}, {"amount": 50}]}';

// 管道操作与长度函数
var orderCount = jmespath(ordersData, 'orders | length(@)'); // 结果: 3

// 管道操作与求和函数
var totalAmount = jmespath(ordersData, 'orders[*].amount | sum(@)'); // 结果: 350
```

### 4. 复杂查询示例

更多复杂的 JMESPath 查询示例，请参考 [JMESPath 官方文档](https://jmespath.org/examples.html) 或项目中的 `docs/examples/JMESPATH_EXAMPLES.md`。

## 实际应用示例

### 规则中的使用

```drl
declare TestFact
    name: String
    payload: String
end

rule "Comprehensive JMESPath Usage"
when
    $fact : TestFact(name == "jmespath_demo")
then
    // 假设 payload 包含复杂的 JSON 数据
    var complexJson = fact.payload; 

    // 示例 1: 提取高价值订单 (过滤)
    // 场景: 找出所有总金额超过 1000 的订单
    var highValueOrders = jmespath(complexJson, 'orders[?total > `1000`]');
    console.log("高价值订单:", JSON.stringify(highValueOrders));

    // 示例 2: 计算总收入 (聚合)
    // 场景: 计算所有订单的总金额
    var totalRevenue = jmespath(complexJson, 'orders[*].amount | sum(@)');
    console.log("总收入:", totalRevenue);

    // 示例 3: 查找特定客户的电子邮件 (过滤 + 投影 + 索引)
    // 场景: 找到 ID 为 "CUST-001" 的客户的电子邮件地址
    var customerEmail = jmespath(complexJson, 'customers[?id == `CUST-001`].email | [0]');
    console.log("客户电子邮件:", customerEmail);

    // 示例 4: 提取所有产品类别 (投影 + 去重)
    // 场景: 获取所有商品的唯一类别列表
    var productCategories = jmespath(complexJson, 'products[*].category | unique(@)');
    console.log("产品类别:", JSON.stringify(productCategories));

    // 示例 5: 查找库存不足的商品名称 (过滤 + 投影)
    // 场景: 找出所有库存量低于 10 的商品名称
    var lowStockItems = jmespath(complexJson, 'products[?inventory < `10`].name');
    console.log("低库存商品:", JSON.stringify(lowStockItems));
end
```

## 错误处理

### 查询失败情况

现在，当 JMESPath 查询遇到无效 JSON 输入或语法错误时，会抛出 JavaScript 异常。您可以使用 `try-catch` 块来捕获这些错误。

```javascript
try {
    // 无效 JSON 输入
    var result1 = jmespath("invalid json", 'user.name');
    console.log("Result 1:", result1);
} catch (e) {
    console.error("Caught JSON parsing error:", e.message);
}

try {
    // 无效 JMESPath 表达式
    var result2 = jmespath('{"valid": "json"}', 'invalid[[[syntax');
    console.log("Result 2:", result2);
} catch (e) {
    console.error("Caught JMESPath syntax error:", e.message);
}
```

## 性能注意事项

1.  **JSON 解析开销**：每次调用 `jmespath()` 都会解析 JSON 字符串。对于频繁查询同一 JSON 数据的情况，建议先在 JavaScript 中解析一次 JSON (`JSON.parse()`)，然后将 JavaScript 对象传递给 `jmespath` 函数（如果 `jmespath` 函数支持直接接收 JS 对象，否则仍需转为字符串）。
2.  **内存使用**：大型 JSON 数据会占用相应内存。处理完后，确保及时释放 JavaScript 对象的引用，以便垃圾回收。
3.  **查询复杂度**：虽然 `jsoncons` 提供了高效的实现，但过于复杂或深层嵌套的查询仍然会影响性能。

## 最佳实践

为了充分利用 JMESPath 并保持代码的清晰和高效，请遵循以下最佳实践：

1.  **优先使用 JMESPath 表达式**：尽可能将数据提取和转换逻辑封装在 JMESPath 表达式中，而不是在 JavaScript 中手动编写循环和条件。这会使您的规则更简洁、更易读。
2.  **理解数据结构**：在编写 JMESPath 表达式之前，确保您对 JSON 数据的结构有清晰的理解。
3.  **测试您的表达式**：对于复杂的 JMESPath 表达式，建议在独立的工具（如在线 JMESPath 调试器）中进行测试，以确保它们按预期工作。
4.  **处理 `null` 结果**：JMESPath 查询在找不到匹配项时通常返回 `null`。在您的 JavaScript 代码中，始终检查 `jmespath()` 函数的返回值是否为 `null` 或 `undefined`。
5.  **性能优化**：对于在规则中频繁执行的 JMESPath 查询，考虑是否可以预处理 JSON 数据，或者优化查询表达式以减少计算量。

## 版本历史

### v1.0 - 基础实现 (已废弃)
-   ✅ 基本属性访问
-   ✅ 数组长度查询
-   ✅ 基础错误处理

### v2.0 - 完整 JMESPath 支持 (当前)
-   ✅ 集成 `jsoncons` 库，提供完整的 JMESPath 规范支持。
-   ✅ 支持所有 JMESPath 内置函数、数组投影、过滤、切片等。
-   ✅ 错误处理机制更新为抛出 JavaScript 异常。
-   ✅ 提升了查询的稳定性和性能。
