# RulesForge 使用指南

此文為產品層指南；若欲查精確語法，以 [`../dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md) 為準。若本文與程式相左，則程式勝。

## 1. 先選對 API 面

RulesForge 當前有三層可實際整合之介面：

- 公開 C API：[`include/rule_forge.h`](/C:/projects/cpp/rulesforge/include/rule_forge.h)
- C++ 引擎 API：`rulesforge/include` 與 `parser/include`
- 外掛 ABI：[`include/rule_forge_plugin.h`](/C:/projects/cpp/rulesforge/include/rule_forge_plugin.h)

建議：

- 新應用嵌入：先用 C API
- 需更細緻之進程內控制：用 C++ API
- 需可重用之外部 source/sink 整合：用 plugin ABI

## 2. 執行時模型

現碼核心只有兩個主要物件：

- `KnowledgeBase`：已編譯規則、native function 註冊、codec registry
- `StatefulSession`：事實、agenda、查詢、追蹤、驗證模式、運行期指標

執行緒模型：

- `KnowledgeBase` 在規則載入與 native 註冊完成後，可供共享
- `StatefulSession` 非 thread-safe

## 3. 載入規則

目前實作之規則載入路徑有：

- 記憶體內 RFL 字串
- 單一 RFL 檔
- 多個 RFL 檔加 import 解析
- 經由 C API 載入 decision table CSV

相關 API：

- `ruleforge_kb_load_drl()`
- `ruleforge_kb_load_drl_file()`
- `ruleforge_kb_load_drl_files()`
- `ruleforge_kb_load_decision_table_csv()`

## 4. 載入事實

引擎當前實用之資料載入格式，凡三：

- JSON
- CSV
- binary payload（需有對應 codec 宣告或匯入）

C API 入口：

- `ruleforge_session_add_fact_json()`
- `ruleforge_session_add_facts_csv()`
- `ruleforge_session_add_fact_binary()`

C++ 入口：

- `session->add_data(DataSource::json(...))`
- `session->add_data(DataSource::csv(...))`
- `session->add_data(DataSource::binary(...))`

補充：

- C++ `DataSource::json(content, expr)` 可套用 JMESPath
- C++ `DataSource::csv(...)` 目前讀的是檔案路徑
- binary 載入依賴 knowledge base 中可用之 codec

## 5. 查詢

RulesForge 支援於 RFL 中定義 named query，並於載入事實、觸發規則後執行查詢。

C API：

- `ruleforge_session_query()`
- `ruleforge_query_result_get_size()`
- `ruleforge_query_result_get_fact_at_index()`

C++：

- `session->execute_query("QueryName")`

## 6. Native 函數與外掛

此二者是兩條不同擴展路徑，勿混為一談。

Native RHS 函數：

- 可用 `ruleforge_kb_register_native_function()` 直接註冊
- 或以 `ruleforge_kb_load_native_function_table()` 載入 DLL/so 函數表
- 供規則內 `invoke(...)` 之類邏輯調用

Source/sink 外掛：

- 由 [`include/rule_forge_plugin.h`](/C:/projects/cpp/rulesforge/include/rule_forge_plugin.h) 定義
- 供 plugin-based ingestion/routing 使用
- 示例在 [`plugins/examples`](/C:/projects/cpp/rulesforge/plugins/examples)
- 說明在 [`plugins/README.md`](/C:/projects/cpp/rulesforge/plugins/README.md)

## 7. RFL 現已實作到哪裡

當前 parser 與 runtime 已覆蓋：

- `package`、`import`、`global`、`declare`、`enum`、`function`、`query`、`rule`
- `salience`、`agenda-group`、`activation-group`、`no-loop`、`enabled`、`duration`、`timer`、`extends`
- `not`、`exists`、`forall`、`accumulate`、query call 等模式
- `insert`、`insertLogical`、`update`、`retract`、`halt` 與 RHS 控制流
- `over window:time(...)` 之滑動視窗語法

精確語法與限制，仍請讀 [`../dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md)。

## 8. 生產落地清單

- 規則編譯一次，重用 `KnowledgeBase`
- 每執行緒或每請求建立一個 `StatefulSession`
- 每個整合邊界只選一種資料載入格式，勿自找複雜
- 在 CI 驗證規則包與樣例資料
- 追蹤與指標只在需要時開啟
- 自定 native function 要小、可預測、且安全

## 9. 接著看何處

- 快速入口：[`QUICKSTART.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/QUICKSTART.md)
- 部署：[`DEPLOYMENT.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/DEPLOYMENT.md)
- 示例：[`EXAMPLES.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/EXAMPLES.md)
- 外掛整合：[`PLUGINS.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/PLUGINS.md)
