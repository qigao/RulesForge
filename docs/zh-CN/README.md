# Drills - 带有 JavaScript 集成的 C++ Rete 规则引擎

[English Documentation](../README.md)

这是一个复杂的、高性能的 C++ 规则引擎，实现了 **Rete 算法** 并带有 **JavaScript (QuickJS) 脚本** 用于规则后果，灵感来源于 Java RuleForge，但专门为 C++ 环境设计。

## 核心架构

**Rete 算法实现**:
- 完整的 Rete 网络，包含 alpha/beta 节点 (`drills/include/rete/rete_node.hpp:15175 行`)
- 包含已编译规则的不可变 `KnowledgeBase` (`drills/include/knowledge_base.hpp:74`)
- 管理工作内存的可变 `StatefulSession` (`drills/include/stateful_session.hpp:141`)

**JavaScript 集成**:
- `JSScriptingManager` 连接 C++ 和 JavaScript (`drills/src/rfl_js_manager.cpp`)
- QuickJS 库用于无缝的 C++/JavaScript 绑定 (`vcpkg.json:90-92`)
- 暴露给 JavaScript 的自定义 `rfl` API，用于事实操作

## 主要特性

1.  **RFL 语言**: 类似 Rules Forge Language 的语法，带有全面的语法 (`drills/dsl.md`, `drills/guide.md`)
2.  **高级条件逻辑**: 支持 `not`、`exists`、`forall` 模式
3.  **数据聚合**: 内置累加器 (`sum`、`count`、`average` 等)
4.  **真值维护系统 (TMS)**: 带有自动依赖跟踪的逻辑断言
5.  **复杂事件处理**: 用于 CEP 场景的时间操作符
6.  **可序列化网络**: 通过网络序列化实现快速启动

## 技术栈

-   **C++20** 标准，采用现代实践
-   **QuickJS** 用于 JavaScript 集成
-   **PEGTL** 用于解析 (`vcpkg.json:66-68`)
-   **Catch2** 用于测试
-   **jsoncons** 用于JSONMESPATH
-   **CMake** 构建系统，带有 vcpkg 依赖管理

## 项目结构

```
drills/
├── include/           # 头文件 (AST, 解析器, Rete 节点, Lua 管理器)
├── src/              # 实现文件
├── test/             # 全面测试套件
├── docs/             # 文档
├── examples/         # 使用示例 (目前为空)
├── dsl.md           # 语法规范
├── guide.md         # 用户指南
└── readme.md        # 快速启动指南
```

## 快速启动

这里有一个简单的示例，帮助您快速上手。

### 1. 在 RFL 文件中编写您的规则

**`my_rules.rfl`**
```rfl
// 定义事实的数据模型
declare Person
    name : String
    age : int
end

declare CanVote
    name : String
end

// 一个查找成年人并断言他们可以投票的规则
rule "Identify Voters"
when
    // 匹配年龄大于或等于 18 岁的 Person 事实
    // 并将其绑定到变量 $p
    $p : Person( age >= 18 )

    // 确保我们尚未处理过此人
    not ( CanVote( name == $p.name ) )
then
    // 'then' 块是 JavaScript。
    // 变量 $p 可作为 'p' 使用。
    console.log("Granting voting rights to: " + p.name);
    rfl.insert({ type: "CanVote", name: p.name });
end

// 一个查找所有可以投票的人的查询
query "find_voters"
    $cv : CanVote()
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

// 辅助函数：将文件读取到字符串中
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

    // 预期输出:
    // Granting voting rights to: Alice
    //
    // Fired 1 rule(s).
    // Found 1 person/people who can vote.
    //  - Alice

    return 0;
}
```

## 优势

-   **面向性能**: Rete 算法针对高吞吐量场景进行了优化
-   **清晰分离**: 不可变知识库与可变会话
-   **全面**: 包含 TMS、CEP、聚合等完整功能集
-   **文档完善**: 广泛的指南和示例
-   **现代 C++**: 利用 C++20 特性和最佳实践
-   **测试**: 彻底的测试覆盖

这是一个生产就绪的规则引擎，适用于需要高性能和通过声明式 RFL 语法与 JavaScript 脚本功能相结合的灵活规则编写的复杂业务规则场景。

## 文档

有关 RFL 语法、特性和高级设计模式的完整参考，请参阅
- [RFL 语言指南](./docs/USER_GUIDE.md)
- [RFL 语法指南](./docs/dsl.md)
