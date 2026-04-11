# RulesForge 部署指南

此文只談現碼所真有者，不談紙上完美架構。

## 1. 編譯一次，重用多次

建議之生產形態如下：

- 將規則編譯為一個 `KnowledgeBase`
- 在初始化階段註冊 native function 與 codec
- 由此 knowledge base 建立多個短生命或可池化之 `StatefulSession`

若每個請求都重編同一套規則，那不是架構，是浪費 CPU。

## 2. 執行緒安全

當前契約：

- `KnowledgeBase`：僅於初始化完成後可安全共享
- `StatefulSession`：非 thread-safe

實務規則：

- 每個 worker thread、請求、或消息流，各用一個 session

## 3. 資料載入怎麼選

每個整合邊界只選一條平實路徑：

- JSON：一般服務整合
- CSV：批量或離線載入
- binary：僅在汝已掌控 codec 且確實在乎吞吐時再用

若汝的整合根本不需要三種都上，就別把三種都寫進產品文檔。

## 4. 驗證與失敗模式

RulesForge 經由 C API 提供 session 驗證模式與逐次呼叫狀態碼。

生產建議：

- 盡早拒收不合法事實
- 在 CI 保留代表性樣本 payload
- 規則編譯錯誤不要用 fallback 蓋掉

正確之失敗模式，通常是「載入或測試時立即失敗」，而非「偷偷補預設值」。

## 5. 原生擴展

有兩種擴展面：

- 載入到 knowledge base 的 native RHS 函數
- 依 [`include/rule_forge_plugin.h`](/C:/projects/cpp/rulesforge/include/rule_forge_plugin.h) 實作之 source/sink 外掛

運維原則：

- callback 要可預測
- 規則觸發之程式內，盡量限制外部 I/O
- plugin binary 應與依賴它的 rule pack 一同版控

## 6. 可觀測性

C++ runtime 已提供：

- 規則執行追蹤
- 規則效能摘要
- session 指標匯出器

它們是診斷與剖析工具，不是長期把高噪音 tracing 開在生產流量上的藉口。

## 7. 打包

此倉目前對外安裝面主要有：

- `include/` 下之 C 頭檔
- `capi/` 產生之 `rule_forge` 共享庫
- `lib/cmake/RulesForge` 下之 CMake package 檔

若汝將 RulesForge 當產品依賴發行，便應以此為契約，勿把私有頭檔混進下游整合文檔。

## 8. 建議發版檢查表

- 確認 `TurboNet`、`TurboScript`、`TurboNet`、`vcpkg` 之構建輸入
- 在 CI 編譯規則
- 執行 `ctest --output-on-failure`
- 至少驗證一個 JSON 示例與一個 CSV 示例
- 若規則依賴 plugin，驗證 plugin binary 可載入
- 將 rule pack、plugin pack、應用版本一併記錄

## 9. 相關文檔

- 上手：[`QUICKSTART.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/QUICKSTART.md)
- 產品指南：[`USER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/USER_GUIDE.md)
- 精確 DSL：[`../dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md)
- 資料載入實務：[`../PRODUCTION_DATABIND_BEST_PRACTICES.md`](/C:/projects/cpp/rulesforge/docs/PRODUCTION_DATABIND_BEST_PRACTICES.md)
