# RulesForge Examples

These examples show realistic rule sets. They are not all equally runnable with the same loader.

That distinction matters:

- `capi/examples/payments.*` is the current direct `capi_demo` smoke example
- examples in this directory may need a `.schema` companion before they can use schema-bound C API loading
- some examples use nested scenario JSON and need preprocessing or a custom loader

## Runability Matrix

| Example | Domain | Direct `capi_demo` Run | Notes |
|---------|--------|------------------------|-------|
| [Loan Eligibility](./loan-eligibility/) | Financial services | Needs schema migration | top-level `applications` array maps cleanly, but external input types are still declared in RFL |
| [Insurance Pricing](./insurance-pricing/) | Insurance | Needs schema migration | top-level `applications` array maps cleanly, but external input types are still declared in RFL |
| [Fraud Detection](./fraud-detection/) | Banking / security | Custom C++ runner | uses `transaction-stream`; the C continuous API is an alternative for schema-bound events |
| [Order Fulfillment](./order-fulfillment/) | E-commerce | Needs schema migration and preprocessing | flatten nested `testOrders[]` first |
| [Travel Booking](./travel-booking/) | Travel | Needs schema migration and preprocessing | flatten nested `testScenarios[]` first |
| [Modular Rules](./modular-rules/) | Architecture | N/A | demonstrates multi-file import layout |

## Directly Runnable Example

Use the C API payments smoke example:

```bash
./build/bin/capi_demo \
  -r capi/examples/payments.rfl \
  -s capi/examples/payments.schema \
  -j capi/examples/payments_test_data.json \
  -m testOrders:Order \
  -q OrdersWithDiscount \
  -b order \
  -f quantity,unitPrice,finalPrice
```

For domain examples in this directory, create a `.schema` for each external input type, import it from the RFL file, and keep RFL `declare` for internal derived facts.

## Examples That Need Preprocessing

### Fraud Detection

Flatten the nested sample first:

```bash
python tools/flatten_example_data.py \
  fraud \
  docs/examples/fraud-detection/fraud-test-data.json \
  docs/examples/fraud-detection/fraud-flat.json
```

Then run the dedicated runner:

```bash
./build/bin/fraud_stream_runner \
  docs/examples/fraud-detection/fraud-detection.rfl \
  docs/examples/fraud-detection/fraud-flat.json
```

The runner prints one consolidated alert per transaction. It merges duplicate raw alert facts by highest severity and computes the displayed `totalScore` from the emitted `FraudSignal` facts.

This example cannot run through the batch-oriented `capi_demo`, because its
rules consume transactions from `entry-point "transaction-stream"`. The public
continuous C API does expose named entry-point insertion for schema-bound JSON
events, but the example's existing runner also performs domain-specific input
flattening and alert consolidation.

### Order Fulfillment

Flatten the nested sample first:

```bash
python tools/flatten_example_data.py \
  order \
  docs/examples/order-fulfillment/order-test-data.json \
  docs/examples/order-fulfillment/order-flat.json
```

After flattening, add a `.schema` file for the external input facts, import it from the RFL file, and pass the same schema with `capi_demo -s`.

### Travel Booking

Flatten the nested sample first:

```bash
python tools/flatten_example_data.py \
  travel \
  docs/examples/travel-booking/travel-test-data.json \
  docs/examples/travel-booking/travel-flat.json
```

After flattening, add a `.schema` file for the external input facts, import it from the RFL file, and pass the same schema with `capi_demo -s`.

## Modular Rules

The modular rules example is about import resolution, not a ready-made CLI demo.
See [`modular-rules/README.md`](./modular-rules/README.md) for the file layout and
parser entry pattern.

## What These Examples Are Good For

- validating business rule structure
- learning RFL patterns from non-trivial rule sets
- testing query outputs against known sample domains
- building your own C or C++ integration harness on top of the public C ABI

## Related Docs

- main product entry: [`../USER_GUIDE.md`](../USER_GUIDE.md)
- data ingestion: [`../DATA_INGESTION.md`](../DATA_INGESTION.md)
- runnable C API example: [`../../capi/examples/readme.md`](../../capi/examples/readme.md)
- DSL reference: [`../dsl.md`](../dsl.md)
