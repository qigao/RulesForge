# RulesForge 部署指南

此文只谈当前代码实际支持的能力，不谈纸面架构。当前产品形态是类 Drools 的规则脚本引擎：基于 RETE 做规则推理，支持动态 schema/data binding，并通过 JIT 后端执行已支持的动态脚本路径。

## 1. 编译一次，重用多次

建议的生产形态如下：

- 将规则编译为一个 `KnowledgeBase`
- 初始化阶段按需注册 host callback
- 由此 knowledge base 建立多个短生命周期或可池化的 `StatefulSession`

不要为每个请求重复编译同一套规则。

## 2. 线程安全

当前契约：

- `KnowledgeBase`：仅在初始化完成后可安全共享
- `StatefulSession`：非 thread-safe

实践规则：

- 每个 worker thread、请求或消息流各用一个 session

## 3. 数据加载怎么选

每个集成边界只选一条简单路径：

- JSON：一般服务集成
- CSV：批量或离线加载
- binary：只在已经控制 schema/payload 且确实需要吞吐时使用

不需要同时支持三种格式时，不要把三种都写进产品集成方案。

## 4. 验证与失败模式

RulesForge 通过 C API 提供 session 验证模式与逐次调用状态码。

生产建议：

- 尽早拒收不合法事实
- 在 CI 保留代表性樣本 payload
- 规则编译错误不要用 fallback 盖掉

正确的失败模式通常是“加载或测试时立即失败”，而不是“偷偷补默认值”。

## 5. Host Callback 边界

RulesForge 将扩展代码限制在显式 host callback 边界：

- 直接注册到 knowledge base 的 RHS host callback

运维原则：

- callback 要可预测
- 规则触发的代码里尽量限制外部 I/O
- callback 实现应与依赖它的 rule pack 一同版本化

## 6. 可观测性

C++ runtime 已提供：

- 规则执行追踪
- 规则性能摘要
- session 指標匯出器

它们是诊断与剖析工具，不应长期在生产流量中开启高噪音 tracing。

## 7. 打包

此仓库目前对外安装面主要有：

- `include/` 下的 C 头文件
- `capi/` 产生的 `rule_forge` 共享库
- `lib/cmake/RulesForge` 下的 CMake package 文件

若将 RulesForge 作为产品依赖发布，应以此为契约，不要把私有头文件写进下游集成文档。

## 8. 建议发版检查表

- 确认 `TurboNet`、`TurboScript`、`vcpkg` 的构建输入
- 在 CI 编译规则
- 执行 `ctest --output-on-failure`
- 至少验证一个 JSON 示例与一个 CSV 示例
- 若规则依赖 host callback，验证 callback 注册
- 将 rule pack、callback 实现、应用版本一并记录

## 9. 相关文档

- 上手：[`QUICKSTART.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/QUICKSTART.md)
- 产品指南：[`USER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/USER_GUIDE.md)
- 精确 DSL：[`../dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md)
- data binding 归属：[`../TURBOSCRIPT_DATABIND_PARSER_COMPARISON.md`](/C:/projects/cpp/rulesforge/docs/TURBOSCRIPT_DATABIND_PARSER_COMPARISON.md)
