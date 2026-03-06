# RulesForge 规则引擎 - 绝对初学者指南

*从未用过规则引擎？从这里开始。*

## 什么是规则引擎？

想象一下，您经营一家在线商店，并有以下业务规则：

- "消费超过 1000 美元的客户成为 VIP 会员"
- "向新客户发送欢迎邮件"
- "如果购物车中有 5 件以上商品，则享受 10% 折扣"

与其将这些规则硬编码到分散在应用程序中的 if 语句中，**规则引擎** 允许您以简单、可读的格式编写它们并高效执行。

## 为什么要使用 RulesForge？

```cpp
// 没有 RulesForge - 分散的业务逻辑
if (customer.balance > 1000 && customer.orders.size() > 5) {
    customer.status = "VIP";
    send_notification(customer, "Welcome to VIP!");
    apply_discount(customer, 0.15);
}

// 在代码的其他地方检查...
if (customer.age < 18) {
    reject_order(order, "Age verification required");
}

// 在其他地方...
if (customer.failed_logins > 3) {
    lock_account(customer);
}
```

```rfl
// 使用 RulesForge - 所有业务逻辑集中在一处
rule "Promote to VIP"
when
    $c: Customer(balance > 1000, orderCount > 5)
    not VipCustomer(customerId == $c.id)
then
    update $c { status = "VIP" }
    insert Notification { customerId = $c.id, message = "Welcome to VIP!" }
end

rule "Age Verification"
when
    $o: Order()
    $c: Customer(id == $o.customerId, age < 18)
then
    insert OrderRejection { orderId = $o.id, reason = "Age verification required" }
end

rule "Account Security"
when
    $c: Customer(failedLogins > 3, accountLocked == false)
then
    update $c { accountLocked = true }
end
```

## 您的第一个 10 分钟示例

### 步骤 1: 安装和构建

```bash
# 克隆项目
git clone <您的仓库 URL>
cd rulesforge

# 构建 (需要 CMake 和 C++20)
cmake --preset=default
cmake --build build
```

### 步骤 2: 创建您的第一个规则文件

创建 `my_first_rules.rfl`：

```rfl
// 告诉系统数据是什么样的
declare Person
    name: String
    age: int
    hasLicense: boolean
end

declare CanDrive
    name: String
    reason: String
end

// 实际的业务规则
rule "Can Drive Check"
when
    // 找到一个 16 岁或以上且有驾照的人
    $person: Person(age >= 16, hasLicense == true)

    // 确保我们尚未处理过他们
    not CanDrive(name == $person.name)
then
    // 规则匹配时执行的原生动作
    insert CanDrive {
        name = $person.name,
        reason = "eligible"
    }
end
```

### 步骤 3: 在 C++ 代码中使用它

创建 `test_driving.cpp`：

```cpp
#include "knowledge_base.hpp"
#include "stateful_session.hpp"
#include <iostream>
#include <fstream>

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main() {
    // 1. 加载规则
    std::string rules = read_file("my_first_rules.rfl");
    ParsingResult result;
    auto knowledge_base = build_knowledge_base(rules, result);

    if (!result.success) {
        std::cerr << "规则编译失败！" << std::endl;
        return 1;
    }

    // 2. 创建一个会话 (将其视为您的工作区)
    auto session = knowledge_base->create_session();

    // 3. 添加一些人
    auto person1 = std::make_shared<Fact>();
    person1->type = "Person";
    person1->fields["name"] = "Alice";
    person1->fields["age"] = static_cast<int64_t>(17);
    person1->fields["hasLicense"] = true;
    session->add_fact(person1);

    auto person2 = std::make_shared<Fact>();
    person2->type = "Person";
    person2->fields["name"] = "Bob";
    person2->fields["age"] = static_cast<int64_t>(15);
    person2->fields["hasLicense"] = true;
    session->add_fact(person2);

    // 4. 运行规则！
    int rules_fired = session->fire_all_rules();
    std::cout << "执行了 " << rules_fired << " 条规则" << std::endl;

    // 5. 检查发生了什么
    auto drivers = session->get_facts_of_type("CanDrive");
    std::cout << "找到了 " << drivers.size() << " 个可以开车的人" << std::endl;

    return 0;
}
```

### 步骤 4: 编译和运行

```bash
# 编译 (根据需要调整路径)
g++ -std=c++20 -I./rulesforge/include -L./build/lib test_driving.cpp -lruleforge -o test_driving

# 运行
./test_driving
```

**预期输出：**

```
执行了 1 条规则
找到了 1 个可以开车的人
```

Bob 不符合条件，因为他只有 15 岁！

## 关键概念简单解释

### 事实 = 您的数据

将事实视为数据库表中的行：

```cpp
// 这是一个"Person"事实
auto person = std::make_shared<Fact>();
person->type = "Person";
person->fields["name"] = "Alice";
person->fields["age"] = static_cast<int64_t>(17);
person->fields["hasLicense"] = true;
```

### 规则 = 您的业务逻辑

规则有两部分：

- **WHEN** (条件)：要查找什么模式
- **THEN** (动作)：找到后做什么

```rfl
rule "规则名称"
when
    // 条件：当此模式匹配时...
then
    // 动作：执行此操作！
end
```

### 会话 = 您的工作区

- 将事实添加到会话
- 对这些事实运行规则
- 规则可以创建新事实或修改现有事实

```cpp
session->add_fact(my_data);     // 放入数据
session->fire_all_rules();      // 处理它
auto results = session->get_facts_of_type("Result"); // 获取结果
```

## 常见初学者模式

### 模式 1: 简单过滤

"查找所有成年人"

```rfl
declare Adult
    name: String
end

rule "查找成年人"
when
    $person: Person(age >= 18)
    not Adult(name == $person.name)
then
    insert Adult { name = $person.name }
end
```

### 模式 2: 验证

"检查数据是否有效"

```rfl
declare ValidationError
    field: String
    message: String
end

rule "验证年龄"
when
    $person: Person(age < 0)
then
    insert ValidationError {
        field = "age",
        message = "年龄不能为负数"
    }
end
```

### 模式 3: 计算

"计算派生值"

```rfl
declare CreditScore
    name: String
    score: double
end

rule "计算信用评分"
when
    $person: Person(age > 0, income > 0)
    not CreditScore(name == $person.name)
then
    insert CreditScore {
        name = $person.name,
        score = min(850, $person.income / 1000 + $person.age * 10)
    }
end
```

## 下一步

一旦您熟悉了基础知识：

1. **尝试更多示例**：查看 `/rulesforge/example/` 目录
2. **学习更多模式**：查看 [快速入门指南](QUICKSTART.md)
3. **高级功能**：转到 [用户指南](USER_GUIDE.md)
4. **实际项目**：查看 [专业指南](PROFESSIONAL_GUIDE.md)

## 常见初学者错误

### ❌ 忘记声明类型

```rfl
// 错误 - 没有声明
rule "Bad Rule"
when
    $p: Person(age > 18)  // Person 是什么？
```

```rfl
// 正确 - 先声明
declare Person
    age: int
end

rule "Good Rule"
when
    $p: Person(age > 18)
```

### ❌ 错误的字段类型

```rfl
declare Person
    age: String  // 错误 - age 应该是 int
end
```

### ❌ 条件中使用 &&

```rfl
rule "Wrong"
when
    $p: Person(age >= 18 && income > 1000)  // && 在这里不起作用
then
    // ...
end
```

```rfl
rule "Correct"
when
    $p: Person(age >= 18, income > 1000)  // 使用逗号分隔约束
then
    // ...
end
```

## 故障排除

### "规则不触发"

1. 检查您的事实是否与 `declare` 语句完全匹配
2. 验证字段类型 (int vs String vs boolean)
3. 启用跟踪：`session->enable_tracing(true)`

### "编译错误"

1. 确保每个 `declare` 块都有匹配的字段类型
2. 检查字段名称中的拼写错误
3. 确保 RFL 语法正确 (约束之间用逗号分隔)

### "无限循环"

1. `update` 会触发 RETE 重新评估 — 确保更新后 LHS 不再匹配
2. 添加守卫约束（如 `status == "pending"`）并在 RHS 中修改它

## 帮助和资源

- 📖 **更多示例**：`/rulesforge/example/` 目录
- 🐛 **问题**：在 GitHub 上报告错误
- 💡 **问题**：首先检查现有文档
- ⚡ **性能**：稍后再担心这个 - 先让它工作起来！

---

*准备好升级了吗？查看 [快速入门指南](QUICKSTART.md) 获取更真实的示例！*
