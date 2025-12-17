# RuleForge Examples

Real-world business rule examples demonstrating the full capabilities of the RuleForge rules engine.

## Example Categories

| Example | Domain | Key Features Demonstrated |
|---------|--------|---------------------------|
| [Loan Eligibility](./loan-eligibility/) | Financial Services | Credit scoring, risk assessment, multi-criteria decisions |
| [Insurance Pricing](./insurance-pricing/) | Insurance | Risk profiling, premium calculation, underwriting rules |
| [Fraud Detection](./fraud-detection/) | Banking/Security | CEP, temporal reasoning, velocity checks, alert escalation |
| [Order Fulfillment](./order-fulfillment/) | E-commerce | Inventory management, discount stacking, shipping optimization |
| [Travel Booking](./travel-booking/) | Travel Industry | Multi-service orchestration, eligibility checks, package pricing |

## Quick Start

Each example includes:
- **README.md** - Business scenario, data model, and rules documentation
- **\*.drl** - Complete rule definitions in DRL format
- **\*-sample.json / \*-test-data.json** - Test data for running the example

### Running an Example

#### Using capi_demo (Recommended)

The quickest way to run examples is with `capi_demo`:

```bash
# Loan Eligibility
capi_demo \
  -d docs/examples/loan-eligibility/loan-eligibility.drl \
  -j docs/examples/loan-eligibility/loan-applications-sample.json \
  -m applications:com.bank.loan.LoanApplication \
  -q LoanDecisions -b decision \
  -f applicationId,approved,approvedAmount,interestRate,reason

# Insurance Pricing
capi_demo \
  -d docs/examples/insurance-pricing/insurance-pricing.drl \
  -j docs/examples/insurance-pricing/insurance-applications-sample.json \
  -m applications:com.insurance.auto.InsuranceApplication \
  -q PolicyDecisions -b decision \
  -f applicationId,approved,annualPremium,coverageLevel,reason

# Fraud Detection
capi_demo \
  -d docs/examples/fraud-detection/fraud-detection.drl \
  -j docs/examples/fraud-detection/fraud-test-data.json \
  -m accountProfiles:com.bank.fraud.AccountProfile \
  -q ActiveAlerts -b alert \
  -f transactionId,accountId,totalScore,riskLevel,action

# Order Fulfillment
capi_demo \
  -d docs/examples/order-fulfillment/order-fulfillment.drl \
  -j docs/examples/order-fulfillment/order-test-data.json \
  -m warehouses:com.ecommerce.Warehouse \
  -m inventory:com.ecommerce.Inventory \
  -m products:com.ecommerce.Product \
  -q FulfillmentPlans -b plan \
  -f orderId,warehouseId,carrier,shippingMethod,shippingCost

# Travel Booking
capi_demo \
  -d docs/examples/travel-booking/travel-booking.drl \
  -j docs/examples/travel-booking/travel-test-data.json \
  -m visaRequirements:com.travel.VisaRequirement \
  -m flightOptions:com.travel.FlightOption \
  -m hotelOptions:com.travel.HotelOption \
  -q PackageQuotes -b quote \
  -f requestId,totalPrice,totalPointsEarned,packageDiscount
```

#### Using C++ API

```cpp
#include "knowledge_base.hpp"

int main() {
    ParseResult result;
    auto kb = build_knowledge_base_from_file("loan-eligibility/loan-eligibility.drl", result);
    if (!result.success) {
        std::cerr << "Parse error: " << result.error_message << std::endl;
        return 1;
    }

    auto session = kb->create_session();

    // Load facts from JSON or create programmatically
    auto applicant = std::make_shared<Fact>();
    applicant->type = "com.bank.loan.LoanApplicant";
    applicant->fields["applicantId"] = "APP-001";
    applicant->fields["annualIncome"] = 85000.0;
    // ... set other fields
    session->add_fact(applicant);

    // Fire rules
    session->fire_all_rules();

    // Query results
    auto decisions = session->execute_query("LoanDecisions");
    // Process results...

    return 0;
}
```

## Feature Coverage Matrix

| Feature | Loan | Insurance | Fraud | Order | Travel |
|---------|:----:|:---------:|:-----:|:-----:|:------:|
| Pattern Matching | ✓ | ✓ | ✓ | ✓ | ✓ |
| Variable Binding | ✓ | ✓ | ✓ | ✓ | ✓ |
| Constraints | ✓ | ✓ | ✓ | ✓ | ✓ |
| `accumulate` | ✓ | ✓ | ✓ | ✓ | - |
| `not` Patterns | ✓ | ✓ | ✓ | ✓ | ✓ |
| `exists` Patterns | - | - | ✓ | ✓ | - |
| `forall` Patterns | - | - | - | ✓ | - |
| `salience` | ✓ | ✓ | ✓ | ✓ | ✓ |
| `activation-group` | - | - | - | ✓ | - |
| `no-loop` | - | - | ✓ | - | - |
| Entry Points (CEP) | - | - | ✓ | - | - |
| Temporal Operators | - | - | ✓ | - | - |
| Queries | ✓ | ✓ | ✓ | ✓ | ✓ |
| Multi-phase Execution | ✓ | ✓ | ✓ | ✓ | ✓ |

## Example Details

### 1. Loan Eligibility
**Domain**: Banking / Financial Services

Demonstrates automated loan application processing with:
- Credit score categorization (Excellent → Poor)
- Risk factor assessment (debt-to-income, employment history)
- Auto-rejection rules for disqualifying conditions
- Tiered loan decisions based on risk levels

**Key Patterns**:
```drl
// Risk factor accumulation
$riskCount: Number() from accumulate(
    RiskFactor(applicantId == $appId),
    count(1)
)

// Auto-rejection with exists check
not LoanDecision(applicantId == $appId)
```

### 2. Insurance Pricing
**Domain**: Auto Insurance Underwriting

Demonstrates risk-based pricing with:
- Driver risk profiling (age, violations, claims)
- Vehicle risk assessment (age, type, value)
- Premium calculation with multiplicative factors
- Automatic decline rules for high-risk profiles

**Key Patterns**:
```drl
// Multi-factor risk accumulation
$riskFactors: Number() from accumulate(
    DriverRiskFactor(applicationId == $appId, $factor: factor),
    sum($factor)
)
```

### 3. Fraud Detection
**Domain**: Transaction Monitoring / Security

Demonstrates Complex Event Processing (CEP) with:
- Real-time transaction analysis via entry points
- Temporal reasoning for velocity checks
- Geographic anomaly detection
- Alert escalation based on signal accumulation

**Key Patterns**:
```drl
// Temporal velocity check
$txn: Transaction() from entry-point "transaction-stream"
$count: Number(intValue > 5) from accumulate(
    Transaction(timestamp > ($ts - 60000)) from entry-point "transaction-stream",
    count(1)
)

// Alert escalation
$signalCount: Number(intValue >= 3) from accumulate(
    FraudSignal(transactionId == $txnId),
    count(1)
)
```

### 4. Order Fulfillment
**Domain**: E-commerce / Logistics

Demonstrates order processing workflow with:
- Multi-warehouse inventory validation
- Cascading discount calculations (loyalty, bulk, promo)
- Shipping method selection with activation groups
- Fulfillment routing to optimal warehouse

**Key Patterns**:
```drl
// Activation group for exclusive shipping rules
activation-group "shipping-cost"

// Universal quantification
forall(
    $item: OrderItem(orderId == $orderId)
    FulfillmentPlan(orderId == $orderId, itemId == $item.itemId)
)
```

### 5. Travel Booking
**Domain**: Travel Industry

Demonstrates multi-service orchestration with:
- Flight pricing with loyalty discounts
- Hotel matching and package bundling
- Visa requirement validation
- Points redemption calculations

**Key Patterns**:
```drl
// Multi-entity join across services
$req: TravelRequest($reqId: requestId, $dest: destination)
$flight: FlightOption(destination == $dest, seatsAvailable >= $travelers)
$hotel: HotelOption(destinationCity == $dest, stars >= $stars)
```

## Additional Resources

### Sample Data Files

| File | Description |
|------|-------------|
| `ecommerce-sample-data.json` | Generic e-commerce entities |
| `iot-sensor-sample-data.json` | IoT sensor readings for event processing |
| `JMESPATH_EXAMPLES.md` | JMESPath query examples for JSON data |

### Related Documentation

- [DSL Reference](../dsl.md) - Complete DRL syntax documentation
- [User Guide](../USER_GUIDE.md) - Getting started and advanced usage
- [API Reference](../API.md) - C++ API documentation

## Contributing Examples

When adding new examples, please follow this structure:

```
docs/examples/your-example/
├── README.md           # Business scenario and documentation
├── your-example.drl    # Rule definitions
└── test-data.json      # Sample test data
```

Each example should:
1. Represent a realistic business domain
2. Demonstrate specific engine features
3. Include comprehensive test scenarios
4. Document expected behavior for each test case
