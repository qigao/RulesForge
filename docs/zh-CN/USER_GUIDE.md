# RulesForge 使用指南

此文是产品层指南；精确语法以 [`../dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md) 为准。若本文与代码不一致，以代码为准。

RulesForge 是类 Drools 的规则脚本引擎。它支持基于 RETE 的规则推理、动态 schema/data binding，以及基于 JIT 的动态脚本执行。

## 1. 先选对 API 面

RulesForge 当前有三层可实际集成的接口：

- 公开 C API：[`include/rule_forge.h`](/C:/projects/cpp/rulesforge/include/rule_forge.h)
- C++ 引擎 API：`rulesforge/include` 與 `parser/include`
- RFL/TurboScript 脚本执行，以及通过 C API 暴露的显式 host callback 边界

建议：

- 新应用嵌入：先用 C API
- 需要更细的进程内控制：用 C++ API
- 外部副作用：显式注册 host callback

## 2. 运行时模型

当前运行时核心有两个主要对象：

- `KnowledgeBase`：已编译规则与 host callback 注册
- `StatefulSession`：事实、agenda、查询、追踪、验证模式、运行期指标

线程模型：

- `KnowledgeBase` 在规则加载与 callback 注册完成后，可供共享
- `StatefulSession` 非 thread-safe

## 3. 加载规则

目前实现的规则加载路径有：

- 内存中的 RFL 字符串
- 单个 RFL 文件
- 多个 RFL 文件加 import 解析
- 通过 C API 加载 decision table CSV

相關 API：

- `ruleforge_kb_load_drl()`
- `ruleforge_kb_load_drl_file()`
- `ruleforge_kb_load_drl_files()`
- `ruleforge_kb_load_decision_table_csv()`

## 4. 加载事实

RulesForge 引擎看到的是 fact。公开 C API 另提供基于 `TurboScript::DataBind` 的 schema-aware 数据绑定入口。

- 已构造的 fact 对象
- 带 schema 的 JSON
- 带 schema 的 CSV
- 带 schema 的 XML
- 带 schema 的 binary TBE payload

C API 入口：

- `ruleforge_session_add_fact_json()`
- `ruleforge_session_add_fact_json_schema()`
- `ruleforge_session_add_fact_binary_schema()`
- `ruleforge_session_add_facts_csv_schema()`
- `ruleforge_session_add_facts_xml_schema()`
- field-based fact construction APIs

C++ 入口：

- `session->add_fact(fact)`
- `session->add_data(DataSource::fact(fact))`

补充：

- C++ engine runtime 仍然是 fact-only。
- schema-aware C API helper 会调用 `TurboScript::DataBind`，把绑定结果转换成 session-owned fact，再插入 session。
- RFL 中可以用 `import "name.schema"` 导入 schema 声明。

## 5. 查询

RulesForge 支持在 RFL 中定义 named query，并在加载事实、触发规则后执行查询。

C API：

- `ruleforge_session_query()`
- `ruleforge_query_result_get_size()`
- `ruleforge_query_result_get_fact_at_index()`

C++：

- `session->execute_query("QueryName")`

## 6. Host Callback

RHS host 调用：

- 用 `ruleforge_kb_register_native_function()` 显式注册
- 供规则内 `invoke(...)` 之类逻辑调用
- 保持确定性，失败按运行时错误处理

## 7. RFL 当前实现范围

当前 parser 与 runtime 已覆盖：

- `package`、`import`、`global`、`declare`、`enum`、`function`、`query`、`rule`
- `salience`、`agenda-group`、`activation-group`、`no-loop`、`enabled`、`duration`、`timer`、`extends`
- `not`、`exists`、`forall`、`accumulate`、query call 等模式
- `insert`、`insertLogical`、`update`、`retract`、`halt` 与 RHS 控制流
- `over window:time(...)` 滑动窗口语法

精确语法与限制请读 [`../dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md)。

## 8. 生产落地清单

- 规则编译一次，重用 `KnowledgeBase`
- 每线程或每请求建立一个 `StatefulSession`
- 每个集成边界只选一种数据加载格式
- 在 CI 验证规则包与样例数据
- 追踪与指标只在需要时开启
- 自定义 host callback 要小、确定、且安全

## 9. 接着看哪里

- 快速入口：[`QUICKSTART.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/QUICKSTART.md)
- 部署：[`DEPLOYMENT.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/DEPLOYMENT.md)
- 示例：[`EXAMPLES.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/EXAMPLES.md)
- data binding 归属：[`../TURBOSCRIPT_DATABIND_PARSER_COMPARISON.md`](/C:/projects/cpp/rulesforge/docs/TURBOSCRIPT_DATABIND_PARSER_COMPARISON.md)
