# RulesForge

RulesForge 是面向 C/C++ 嵌入式集成的基于 RETE 的规则引擎。它支持动态 schema 和数据绑定，并通过 JIT 后端运行规则。

内部实现上，RFL 描述规则与 working memory，TurboScript::DataBind 负责 schema-aware 数据绑定，RETE 负责规则传播规划，已支持的规则 kernel 通过 JIT 路径运行。

English: [`README.md`](/C:/projects/cpp/rulesforge/README.md)

## 仓库所含模块

- `rulesforge/`：核心 C++ 引擎
- `parser/`：可独立使用的 RFL 解析器
- `capi/`：共享库与公开 C API
- `docs/`：产品文档、DSL 参考、部署与示例
- `tools/rulesforge_schema_compiler/`：schema/codegen 工具

## 推荐入口

新用户先看公开 C API：

- 主 API：[`include/rule_forge.h`](/C:/projects/cpp/rulesforge/include/rule_forge.h)
- 共用类型：[`include/ruleforge_types.h`](/C:/projects/cpp/rulesforge/include/ruleforge_types.h)

C++ API 也可用，但更偏底层，分布在 `rulesforge/include` 与 `parser/include`。语法与执行语义以 [`docs/dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md) 为准。

## 构建

前置条件：

- CMake 3.20+
- 支持 C17/C++20 的编译器
- Ninja
- `vcpkg`
- 本地可用的 `TurboNet` 和 `TurboScript`

顶层 CMake 目前要求以下路径变量：

- `TURBONET_ROOT`
- `TURBOSCRIPT_ROOT`
- `TURBO_UTILS`

通用配置流程：

```bash
cmake -S . -B build \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DTURBONET_ROOT=/path/to/TurboNet \
  -DTURBOSCRIPT_ROOT=/path/to/TurboScript \
  -DTURBO_UTILS=/path/to/TurboNet

cmake --build build
ctest --test-dir build --output-on-failure
```

仓库内也提供 `presets/` 与 `CMakePresets.json`。先运行 `cmake --list-presets`，再选择当前环境实际存在的 preset。

## 5 分鐘上手

当前最稳的产品路径是使用仓库附带的 `capi_demo`：

```bash
./build/bin/capi_demo \
  -r docs/examples/loan-eligibility/loan-eligibility.rfl \
  -j docs/examples/loan-eligibility/loan-applications-sample.json \
  -m applications:com.bank.loan.LoanApplication \
  -q LoanDecisions \
  -b decision \
  -f applicationId,approved,approvedAmount,reason
```

此命令会完成四件事：编译规则、加载 JSON 事实、触发规则、输出 `LoanDecisions` 查询结果。

## 文档索引

- 快速上手：[`docs/zh-CN/QUICKSTART.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/QUICKSTART.md)
- 初学者指南：[`docs/zh-CN/BEGINNER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/BEGINNER_GUIDE.md)
- 使用指南：[`docs/zh-CN/USER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/USER_GUIDE.md)
- DSL 参考：[`docs/dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md)
- 产品发布门槛：[`docs/PRODUCT_READINESS.md`](/C:/projects/cpp/rulesforge/docs/PRODUCT_READINESS.md)
- 错误目录：[`docs/ERROR_CATALOG.md`](/C:/projects/cpp/rulesforge/docs/ERROR_CATALOG.md)
- C API 契约：[`docs/C_API_CONTRACT.md`](/C:/projects/cpp/rulesforge/docs/C_API_CONTRACT.md)
- TurboScript DataBind/parser 对比：[`docs/TURBOSCRIPT_DATABIND_PARSER_COMPARISON.md`](/C:/projects/cpp/rulesforge/docs/TURBOSCRIPT_DATABIND_PARSER_COMPARISON.md)
- 部署：[`docs/zh-CN/DEPLOYMENT.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/DEPLOYMENT.md)
- 示例总览：[`docs/zh-CN/EXAMPLES.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/EXAMPLES.md)

## 本次整理修复的问题

此前产品文档有几类问题：

- 没有根目录英文 README
- 多处示例引用不存在的 API，如 `fact_builder.hpp`、`get_facts_of_type()`
- 构建命令与实际 CMake 配置不一致
- 舊產品名 `Drills` 殘留
- 中英文入口互链错乱

本次先将产品入口文档更新为与当前代码相符的版本。
