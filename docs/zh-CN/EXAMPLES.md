# RulesForge 示例总览

此页为中文入口；详细规则内容仍在 [`docs/examples/`](/C:/projects/cpp/rulesforge/docs/examples)。

## 哪些示例可直接运行

可直接用 `capi_demo` 跑：

- 贷款审批 `loan-eligibility`
- 保险定价 `insurance-pricing`

先扁平化后可运行：

- 订单履约 `order-fulfillment`
- 差旅预订 `travel-booking`

需专用 runner：

- 欺诈检测 `fraud-detection`

仅用于多文件组织演示：

- 模块化规则 `modular-rules`

## 直接可跑的命令

### 贷款审批

```bash
./build/bin/capi_demo \
  -r docs/examples/loan-eligibility/loan-eligibility.rfl \
  -j docs/examples/loan-eligibility/loan-applications-sample.json \
  -m applications:com.bank.loan.LoanApplication \
  -q LoanDecisions \
  -b decision \
  -f applicantId,approved,approvedAmount,interestRate,reason
```

### 保险定价

```bash
./build/bin/capi_demo \
  -r docs/examples/insurance-pricing/insurance-pricing.rfl \
  -j docs/examples/insurance-pricing/insurance-applications-sample.json \
  -m applications:com.insurance.auto.InsuranceApplication \
  -q PolicyDecisions \
  -b decision \
  -f applicationId,approved,annualPremium,coverageLevel,reason
```

## 先扁平化再运行

订单履约：

```bash
python tools/flatten_example_data.py \
  order \
  docs/examples/order-fulfillment/order-test-data.json \
  docs/examples/order-fulfillment/order-flat.json
```

差旅预订：

```bash
python tools/flatten_example_data.py \
  travel \
  docs/examples/travel-booking/travel-test-data.json \
  docs/examples/travel-booking/travel-flat.json
```

欺诈检测：

```bash
python tools/flatten_example_data.py \
  fraud \
  docs/examples/fraud-detection/fraud-test-data.json \
  docs/examples/fraud-detection/fraud-flat.json
```

扁平化后，用专用 runner 执行：

```bash
./build/bin/fraud_stream_runner \
  docs/examples/fraud-detection/fraud-detection.rfl \
  docs/examples/fraud-detection/fraud-flat.json
```

此 runner 输出的是“最终告警视图”，不是把原始工作内存中的 `FraudAlert` 逐行照抄出来：

- 会按交易合并重复告警，只保留最高风险等级
- 会从 `FraudSignal` 重新累计并显示 `totalScore`
- 每笔交易最终只输出一行：`transactionId | accountId | totalScore | riskLevel | action`

## 为何仍不能用 `capi_demo`

`fraud-detection` 的规则消费的是 `entry-point "transaction-stream"`，而当前公开 C API 尚无 entry-point 插入接口。故它雖可扁平化，仍不能走 `capi_demo`，只能用專用 runner 或自定程式。

## 继续阅读

- 英文详细示例索引：[`../examples/README.md`](/C:/projects/cpp/rulesforge/docs/examples/README.md)
- DSL 语法：[`../dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md)
- 中文快速上手：[`QUICKSTART.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/QUICKSTART.md)
