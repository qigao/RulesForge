# RulesForge - 高性能 C++ Rete 规则引擎

[English Documentation](../README.md)

这是一个高性能的 C++ 规则引擎，实现了 **Rete 算法** 并使用 **Native RHS** 动作语法，灵感来源于 Java RulesForge，但专门为 C++ 环境设计。

## 核心架构

**Rete 算法实现**:

- 完整的 Rete 网络，包含 alpha/beta 节点
- 包含已编译规则的不可变 `KnowledgeBase`
- 管理工作内存的可变 `StatefulSession`

**Native RHS 动作语法**:

- 内置动作：`insert`、`update`、`retract`、`halt`
- 控制流：`if/else if/else`、`while`、`switch/case`、`for` + JMESPath
- 数学表达式：内置表达式引擎支持（算术、三角函数、统计函数等）
- 循环控制：`break`、`continue`

## 主要特性

1. **RFL 语言**: 类似 Rules Forge Language 的语法
2. **高级条件逻辑**: 支持 `not`、`exists`、`forall` 模式
3. **数据聚合**: 内置累加器 (`sum`、`count`、`average` 等)
4. **真值维护系统 (TMS)**: 带有自动依赖跟踪的逻辑断言
5. **复杂事件处理**: 用于 CEP 场景的时间操作符
6. **JMESPath 集成**: 通过 `for` 循环查询和迭代 JSON 数据
7. **表达式引擎**: 完整的数学函数和表达式支持

## 技术栈

- **C++20** 标准，采用现代实践
- **jsoncons** 用于 JMESPath
- **CMake** 构建系统，带有 vcpkg 依赖管理

## 项目结构

```
rulesforge/
├── include/           # 头文件 (AST, 解析器, Rete 节点)
├── src/              # 实现文件
├── test/             # 全面测试套件
├── docs/             # 文档
├── examples/         # 使用示例
├── dsl.md           # 语法规范
├── guide.md         # 用户指南
└── readme.md        # 快速启动指南
```

## 快速启动

### 1. 在 RFL 文件中编写您的规则

**`my_rules.rfl`**

```rfl
// 定义事实的数据模型
declare Person
    name: String
    age: int
end

declare CanVote
    name: String
end

// 查找成年人并断言他们可以投票的规则
rule "Identify Voters"
when
    $p: Person(age >= 18)
    not CanVote(name == $p.name)
then
    insert CanVote { name = $p.name }
end

// 查找所有可以投票的人的查询
query "find_voters"
    $cv: CanVote()
end
```

### 2. 在您的 C++ 应用程序中使用引擎

```cpp
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"
#include "query_result.hpp"
#include "errors.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main() {
    // 1. 将 RFL 文件加载并编译到 KnowledgeBase 中
    std::string rfl_content = read_file("my_rules.rfl");
    ParsingResult result;
    std::shared_ptr<KnowledgeBase> kb = build_knowledge_base(rfl_content, result);

    if (!result.success) {
        for (const auto& err : result.errors) {
            std::cerr << err.to_string() << std::endl;
        }
        return 1;
    }

    // 2. 从 KnowledgeBase 创建一个有状态会话
    std::unique_ptr<StatefulSession> session = kb->create_session();

    // 3. 将事实添加到会话的工作内存中
    auto person1 = std::make_shared<Fact>();
    person1->type = "Person";
    person1->fields["name"] = "Alice";
    person1->fields["age"] = (int64_t)30;
    session->add_fact(person1);

    auto person2 = std::make_shared<Fact>();
    person2->type = "Person";
    person2->fields["name"] = "Bob";
    person2->fields["age"] = (int64_t)16;
    session->add_fact(person2);

    // 4. 触发规则
    int fired_count = session->fire_all_rules();
    std::cout << "\n触发了 " << fired_count << " 条规则。\n";

    // 5. 查询结果
    QueryResult query_results = session->execute_query("find_voters");
    std::cout << "找到了 " << query_results.size() << " 个可以投票的人。\n";
    for (const auto& row : query_results) {
        auto name = row.getFieldAs<std::string>("$cv", "name");
        if (name) {
            std::cout << " - " << *name << std::endl;
        }
    }

    return 0;
}
```

## 优势

- **面向性能**: Rete 算法针对高吞吐量场景进行了优化
- **清晰分离**: 不可变知识库与可变会话
- **全面**: 包含 TMS、CEP、聚合等完整功能集
- **零外部脚本依赖**: Native RHS 无需 JavaScript 运行时
- **现代 C++**: 利用 C++20 特性和最佳实践
- **测试**: 彻底的测试覆盖

## 文档

- [RFL 语言指南](./docs/USER_GUIDE.md)
- [RFL 语法指南](./docs/dsl.md)
