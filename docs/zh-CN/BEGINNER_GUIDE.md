# RulesForge 初學者指南

若汝從未用過規則引擎，只記三件事：

- 事實 facts 是輸入資料
- 規則 rules 便是 `when ... then ...`
- 會話 session 是規則執行時承載資料之盒子

## 最小可用心智模型

RulesForge 將兩物分開：

- `KnowledgeBase`：已編譯之規則，可於完成設定後共享
- `StatefulSession`：可變執行時狀態，同一時間只應由一執行緒使用

此分離，比任何宣傳語都更重要。規則編譯一次，會話可反覆建立。

## 一個極小規則

```rfl
declare Person
    name: String
    age: int
end

query "Adults"
    $p: Person(age >= 18)
end
```

此查詢不修改資料，只是找出符合條件之事實。

## 最容易看見效果之方式

先完成構建，然後執行：

```bash
./build/bin/capi_demo \
  -r docs/examples/loan-eligibility/loan-eligibility.rfl \
  -j docs/examples/loan-eligibility/loan-applications-sample.json \
  -m applications:com.bank.loan.LoanApplication \
  -q LoanDecisions \
  -b decision \
  -f applicationId,approved,approvedAmount,reason
```

先用 `capi_demo`，有三個理由：

- 它走的是公開 API [`include/rule_forge.h`](/C:/projects/cpp/rulesforge/include/rule_forge.h)
- 它本就在此倉中
- 它覆蓋了實際之 compile/load/fire/query 路徑

## 初學先學何物

1. 如何用 `declare` 定義事實型別
2. 如何寫一個 `query`
3. 如何寫一個 `rule`
4. 如何載入 JSON 或 CSV 事實
5. 為何 session 不可跨執行緒共用

未有明確整合需求之前，勿先跳入 plugin DLL、自定 native callback、或 router 整合。那只是徒增噪音。

## 建議閱讀次序

1. [`QUICKSTART.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/QUICKSTART.md)
2. [`USER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/USER_GUIDE.md)
3. [`../dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md)
4. [`../examples/README.md`](/C:/projects/cpp/rulesforge/docs/examples/README.md)
