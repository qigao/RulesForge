# RulesForge

RulesForge 乃面向嵌入式整合之 C/C++ 规则引擎，以 Rete 類匹配網路、RFL 規則語言、以及精簡之公開 C API 為核心。

English: [`README.md`](/C:/projects/cpp/rulesforge/README.md)

## 倉庫所含模組

- `rulesforge/`：核心 C++ 引擎
- `parser/`：可獨立使用之 RFL 解析器
- `capi/`：共享庫與公開 C API
- `plugins/`：source/sink 外掛 ABI 與示例
- `router/`：建立於外掛 ABI 之路由引擎
- `docs/`：產品文檔、DSL 參考、部署與示例
- `tools/rulesforge_schema_compiler/`：schema/codegen 工具

## 推薦入口

新使用者先看公開 C API：

- 主 API：[`include/rule_forge.h`](/C:/projects/cpp/rulesforge/include/rule_forge.h)
- 外掛 ABI：[`include/rule_forge_plugin.h`](/C:/projects/cpp/rulesforge/include/rule_forge_plugin.h)
- 共用型別：[`include/ruleforge_types.h`](/C:/projects/cpp/rulesforge/include/ruleforge_types.h)

C++ API 亦可用，然較偏底層，散見於 `rulesforge/include` 與 `parser/include`。語法與執行語義，當以 [`docs/dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md) 為準，其已明言「若文檔與程式相左，則以程式為準」。

## 構建

先備條件：

- CMake 3.20+
- 支援 C17/C++20 之編譯器
- Ninja
- `vcpkg`
- 本地可用之 `TurboNet`、`TurboScript`、`TurboNet`

頂層 CMake 目前要求以下路徑變數：

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

倉內亦附 `presets/` 與 `CMakePresets.json`。先行 `cmake --list-presets`，再用汝機器上實際存在者即可。

## 5 分鐘上手

當前最穩之產品路徑，是使用隨倉附帶的 `capi_demo`：

```bash
./build/bin/capi_demo \
  -r docs/examples/loan-eligibility/loan-eligibility.rfl \
  -j docs/examples/loan-eligibility/loan-applications-sample.json \
  -m applications:com.bank.loan.LoanApplication \
  -q LoanDecisions \
  -b decision \
  -f applicationId,approved,approvedAmount,reason
```

此命令會完成四事：編譯規則、載入 JSON 事實、觸發規則、輸出 `LoanDecisions` 查詢結果。

## 文檔索引

- 快速上手：[`docs/zh-CN/QUICKSTART.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/QUICKSTART.md)
- 初學者指南：[`docs/zh-CN/BEGINNER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/BEGINNER_GUIDE.md)
- 使用指南：[`docs/zh-CN/USER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/USER_GUIDE.md)
- DSL 參考：[`docs/dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md)
- 部署：[`docs/zh-CN/DEPLOYMENT.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/DEPLOYMENT.md)
- 生產資料載入建議：[`docs/PRODUCTION_DATABIND_BEST_PRACTICES.md`](/C:/projects/cpp/rulesforge/docs/PRODUCTION_DATABIND_BEST_PRACTICES.md)
- 示例總覽：[`docs/zh-CN/EXAMPLES.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/EXAMPLES.md)
- 外掛：[`docs/zh-CN/PLUGINS.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/PLUGINS.md)
- Router：[`router/README.md`](/C:/projects/cpp/rulesforge/router/README.md)

## 本次整理所修之病

此前產品文檔有數大問題：

- 無根目錄英文 README
- 多處示例援引不存在之 API，如 `fact_builder.hpp`、`get_facts_of_type()`
- 構建命令與實際 CMake 配置不合
- 舊產品名 `Drills` 殘留
- 中英文入口互鏈錯亂

今次先將產品入口文檔重寫為與現碼相符之版本。
