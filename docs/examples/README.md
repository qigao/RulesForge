# RulesForge Examples

These examples show realistic rule sets. They are not all equally runnable with the same loader.

That distinction matters:

- some examples can run directly with `capi_demo`
- some examples use nested scenario JSON and need preprocessing or a custom loader

## Runability Matrix

| Example | Domain | Direct `capi_demo` Run | Notes |
|---------|--------|------------------------|-------|
| [Loan Eligibility](./loan-eligibility/) | Financial services | Yes | top-level `applications` array maps cleanly |
| [Insurance Pricing](./insurance-pricing/) | Insurance | Yes | top-level `applications` array maps cleanly |
| [Fraud Detection](./fraud-detection/) | Banking / security | Custom runner | needs flattening plus `transaction-stream` insertion |
| [Order Fulfillment](./order-fulfillment/) | E-commerce | Yes, after preprocessing | flatten nested `testOrders[]` first |
| [Travel Booking](./travel-booking/) | Travel | Yes, after preprocessing | flatten nested `testScenarios[]` first |
| [Modular Rules](./modular-rules/) | Architecture | N/A | demonstrates multi-file import layout |

## Directly Runnable Examples

### Loan Eligibility

```bash
./build/bin/capi_demo \
  -r docs/examples/loan-eligibility/loan-eligibility.rfl \
  -j docs/examples/loan-eligibility/loan-applications-sample.json \
  -m applications:com.bank.loan.LoanApplication \
  -q LoanDecisions \
  -b decision \
  -f applicantId,approved,approvedAmount,interestRate,reason
```

### Insurance Pricing

```bash
./build/bin/capi_demo \
  -r docs/examples/insurance-pricing/insurance-pricing.rfl \
  -j docs/examples/insurance-pricing/insurance-applications-sample.json \
  -m applications:com.insurance.auto.InsuranceApplication \
  -q PolicyDecisions \
  -b decision \
  -f applicationId,approved,annualPremium,coverageLevel,reason
```

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

This example still cannot run through `capi_demo`, because the rules consume transactions from `entry-point "transaction-stream"` and the current public C API does not expose entry-point insertion.

### Order Fulfillment

Flatten the nested sample first:

```bash
python tools/flatten_example_data.py \
  order \
  docs/examples/order-fulfillment/order-test-data.json \
  docs/examples/order-fulfillment/order-flat.json
```

Then run:

```bash
./build/bin/capi_demo \
  -r docs/examples/order-fulfillment/order-fulfillment.rfl \
  -j docs/examples/order-fulfillment/order-flat.json \
  -m warehouses:com.ecommerce.Warehouse \
  -m inventory:com.ecommerce.Inventory \
  -m promotions:com.ecommerce.Promotion \
  -m orders:com.ecommerce.Order \
  -m orderItems:com.ecommerce.OrderItem \
  -q FulfillmentPlans \
  -b plan \
  -f orderId,itemId,warehouseId,carrier,shippingMethod,shippingCost
```

### Travel Booking

Flatten the nested sample first:

```bash
python tools/flatten_example_data.py \
  travel \
  docs/examples/travel-booking/travel-test-data.json \
  docs/examples/travel-booking/travel-flat.json
```

Then run:

```bash
./build/bin/capi_demo \
  -r docs/examples/travel-booking/travel-booking.rfl \
  -j docs/examples/travel-booking/travel-flat.json \
  -m visaRequirements:com.travel.VisaRequirement \
  -m flightOptions:com.travel.FlightOption \
  -m hotelOptions:com.travel.HotelOption \
  -m travelRequests:com.travel.TravelRequest \
  -q PackageQuotes \
  -b quote \
  -f requestId,grandTotal,totalPointsEarned
```

## Modular Rules

The modular rules example is about import resolution, not a ready-made CLI demo. See [`modular-rules/README.md`](/C:/projects/cpp/rulesforge/docs/examples/modular-rules/README.md) for the file layout and parser entry pattern.

## What These Examples Are Good For

- validating business rule structure
- learning RFL patterns from non-trivial rule sets
- testing query outputs against known sample domains
- building your own integration harness on top of the public C API or C++ API

## Related Docs

- main product entry: [`README.md`](/C:/projects/cpp/rulesforge/README.md)
- quickstart: [`../QUICKSTART.md`](/C:/projects/cpp/rulesforge/docs/QUICKSTART.md)
- DSL reference: [`../dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md)
