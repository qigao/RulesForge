# Drills 规则引擎 - 快速入门指南

*15 分钟内即可上手*

## 什么是 Drills？

Drills 是一个高性能的 C++ 规则引擎，实现了 **Rete 算法** 并集成了 **JavaScript**。可以将其理解为“业务逻辑即代码”——用声明性语言编写复杂的条件，用 JavaScript 执行动作。

## 5 分钟示例

### 1. 编写您的业务规则

创建 `my_rules.rfl`：
```rfl
// 定义您的数据模型
declare Customer
    id: int
    name: String
    age: int
    balance: double
    status: String
end

declare VipCustomer
    customerId: int
    reason: String
end

// 业务规则：高价值客户成为 VIP
rule "Promote to VIP"
salience 10
when
    $c: Customer(balance > 10000, status == "Active")
    not VipCustomer(customerId == $c.id)
then
    // JavaScript 动作 - 'c' 是匹配的客户
    console.log(`将 ${c.name} 提升为 VIP 状态`);
    rfl.insert({
        type: "VipCustomer", 
        customerId: c.id,
        reason: "高余额: $" + c.balance
    });
end

// 查询所有 VIP 客户
query "find_vip_customers"
    $vip: VipCustomer() 
    $customer: Customer(id == $vip.customerId)
end
```

### 2. 在您的 C++ 应用程序中使用

```cpp
#include "knowledge_base.hpp"
#include "stateful_session.hpp"
#include "fact_builder.hpp"

int main() {
    // 加载并编译规则
    std::string rfl = read_file("my_rules.rfl");
    ParsingResult result;
    auto kb = build_knowledge_base(rfl, result);
    
    if (!result.success) {
        for (auto& err : result.errors) {
            std::cerr << err.to_string() << std::endl;
        }
        return 1;
    }

    // 创建会话并添加事实
    auto session = kb->create_session();
    
    // 使用类型安全构建器添加客户数据
    auto customer = CUSTOMER()
        .id(1001)
        .name("Alice Johnson")
        .age(35)
        .balance(15000.0)
        .status("Active")
        .build();
        
    session->add_fact(customer);
    
    // 触发规则，见证奇迹
    int rules_fired = session->fire_all_rules();
    std::cout << "触发了 " << rules_fired << " 条规则\n";
    
    // 查询结果
    auto vips = session->execute_query("find_vip_customers");
    std::cout << "找到了 " << vips.size() << " 位 VIP 客户\n";
    
    return 0;
}
```

### 3. 预期输出
```
将 Alice Johnson 提升为 VIP 状态
触发了 1 条规则
找到了 1 位 VIP 客户
```

## 2 分钟内掌握关键概念

### 事实 = 您的数据
```cpp
// 传统方法 - 手动创建事实
auto fact = std::make_shared<Fact>();
fact->type = "Customer";
fact->fields["name"] = "John";
fact->fields["balance"] = 5000.0;

// Drills 方法 - 类型安全构建器
auto customer = CUSTOMER()
    .name("John")
    .balance(5000.0)
    .build();
```

### 规则 = 您的业务逻辑
```rfl
rule "规则名称"
when
    // 条件 - 匹配什么数据模式？
    $customer: Customer(balance > 1000, status == "Active")
then
    // 动作 - 匹配后做什么？
    console.log("高价值客户: " + customer.name);
    rfl.insert({type: "HighValueCustomer", id: customer.id});
end
```

### 会话 = 您的工作内存
```cpp
auto session = kb->create_session();
session->add_fact(customer_data);      // 添加数据
int fired = session->fire_all_rules(); // 处理规则
auto results = session->execute_query("my_query"); // 查询结果
```

## 常见模式

### 模式 1: 数据验证
```rfl
rule "验证客户"
when
    $c: Customer(age < 18)
then
    console.error(`客户 ${c.name} 未成年: ${c.age}`);
    rfl.insert({type: "ValidationError", message: "客户必须年满 18 岁"});
end
```

### 模式 2: 数据转换
```rfl
rule "计算信用评分"
when
    $c: Customer(balance > 0)
    not CreditScore(customerId == $c.id)
then
    let score = Math.min(850, Math.max(300, c.balance / 100 + 600));
    rfl.insert({
        type: "CreditScore", 
        customerId: c.id, 
        score: score
    });
end
```

### 模式 3: 复杂条件
```rfl
rule "忠诚客户奖励"
when
    $c: Customer(status == "Active")
    $orders: Number() from accumulate(
        Order(customerId == $c.id, amount > 100),
        count()
    )
    eval($orders >= 5)
then
    console.log(`${c.name} 有 ${orders} 笔大订单 - 奖励时间！`);
    rfl.insert({type: "Reward", customerId: c.id, points: 1000});
end
```

## 构建和运行

### 先决条件
- CMake 3.20+
- C++20 编译器 (MSVC 2022, GCC 11+, Clang 13+)
- vcpkg (用于依赖项)

### 快速构建
```bash
git clone <您的仓库>
cd drills
cmake --preset=default
cmake --build build
```

### 运行示例
```bash
# 基本示例
./build/bin/drills_engine examples/basic.rfl

# 性能演示
./build/bin/memory_optimization_demo
```

## 下一步？

-   **简单规则？** → 继续阅读 [用户指南](USER_GUIDE.md)
-   **生产部署？** → 查看 [部署指南](DEPLOYMENT.md)

## 需要帮助？

-   📖 **文档**：所有指南都在 `/docs/` 中
-   🐛 **问题**：在 GitHub Issues 中报告错误
-   💡 **示例**：查看 `/drills/example/` 目录
-   ⚡ **性能**：查看内存优化示例

---

*使用现代 C++20、QuickJS 和 Rete 算法构建，充满 ❤️*
