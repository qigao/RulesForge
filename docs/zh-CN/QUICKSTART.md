# RulesForge 快速上手

此指南以公開 C API 與隨倉附帶之 `capi_demo` 為主，蓋此路徑最符合當前安裝面與示例檔。

## 1. 構建

先備條件：

- CMake 3.20+
- 支援 C17/C++20 之編譯器
- Ninja
- `vcpkg`
- 本地可用之 `TurboNet`、`TurboScript`、`TurboNet`

通用配置命令：

```bash
cmake -S . -B build \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DTURBONET_ROOT=/path/to/TurboNet \
  -DTURBOSCRIPT_ROOT=/path/to/TurboScript \
  -DTURBO_UTILS=/path/to/TurboNet

cmake --build build
```

若汝機器已配置倉庫專用 preset，可先行 `cmake --list-presets` 檢之。

## 2. 先跑一個真示例

用內建之貸款審批示例：

```bash
./build/bin/capi_demo \
  -r docs/examples/loan-eligibility/loan-eligibility.rfl \
  -j docs/examples/loan-eligibility/loan-applications-sample.json \
  -m applications:com.bank.loan.LoanApplication \
  -q LoanDecisions \
  -b decision \
  -f applicationId,approved,approvedAmount,reason
```

各參數之義：

- `-r`：RFL 規則檔
- `-j`：JSON 數據檔
- `-m`：將一個 JSON 陣列映射為一種事實型別，格式為 `array:type`
- `-q`：規則觸發後要執行之查詢
- `-b`：查詢結果中要取之 binding 名
- `-f`：輸出欄位列表

## 3. 執行時實際流程

`capi_demo` 會依次做此七步：

1. `ruleforge_init()`
2. `ruleforge_kb_create()`
3. `ruleforge_kb_load_drl()`
4. `ruleforge_session_create()`
5. 從 JSON 或 CSV 載入事實
6. `ruleforge_session_fire_all_rules()`
7. `ruleforge_session_query()`

上述 API 皆定義於 [`include/rule_forge.h`](/C:/projects/cpp/rulesforge/include/rule_forge.h)。

## 4. 再試 CSV

```bash
./build/bin/capi_demo \
  -r capi/examples/payments.rfl \
  -c capi/examples/payments_test_data.csv \
  -T com.example.pricing.Order \
  -q OrdersWithDiscount \
  -f quantity,unitPrice,finalPrice
```

## 5. 接著讀什麼

- 需更白話之入門：[`BEGINNER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/BEGINNER_GUIDE.md)
- 需了解整合模型與 API 選型：[`USER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/USER_GUIDE.md)
- 需精確語法：[`../dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md)
- 需部署建議：[`DEPLOYMENT.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/DEPLOYMENT.md)
