# RulesForge 规则引擎 - 完整用户指南

*掌握声明式业务逻辑的艺术*

## 目录

1. [**核心概念**](#1-核心概念)
2. [**RFL 语言参考**](#2-rfl-语言参考)
3. [**RHS 动作与控制流**](#3-rhs-动作与控制流)
4. [**高级模式**](#4-高级模式)
5. [**性能指南**](#5-性能指南)
6. [**故障排除**](#6-故障排除)

---

## 1. 核心概念

### Rete 算法实践

RulesForge 实现了 **Rete 算法**，这是一种强大的模式匹配技术，具有以下特点：

- ✅ **增量处理** - 只重新计算已更改的部分
- ✅ **内存网络** - 存储中间结果以提高速度
- ✅ **冲突解决** - 智能处理多个规则匹配
- ✅ **真值维护** - 当前提条件改变时自动撤销派生事实

```rfl
rule "价格警报"
when
    $product: Product(price < 100)
    $user: User(interests contains $product.category)
then
    // 仅当两个条件都满足时才触发
    // 如果价格超过 100，则自动撤销警报
    insertLogical PriceAlert { userId = $user.id, productId = $product.id }
end
```

### 工作内存 vs 知识库

```cpp
// 知识库 = 不可变编译规则 (线程安全)
auto kb = build_knowledge_base(rfl_source, result);

// 会话 = 可变工作内存 (每个线程一个)
auto session1 = kb->create_session(); // 线程 1
auto session2 = kb->create_session(); // 线程 2

// 多个会话可以共享同一个知识库
session1->add_fact(customer1);
session2->add_fact(customer2); // 独立数据
```

### 数据流：事实、规则和引擎

1. **规则被编译到知识库中：**
    - RFL 文件被解析并编译成优化的 Rete 网络，存储在 `KnowledgeBase` 中。`KnowledgeBase` 是不可变的且线程安全的。

2. **事实被插入到工作内存中：**
    - 应用程序数据（"事实"）被插入到 `StatefulSession`（引擎的工作内存）中。每个会话都是可变的，通常与单个线程绑定。

3. **引擎执行模式匹配：**
    - Rete 算法根据规则持续评估事实。当事实匹配规则的 LHS 条件时，该规则被激活。

4. **规则动作执行：**
    - 激活的规则执行 RHS 动作（Native 语法），可以：
        - **插入新事实**：`insert TypeName { ... }`
        - **修改现有事实**：`update $var { ... }`
        - **撤销事实**：`retract $var`
        - **逻辑插入**：`insertLogical TypeName { ... }`（条件不满足时自动撤销）

5. **查询结果：**
    - 规则触发后，查询 `StatefulSession` 以检索结果。

---

## 2. RFL 语言参考

### 2.1 文件结构

```rfl
package com.example.business.rules

import com.example.model.Customer
import com.example.util.DateUtils

global java.util.List notifications

declare CustomEvent
    timestamp: long
    userId: int
    action: String
end

query "active_customers"
    $c: Customer(status == "Active")
end

rule "业务规则 1"
when
    // 条件
then
    // 动作
end
```

### 2.2 类型声明

定义数据模式：

```rfl
declare Customer
    id: int                    // 必填字段
    name: String              // 字符串类型
    balance: double           // 数字类型
    active: boolean           // 布尔值
    registrationDate: long    // Unix 时间戳
    metadata: Object          // 通用对象
end

declare VipStatus
    customerId: int
    level: String
    expires: long
end
```

**支持的类型：**

- `int` (64 位有符号整数)
- `double` (64 位浮点数)
- `String` (UTF-8 字符串)
- `boolean` (true/false)
- `long` (int 的别名)
- `Object` (通用变体类型)

### 2.3 规则语法

```rfl
rule "规则名称"
    salience 10              // 优先级 (越高越早)
    agenda-group "validation" // 规则组
when
    // 左侧 (LHS) - 条件
    $customer: Customer(
        age >= 18,           // 简单约束
        status == "Active",  // 字符串比较
        balance > 1000.0     // 数字约束
    )

    // 负面条件
    not VipStatus(customerId == $customer.id)

    // 存在性检查
    exists Order(customerId == $customer.id)

then
    // 右侧 (RHS) - Native 动作
    insert VipStatus {
        customerId = $customer.id,
        level = "Gold"
    }
end
```

### 2.4 约束运算符

| 运算符 | 描述 | 示例 |
|----------|-------------|---------|
| `==` | 等于 | `status == "Active"` |
| `!=` | 不等于 | `age != 0` |
| `<`, `<=`, `>`, `>=` | 比较 | `balance > 1000` |
| `contains` | 字符串/数组包含 | `name contains "John"` |
| `matches` | 正则表达式匹配 | `email matches ".*@company\.com"` |
| `in` | 值在列表中 | `status in ("Active", "Pending")` |
| `not in` | 值不在列表中 | `country not in ("US", "CA")` |

### 2.5 模式匹配

#### 基本模式

```rfl
$customer: Customer(balance > 1000)
```

#### 多个约束

```rfl
$order: Order(
    amount > 100,
    status == "Completed",
    customerId == $customer.id
)
```

#### 嵌套字段访问

```rfl
$user: User(profile.preferences.newsletter == true)
```

#### 变量绑定

```rfl
$customer: Customer($customerId: id, balance > 1000)
$orders: Order(customerId == $customerId)
```

### 2.6 高级模式

#### Accumulate - 数据聚合

```rfl
rule "高价值客户"
when
    $customer: Customer()
    $totalSpent: Number() from accumulate(
        Order(customerId == $customer.id, $amount: amount),
        sum($amount)
    )
    eval($totalSpent > 10000)
then
    insert HighValueCustomer { customerId = $customer.id, totalSpent = $totalSpent }
end
```

**Accumulate 函数：**

- `count()` - 计数匹配项
- `sum($field)` - 求和数字字段
- `min($field)` - 最小值
- `max($field)` - 最大值
- `average($field)` - 平均值

#### Collect - 收集事实

```rfl
rule "捆绑订单"
when
    $customer: Customer()
    $orders: List() from collect(
        Order(customerId == $customer.id)
    )
    eval($orders.size() >= 3)
then
    insert BundleDiscount { customerId = $customer.id }
end
```

#### Forall - 全称量词

```rfl
rule "所有订单已完成"
when
    $customer: Customer()
    forall(
        $order: Order(customerId == $customer.id)
        Order(this == $order, status == "Completed")
    )
then
    insert AllOrdersComplete { customerId = $customer.id }
end
```

### 2.7 查询

用于数据检索的参数化查询：

```rfl
query "按状态查询客户"(String requiredStatus)
    $customer: Customer(status == requiredStatus)
end

query "范围内的订单"(double minAmount, double maxAmount)
    $order: Order(amount >= minAmount, amount <= maxAmount)
end

query "客户订单"(int customerId)
    $customer: Customer(id == customerId)
    $order: Order(customerId == customerId)
end
```

**C++ 中的用法：**

```cpp
// 简单查询
auto activeCustomers = session->execute_query("customers_by_status", {"Active"});

// 范围查询
auto midRangeOrders = session->execute_query("orders_in_range", {100.0, 500.0});

// 处理结果
for (auto& row : activeCustomers) {
    if (auto customer = row.get("$customer")) {
        std::cout << "找到: " << customer->fields.at("name") << std::endl;
    }
}
```

---

## 3. RHS 动作与控制流

### 3.1 基本动作

#### 插入新事实

```rfl
rule "创建 VIP 状态"
when
    $customer: Customer(balance > 10000)
    not VipStatus(customerId == $customer.id)
then
    insert VipStatus { customerId = $customer.id, level = "Gold" }
end
```

#### 更新事实

```rfl
rule "处理订单"
when
    $order: Order(status == "new")
then
    update $order { status = "processed", total = $order.total * 0.9 }
end
```

> ⚠️ **重要：`update` 会立即触发 RETE 重新评估。** 这意味着 `update` 执行后，引擎会立即检查所有规则是否需要重新触发。这是规则引擎的标准行为（和 Drools 一致），不是 bug。
>
> 如果你的规则在 `update` 后仍然匹配 LHS 条件，规则会被重复触发，可能导致无限循环。解决方法是在 LHS 中添加约束来防止重复触发：
>
> ```rfl
> // ❌ 危险：update 后规则仍然匹配，无限循环
> rule "Bad Example"
> when
>     $c: Counter(count < 10)
> then
>     update $c { count = $c.count + 1 }
> end
>
> // ✅ 安全：update 后 status 不再是 "pending"，规则不再匹配
> rule "Good Example"
> when
>     $c: Counter(status == "pending")
> then
>     update $c { status = "done", count = $c.count + 1 }
> end
> ```
>
> 这个行为对所有包含 `update` 的控制结构都适用，包括 `if`、`for`、`while`、`switch`。

#### 撤销事实

```rfl
rule "移除已完成任务"
when
    $task: Task(status == "done")
then
    retract $task
end
```

#### 逻辑插入（自动撤销）

```rfl
rule "标记高风险"
when
    $tx: Transaction(amount > 50000)
then
    insertLogical HighRiskFlag { transactionId = $tx.id }
end
```

### 3.2 控制流

#### 条件逻辑 (if / else if / else)

```rfl
rule "客户分类"
when
    $customer: Customer()
then
    if $customer.balance > 10000 {
        insert PremiumCustomer { id = $customer.id }
    } else if $customer.balance > 5000 {
        insert GoldCustomer { id = $customer.id }
    } else {
        insert StandardCustomer { id = $customer.id }
    }
end
```

#### While 循环

循环执行直到条件为 false，内置安全限制（默认最多 1000 次迭代）：

```rfl
rule "复利计算"
when
    $account: Account(status == "pending")
then
    while $account.years > 0 {
        update $account {
            balance = $account.balance * (1 + $account.rate),
            years = $account.years - 1
        }
    }
    update $account { status = "done" }
end
```

> ⚠️ 注意：`while` 循环内的 `update` 会触发 RETE 重新评估。确保 LHS 约束在 `update` 后不再匹配，防止规则被重复触发。

#### Switch / Case

根据表达式的值选择分支：

```rfl
rule "应用等级折扣"
when
    $order: Order(status == "new")
then
    switch $order.tier {
        case 1 {
            update $order { discount = 0.05 }
        }
        case 2 {
            update $order { discount = 0.10 }
        }
        case 3 {
            update $order { discount = 0.15 }
        }
        default {
            update $order { discount = 0 }
        }
    }
    update $order { status = "processed" }
end
```

#### Break / Continue

在 `for` 和 `while` 循环中使用：

```rfl
rule "处理订单项"
when
    $job: Job(status == "pending")
then
    while $job.retry < 3 {
        if $job.skipCurrentAttempt == true {
            update $job { skipCurrentAttempt = false, retry = $job.retry + 1 }
            continue
        }
        if $job.lastErrorCode == 0 {
            break
        }
        update $job { retry = $job.retry + 1 }
    }
end
```

#### For 循环 (JMESPath)

使用 LHS 的数据源模式处理 JSON 数据：

```rfl
rule "处理订单项"
when
    $item: OrderItem() from json(file("data/order_items.json"), "items[*]")
then
    insert ProcessedItem {
        orderId = $item.orderId,
        productName = $item.name,
        quantity = $item.quantity
    }
end
```

#### 其他动作

```rfl
halt                        // 停止规则引擎
setFocus("validation")      // 切换议程组
```

### 3.3 数学表达式

RHS 使用内置表达式引擎：

#### 运算符

| 类型 | 运算符 |
|------|--------|
| 数学 | `+`, `-`, `*`, `/`, `%`, `^` |
| 比较 | `==`, `!=`, `<`, `<=`, `>`, `>=` |
| 逻辑 | `&&`, `\|\|` |
| 三元 | `condition ? true_value : false_value` |

#### 内置函数

| 类别 | 函数 |
|------|------|
| 基础数学 | `abs`, `ceil`, `floor`, `round`, `trunc`, `frac`, `sgn` |
| 幂/根 | `sqrt`, `pow`, `root`, `exp`, `expm1` |
| 对数 | `log`, `log2`, `log10`, `log1p`, `logn` |
| 三角函数 | `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2` |
| 双曲函数 | `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh` |
| 比较 | `min`, `max`, `clamp`, `inrange` |
| 聚合 | `avg`, `sum`, `mul` |
| 特殊函数 | `erf`, `erfc`, `ncdf`, `hypot`, `mod` |

#### 常量

`pi`, `e`, `inf`, `epsilon`

#### 示例

```rfl
rule "价格取整"
when
    $order: Order()
then
    update $order { finalPrice = floor($order.total * 100) / 100 }
end

rule "复利计算"
when
    $account: Account()
then
    update $account { balance = $account.principal * pow(1 + $account.rate, $account.years) }
end

rule "归一化并限制分数"
when
    $result: Result()
then
    update $result {
        normalized = clamp(0, ($result.raw - $result.min) / ($result.max - $result.min) * 100, 100)
    }
end

rule "条件赋值"
when
    $order: Order()
then
    update $order {
        discount = $order.total > 1000 ? $order.total * 0.1 : 0
    }
end
```

### 3.4 Native 函数与 DLL 函数表

RHS 可以调用宿主注册的 Native 函数，也可以调用从 DLL/so 插件函数表加载的函数。

规则侧写法一致：

```rfl
rule "调用 Native"
when
    $s: Sensor(temperature > 30)
then
    invoke pluginLog($s.id, $s.temperature)
    update $s { score = pluginMetric($s.temperature, 2) }
end
```

C API 接入路径：
- 直接注册函数：`ruleforge_kb_register_native_function(...)`
- 加载 DLL/so 函数表：`ruleforge_kb_load_native_function_table(...)`

运行时流程：
1. 解析 RHS `invoke` 或赋值表达式中的函数调用。
2. 按函数名在 KnowledgeBase 的 native registry 中解析。
3. 计算参数。
4. 调用对应 native 回调。

完整示例见：
- `capi/examples/CAPI_NATIVE_DLL_ZH.md`
- `capi/examples/NATIVE_FUNCTIONS_ZH.md`

---

## 4. 高级模式

### 4.1 状态机模式

建模复杂工作流：

```rfl
declare ProcessState
    processId: String
    currentState: String
end

rule "启动流程"
when
    $request: ProcessRequest(status == "NEW")
    not ProcessState(processId == $request.id)
then
    insert ProcessState {
        processId = $request.id,
        currentState = "VALIDATION"
    }
    update $request { status = "PROCESSING" }
end

rule "验证完成"
when
    $state: ProcessState(currentState == "VALIDATION")
    $validation: ValidationResult(processId == $state.processId, valid == true)
then
    update $state { currentState = "APPROVAL" }
end

rule "流程批准"
when
    $state: ProcessState(currentState == "APPROVAL")
    $approval: ApprovalResult(processId == $state.processId, approved == true)
then
    update $state { currentState = "COMPLETE" }
end
```

### 4.2 复杂事件处理 (CEP)

跟踪跨时间模式：

```rfl
declare LoginEvent
    userId: int
    timestamp: long
    ipAddress: String
    success: boolean
end

declare SuspiciousActivity
    userId: int
    reason: String
end

rule "多次登录失败"
when
    $user: User()
    $failedLogins: Number() from accumulate(
        LoginEvent(
            userId == $user.id,
            success == false
        ),
        count()
    )
    eval($failedLogins >= 5)
    not SuspiciousActivity(userId == $user.id)
then
    insert SuspiciousActivity {
        userId = $user.id,
        reason = "multiple_failed_logins"
    }
end

rule "地理异常"
when
    $user: User()
    $login1: LoginEvent(userId == $user.id, $ip1: ipAddress)
    $login2: LoginEvent(
        userId == $user.id,
        ipAddress != $ip1,
        timestamp > $login1.timestamp
    )
    not SuspiciousActivity(userId == $user.id)
then
    insert SuspiciousActivity {
        userId = $user.id,
        reason = "geo_anomaly"
    }
end
```

### 4.3 数据验证框架

```rfl
declare ValidationError
    entityType: String
    entityId: int
    field: String
    message: String
    severity: String
end

rule "验证客户年龄"
when
    $customer: Customer(age < 18)
then
    insert ValidationError {
        entityType = "Customer",
        entityId = $customer.id,
        field = "age",
        message = "must_be_18_or_older",
        severity = "ERROR"
    }
end

rule "验证余额一致性"
when
    $customer: Customer($customerId: id, $balance: balance)
    $totalOrders: Number() from accumulate(
        Order(customerId == $customerId, status == "Completed", $amount: amount),
        sum($amount)
    )
    eval(abs($balance - $totalOrders) > 0.01)
then
    insert ValidationError {
        entityType = "Customer",
        entityId = $customer.id,
        field = "balance",
        message = "balance_mismatch",
        severity = "WARNING"
    }
end
```

---

## 5. 性能指南

### 5.1 规则设计最佳实践

#### ✅ 优先放置选择性约束

```rfl
// 好 - 最具选择性的约束优先
when
    $customer: Customer(tier == "VIP", status == "Active")

// 坏 - 选择性较低的约束优先
when
    $customer: Customer(status == "Active", tier == "VIP")
```

#### ✅ 使用适当的优先级

```rfl
rule "数据验证"
    salience 1000  // 优先运行
when
    $data: InputData()
then
    // 验证数据
end

rule "业务逻辑"
    salience 100   // 验证后运行
when
    $data: InputData(valid == true)
then
    // 处理数据
end
```

#### ✅ 最小化 `eval()` 使用

```rfl
// 好 - 本机约束
when
    $customer: Customer(balance > 1000, age >= 21)

// 避免 - eval 较慢
when
    $customer: Customer()
    eval($customer.balance > 1000 && $customer.age >= 21)
```

### 5.2 内存优化

#### 使用类型化构建器以获得更好的性能

```cpp
// 优化方法 - 使用对象池和字符串驻留
auto customer = FAST_CUSTOMER()
    .id(1001)
    .name("John Doe")
    .balance(5000.0)
    .build();

// 标准方法
auto customer = std::make_shared<Fact>();
customer->type = "Customer";
customer->fields["id"] = static_cast<int64_t>(1001);
customer->fields["name"] = "John Doe";
customer->fields["balance"] = 5000.0;
```

#### 批量操作以实现高吞吐量

```cpp
// 好 - 批量处理
std::vector<std::shared_ptr<Fact>> customers;
for (int i = 0; i < 1000; ++i) {
    customers.push_back(create_customer(i));
}
session->add_facts(customers); // 单次批量操作

// 避免 - 单独操作
for (int i = 0; i < 1000; ++i) {
    session->add_fact(create_customer(i)); // 1000 次单独操作
}
```

### 5.3 监控规则性能

```cpp
// 启用规则跟踪
session->enable_tracing(true);

// 执行规则
session->fire_all_rules();

// 获取性能报告
auto trace = session->get_execution_trace();
auto summary = session->get_rule_performance_summary();

for (auto& [rule_name, stats] : summary) {
    std::cout << rule_name << ": "
              << stats.execution_count << " 次执行, "
              << stats.total_time_ms << "ms 总计\n";
}
```

### 5.4 避免在会话中存储大型数据

`StatefulSession` 是引擎的工作内存，旨在通过 Rete 算法进行高效的模式匹配，而不是作为通用数据存储。

**最佳实践：**

- **只插入"活动"事实：** 只包含直接参与规则 `when` 条件模式匹配的事实。
- **外部存储大型数据：** 大型、复杂或不直接参与规则条件的数据，存储在外部系统中。
- **按需加载相关子集：** 仅在需要处理之前将必要的最小子集检索到会话中。

---

## 6. 故障排除

### 6.1 常见问题

#### 规则未触发

**调试步骤：**

1. 检查事实是否存在：`session->get_facts_of_type("Customer")`
2. 验证约束是否匹配
3. 检查外键关系
4. 使用 `session->enable_tracing(true)` 获取详细执行日志

#### 无限循环

`update` 会触发 RETE 重新评估。如果规则在 `update` 后仍然匹配 LHS 条件，规则会被重复触发。

```rfl
// ❌ 无限循环
rule "Bad"
when
    $c: Counter(count < 10)
then
    update $c { count = $c.count + 1 }
end

// ✅ 安全
rule "Good"
when
    $c: Counter(status == "pending")
then
    update $c { status = "done", count = $c.count + 1 }
end
```

#### 内存问题

```cpp
// 监控内存使用
auto stats = PoolStatsCollector::collect();
std::cout << PoolStatsCollector::format_stats(stats) << std::endl;

// 定期清除未使用的事实
session->retract_facts_of_type("TemporaryData");

// 对高频操作使用对象池
auto pooled_fact = GlobalPools::make_pooled_fact();
```

### 6.2 性能调试

#### 识别慢规则

```cpp
session->enable_tracing(true);
auto start = std::chrono::high_resolution_clock::now();

session->fire_all_rules();

auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

std::cout << "规则执行耗时: " << duration.count() << "ms\n";

// 分析每条规则的性能
auto summary = session->get_rule_performance_summary();
for (auto& [rule, stats] : summary) {
    if (stats.average_time_ms > 10.0) {
        std::cout << "慢规则: " << rule << " - " << stats.average_time_ms << "ms 平均\n";
    }
}
```

---

## 下一步

- **[部署指南](DEPLOYMENT.md)** - 生产部署、监控

---

*"好的程序员关心数据结构及其关系。坏程序员关心代码。"* - Linus Torvalds
