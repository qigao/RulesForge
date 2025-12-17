# Drills 规则引擎 - 完整用户指南

*掌握声明式业务逻辑的艺术*

## 目录

1.  [**核心概念**](#1-核心概念)
2.  [**DRL 语言参考**](#2-drl-语言参考)
3.  [**JavaScript 集成**](#3-javascript-集成)
4.  [**高级模式**](#4-高级模式)
5.  [**性能指南**](#5-性能指南)
6.  [**故障排除**](#6-故障排除)
7.  [**部署指南**](#7-部署指南)

---

## 1. 核心概念

### Rete 算法实践

Drills 实现了 **Rete 算法**，这是一种强大的模式匹配技术，具有以下特点：

-   ✅ **增量处理** - 只重新计算已更改的部分
-   ✅ **内存网络** - 存储中间结果以提高速度
-   ✅ **冲突解决** - 智能处理多个规则匹配
-   ✅ **真值维护** - 当前提条件改变时自动撤销派生事实

```drl
rule "价格警报"
when
    $product: Product(price < 100)
    $user: User(interests contains $product.category)
then
    // 仅当两个条件都满足时才触发
    // 如果价格超过 100，则自动撤销警报
    drools.insert({
        type: "PriceAlert",
        userId: user.id,
        productId: product.id
    });
end
```

### 工作内存 vs 知识库

```cpp
// 知识库 = 不可变编译规则 (线程安全)
auto kb = build_knowledge_base(drl_source, result);

// 会话 = 可变工作内存 (每个线程一个)
auto session1 = kb->create_session(); // 线程 1
auto session2 = kb->create_session(); // 线程 2

// 多个会话可以共享同一个知识库
session1->add_fact(customer1);
session2->add_fact(customer2); // 独立数据
```

### 数据流：事实、规则和引擎

理解数据如何在 Drills 引擎中流动至关重要。这是一个 **事实**（您的输入数据）与 **规则**（您定义的逻辑）在引擎的 **工作内存** 中持续交互的循环。

1.  **规则被摄取到知识库中：**
    *   您的规则，无论是通过 DRL 文件、决策表（如 CSV）还是其他格式定义，首先被解析并编译成优化的内部表示，主要是 Rete 网络。
    *   这个编译后的规则集存储在 `KnowledgeBase` 中。`KnowledgeBase` 是不可变的且线程安全的，充当您业务逻辑的蓝图。

2.  **事实被插入到工作内存中：**
    *   您的应用程序数据，被称为“事实”，是表示系统当前状态的对象（例如 C++ 中的 `Fact` 实例）。
    *   这些事实被插入到 `StatefulSession`（引擎的工作内存）中。每个会话都是可变的，并且通常与单个线程或事务绑定。

3.  **引擎执行和模式匹配：**
    *   一旦事实进入 `StatefulSession`，Rete 算法会根据从 `KnowledgeBase` 加载的规则持续评估它们。
    *   当一个事实（或事实的组合）匹配规则的条件（LHS - 左侧）时，该规则被激活。

4.  **规则动作和数据修改：**
    *   激活的规则执行其动作（RHS - 右侧），这些动作通常是 JavaScript 代码。这些动作可以：
        *   **修改现有事实**：更改工作内存中已有事实的属性。
        *   **插入新事实**：向工作内存中添加新事实，可能触发其他规则。
        *   **撤销事实**：从工作内存中移除事实。
        *   **触发外部效应**：通过回调或外部 API 与您的应用程序交互（例如，日志记录、发送通知、更新数据库）。

5.  **查询结果：**
    *   在规则触发并且工作内存达到稳定状态后，您可以查询 `StatefulSession` 以检索特定事实或规则执行的结果。

**本质上：** 规则被编译一次到 `KnowledgeBase` 中。事实被动态插入到 `StatefulSession` 中。然后引擎持续将事实与规则匹配，执行可以修改事实本身或触发外部效应的动作，最后，您查询会话以获取结果。

---

## 2. DRL 语言参考

### 2.1 文件结构

每个 DRL 文件都遵循以下结构：

```drl
package com.example.business.rules

import com.example.model.Customer
import com.example.util.DateUtils

global java.util.List notifications

declare CustomEvent
    timestamp: long
    userId: int
    action: String
end

function calculateScore(balance, age) {
    return balance / age * 10;
}

query "active_customers"
    $c: Customer(status == "Active")
end

rule "业务规则 1"
// 属性
when
    // 条件
then
    // 动作
end

rule "业务规则 2"
// 更多规则...
```

### 2.2 类型声明

定义您的数据模式：

```drl
declare Customer
    id: int                    // 必填字段
    name: String              // 字符串类型
    balance: double           // 数字类型
    active: boolean           // 布尔值
    registrationDate: long    // Unix 时间戳
    tags: String[]            // 数组 (未完全实现)
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

```drl
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
        // 右侧 (RHS) - JavaScript 动作
        console.log(`处理客户: ${customer.name}`);

        drools.insert({
            type: "VipStatus",
            customerId: customer.id,
            level: "Gold",
            expires: Date.now() + 365 * 24 * 60 * 60 * 1000
        });
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
```drl
$customer: Customer(balance > 1000)
```

#### 多个约束
```drl
$order: Order(
    amount > 100,
    status == "Completed",
    customerId == $customer.id
)
```

#### 嵌套字段访问
```drl
$user: User(profile.preferences.newsletter == true)
```

#### 变量绑定
```drl
$customer: Customer($customerId: id, balance > 1000)
$orders: Order(customerId == $customerId)
```

### 2.6 高级模式

#### Accumulate - 数据聚合
```drl
rule "高价值客户"
when
    $customer: Customer()
    $totalSpent: Number() from accumulate(
        Order(customerId == $customer.id, $amount: amount),
        sum($amount)
    )
    eval($totalSpent > 10000)
then
    console.log(`${customer.name} 消费了 $${totalSpent}`);
end
```

**Accumulate 函数：**
- `count()` - 计数匹配项
- `sum($field)` - 求和数字字段
- `min($field)` - 最小值
- `max($field)` - 最大值
- `average($field)` - 平均值

#### Collect - 收集事实
```drl
rule "捆绑订单"
when
    $customer: Customer()
    $orders: List() from collect(
        Order(customerId == $customer.id)
    )
    eval($orders.size() >= 3)
then
    console.log(`客户 ${customer.name} 有 ${orders.length} 笔待处理订单`);
    // 捆绑它们以获得折扣
end
```

#### Forall - 全称量词
```drl
rule "所有订单已完成"
when
    $customer: Customer()
    forall(
        $order: Order(customerId == $customer.id)
        Order(this == $order, status == "Completed")
    )
then
    console.log(`客户 ${customer.name} 的所有订单都已完成`);
end
```

### 2.7 查询

用于数据检索的参数化查询：

```drl
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

## 3. JavaScript 集成

规则的 RHS（then-block）使用 **QuickJS** - 一个快速、轻量级的 JavaScript 引擎。

### 3.1 可用 API

#### drools 对象
```javascript
// 事实操作
drools.insert({type: "NewFact", field: "value"});
drools.retract(existingFact);
drools.update(existingFact, {field: "newValue"});

// 逻辑断言 (当规则不再匹配时自动撤销)
drools.insertLogical({type: "DerivedFact", source: customer.id});

// 工作内存查询
let facts = drools.getFactsOfType("Customer");
let fact = drools.getFactById(123);
```

#### 控制台日志
```javascript
console.log("信息消息");
console.warn("警告消息");
console.error("错误消息");
```

#### 标准 JavaScript
```javascript
// 数学运算
let score = Math.max(0, Math.min(100, balance / 1000));

// 日期运算
let now = Date.now();
let expire = now + (30 * 24 * 60 * 60 * 1000); // 30 天

// 字符串运算
let message = `客户 ${customer.name} 的余额为 $${customer.balance}`;

// JSON 运算
let config = JSON.parse(customer.preferences);
```

### 3.2 变量绑定

DRL 变量变为 JavaScript 对象：

```drl
rule "示例"
when
    $customer: Customer($name: name, $balance: balance)
    $order: Order(customerId == $customer.id, $amount: amount)
then
    // DRL $customer 变为 JS customer
    console.log(`客户: ${customer.name}`);

    // 字段绑定变为 JS 变量
    console.log(`姓名: ${name}, 余额: ${balance}`);
    console.log(`订单金额: ${amount}`);

    // 访问嵌套字段
    if (customer.profile && customer.profile.email) {
        console.log(`电子邮件: ${customer.profile.email}`);
    }
end
```

### 3.3 复杂 JavaScript 动作

```drl
rule "复杂业务逻辑"
when
    $customer: Customer()
    $orders: List() from collect(Order(customerId == $customer.id))
then
    // 计算指标
    let totalAmount = 0;
    let orderCount = orders.length;

    for (let order of orders) {
        totalAmount += order.amount;

        // 检查模式
        if (order.category === "Premium" && order.amount > 1000) {
            drools.insert({
                type: "PremiumPurchase",
                customerId: customer.id,
                orderId: order.id,
                amount: order.amount
            });
        }
    }

    // 确定客户等级
    let tier = "Bronze";
    if (totalAmount > 10000) tier = "Gold";
    else if (totalAmount > 5000) tier = "Silver";

    // 更新客户
    drools.update(customer, {
        totalSpent: totalAmount,
        orderCount: orderCount,
        tier: tier,
        lastUpdated: Date.now()
    });

    // 发送通知
    if (tier !== customer.tier) {
        drools.insert({
            type: "TierChangeNotification",
            customerId: customer.id,
            oldTier: customer.tier,
            newTier: tier
        });
    }
end
```

---

## 4. 高级模式

### 4.1 状态机模式

建模复杂工作流：

```drl
declare ProcessState
    processId: String
    currentState: String
    data: Object
end

rule "启动流程"
when
    $request: ProcessRequest(status == "NEW")
    not ProcessState(processId == $request.id)
then
    drools.insert({
        type: "ProcessState",
        processId: request.id,
        currentState: "VALIDATION",
        data: {startTime: Date.now()}
    });

    drools.update(request, {status: "PROCESSING"});
end

rule "验证完成"
when
    $state: ProcessState(currentState == "VALIDATION")
    $validation: ValidationResult(processId == $state.processId, valid == true)
then
    drools.update(state, {
        currentState: "APPROVAL",
        data: {...state.data, validatedAt: Date.now()}
    });
end

rule "流程批准"
when
    $state: ProcessState(currentState == "APPROVAL")
    $approval: ApprovalResult(processId == $state.processId, approved == true)
then
    drools.update(state, {
        currentState: "COMPLETE",
        data: {...state.data, completedAt: Date.now()}
    });
end
```

### 4.2 复杂事件处理 (CEP)

跟踪跨时间模式：

```drl
declare LoginEvent
    userId: int
    timestamp: long
    ipAddress: String
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
            success == false,
            timestamp > (Date.now() - 300000) // 过去 5 分钟
        ),
        count()
    )
    eval($failedLogins >= 5)
    not SuspiciousActivity(userId == $user.id)
then
    drools.insert({
        type: "SuspiciousActivity",
        userId: user.id,
        reason: `${failedLogins} 次登录失败，在 5 分钟内`
    });

    console.warn(`安全警报: 用户 ${user.id} 有 ${failedLogins} 次登录失败`);
end

rule "地理异常"
when
    $user: User()
    $login1: LoginEvent(userId == $user.id, $ip1: ipAddress)
    $login2: LoginEvent(
        userId == $user.id,
        ipAddress != $ip1,
        timestamp > $login1.timestamp,
        timestamp < ($login1.timestamp + 3600000) // 1 小时内
    )
then
    // 检查 IP 是否来自不同国家
    let country1 = geoLookup(login1.ipAddress);
    let country2 = geoLookup(login2.ipAddress);

    if (country1 !== country2) {
        drools.insert({
            type: "SuspiciousActivity",
            userId: user.id,
            reason: "1 小时内来自 ${country1} 和 ${country2} 的登录"
        });
    }
end
```

### 4.3 数据验证框架

```drl
declare ValidationError
    entityType: String
    entityId: int
    field: String
    message: String
    severity: String
end

rule "验证客户电子邮件"
when
    $customer: Customer($email: email)
    eval($email === null || $email === "" || !$email.includes("@"))
then
    drools.insert({
        type: "ValidationError",
        entityType: "Customer",
        entityId: customer.id,
        field: "email",
        message: "电子邮件地址是必需的且必须有效",
        severity: "ERROR"
    });
end

rule "验证客户年龄"
when
    $customer: Customer(age < 18)
then
    drools.insert({
        type: "ValidationError",
        entityType: "Customer",
        entityId: customer.id,
        field: "age",
        message: "客户必须年满 18 岁",
        severity: "ERROR"
    });
end

rule "验证余额一致性"
when
    $customer: Customer($customerId: id, $balance: balance)
    $totalOrders: Number() from accumulate(
        Order(customerId == $customerId, status == "Completed", $amount: amount),
        sum($amount)
    )
    eval(Math.abs($balance - $totalOrders) > 0.01) // 考虑四舍五入
then
    drools.insert({
        type: "ValidationError",
        entityType: "Customer",
        entityId: customer.id,
        field: "balance",
        message: `余额不匹配: ${balance} vs 计算出的 ${totalOrders}`,
        severity: "WARNING"
    });
end
```

---

## 5. 性能指南

### 5.1 规则设计最佳实践

#### ✅ 优先放置选择性约束
```drl
// 好 - 最具选择性的约束优先
when
    $customer: Customer(tier == "VIP", status == "Active")

// 坏 - 选择性较低的约束优先
when
    $customer: Customer(status == "Active", tier == "VIP")
```

#### ✅ 使用适当的优先级
```drl
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
```drl
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

### 5.4 避免在会话中存储大型或复杂数据

`StatefulSession` 是引擎的工作内存，旨在通过 Rete 算法进行高效的模式匹配，而不是作为通用数据存储。插入大量数据或不积极参与规则匹配的复杂、被动对象会严重降低性能并导致过多的内存消耗。

**为什么这是一个坏主意：**

*   **内存膨胀：** 插入的每个事实都会消耗内存。大型事实或大量事实会迅速耗尽可用内存。
*   **性能下降：** Rete 网络是增量的。事实的每次插入、修改或撤销都会触发网络中大量的重新评估。更多的数据意味着呈指数级增长的处理开销，导致 `fire_all_rules()` 调用缓慢。
*   **不必要的复杂性：** 将会话视为数据库会增加管理数据生命周期和过滤规则中不相关数据的复杂性。

**最佳实践：**

*   **只插入“活动”事实：** `StatefulSession` 应只包含直接参与规则 `when` 条件模式匹配的事实。
*   **外部存储大型/复杂数据：** 对于大型、复杂或不直接参与规则条件的数据，将其存储在外部系统（例如数据库、缓存）中。
*   **按需加载相关子集：** 当规则需要访问这些外部数据时，仅在需要处理之前将必要的、最小的子集检索到会话中（例如，通过规则动作、全局变量或外部服务调用）。

**本质上：** 规则引擎处理逻辑，它不存储应用程序的整个数据集。尊重引擎的设计以确保最佳性能和可维护性。

---

## 6. 故障排除

### 6.1 常见问题

#### 规则未触发
```drl
rule "调试规则"
when
    $customer: Customer(balance > 1000)
    $order: Order(customerId == $customer.id)
then
    console.log("规则为客户触发: " + customer.name);
end
```

**调试步骤：**
1.  检查两个事实是否存在：`session->get_facts_of_type("Customer")`
2.  验证约束是否匹配：`customer.balance > 1000`
3.  检查外键关系：`order.customerId == customer.id`
4.  使用 `session->enable_tracing(true)` 获取详细执行日志

#### JavaScript 运行时错误
```javascript
// 在 RHS 中添加错误处理
then
    try {
        let result = complexCalculation(customer.data);
        drools.insert({type: "Result", value: result});
    } catch (error) {
        console.error("计算失败: " + error.message);
        drools.insert({
            type: "ProcessingError",
            entityId: customer.id,
            error: error.message
        });
    }
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
    if (stats.average_time_ms > 10.0) { // 标记慢规则
        std::cout << "慢规则: " << rule << " - " << stats.average_time_ms << "ms 平均\n";
    }
}
```

#### 优化网络传播
```cpp
// 使用事实类型批处理以提高 Rete 网络效率
std::vector<std::shared_ptr<Fact>> customers = load_customers();
std::vector<std::shared_ptr<Fact>> orders = load_orders();

// 按类型添加以最小化网络传播
session->add_facts(customers);
session->add_facts(orders);

// 优于混合添加
```

---

## 下一步

准备好学习高级主题了吗？

-   **[部署指南](DEPLOYMENT.md)** - 生产部署、监控

---

*"好的程序员关心数据结构及其关系。坏程序员关心代码。"* - Linus Torvalds

Drills 引擎围绕优雅的数据结构构建，使复杂的业务逻辑易于表达且执行速度极快。

```
