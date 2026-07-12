# Fraud Detection Rules Example

A real-time transaction monitoring system demonstrating Complex Event Processing (CEP), pattern detection, and risk scoring using temporal operators.

## Business Scenario

A financial institution needs real-time fraud detection with:

1. **Velocity Checks** - Multiple transactions in short time periods
2. **Geographic Anomalies** - Transactions from impossible locations
3. **Amount Anomalies** - Unusual spending patterns
4. **Behavioral Analysis** - Deviation from normal patterns
5. **Risk Scoring** - Aggregate multiple risk signals
6. **Alert Generation** - Automated response triggers

## Data Model

```
Transaction
├── transactionId: String
├── accountId: String
├── cardNumber: String (masked)
├── amount: double
├── currency: String
├── merchantId: String
├── merchantCategory: String
├── merchantCountry: String
├── merchantCity: String
├── channel: String ("pos", "online", "atm", "mobile")
├── timestamp: long (Unix ms)
├── latitude: double
├── longitude: double
└── ipAddress: String

AccountProfile
├── accountId: String
├── accountType: String
├── avgMonthlySpend: double
├── avgTransactionAmount: double
├── primaryCountry: String
├── trustedMerchants: String[]
├── lastKnownLatitude: double
├── lastKnownLongitude: double
└── lastTransactionTime: long

FraudSignal (derived)
├── transactionId: String
├── signalType: String
├── severity: String ("Low", "Medium", "High", "Critical")
├── score: int
├── description: String
└── timestamp: long

FraudAlert (derived)
├── alertId: String
├── transactionId: String
├── accountId: String
├── totalScore: int
├── riskLevel: String
├── signals: String[]
├── action: String ("allow", "review", "block", "freeze")
└── timestamp: long
```

## Detection Rules

### Velocity-Based Detection
| Rule | Condition | Score |
|------|-----------|-------|
| Rapid Fire | >5 transactions in 1 minute | 40 |
| High Frequency | >10 transactions in 10 minutes | 30 |
| ATM Hammering | >3 ATM withdrawals in 30 minutes | 35 |

### Geographic Detection
| Rule | Condition | Score |
|------|-----------|-------|
| Impossible Travel | >500 miles in <1 hour | 50 |
| New Country | First transaction in new country | 25 |
| High Risk Country | Transaction from blacklisted country | 45 |

### Amount Detection
| Rule | Condition | Score |
|------|-----------|-------|
| Large Amount | >3x average transaction | 20 |
| Round Amount | Large round number (testing card) | 15 |
| Just Below Limit | Amount just under reporting threshold | 30 |

### Risk Score Actions
| Total Score | Risk Level | Action |
|-------------|------------|--------|
| 0-25 | Low | Allow |
| 26-50 | Medium | Allow + Log |
| 51-75 | High | Review Required |
| 76-100 | Very High | Block + Alert |
| >100 | Critical | Block + Freeze + Alert |

## CEP Features Demonstrated

This example showcases:
- **Temporal operators**: `after`, `before`, `within`
- **Entry points**: `transaction-stream`
- **Accumulate patterns**: Counting events in time windows
- **Sliding windows**: Rolling time-based aggregations
- **Pattern matching**: Detecting sequences of events

## Running the Example

### Run with the C++ Example

This example needs stream insertion, so the repo now provides a dedicated C++ runner.

First flatten the sample data:

```bash
python tools/flatten_example_data.py \
  fraud \
  docs/examples/fraud-detection/fraud-test-data.json \
  docs/examples/fraud-detection/fraud-flat.json
```

Then run the dedicated example runner:

```bash
./build/bin/fraud_stream_runner \
  docs/examples/fraud-detection/fraud-detection.rfl \
  docs/examples/fraud-detection/fraud-flat.json
```

The runner outputs a final alert view rather than the raw working-memory rows:

- it consolidates duplicate `FraudAlert` facts by transaction and keeps the highest-severity outcome
- it computes the displayed `totalScore` from the emitted `FraudSignal` facts
- it prints one line per transaction in the form `transactionId | accountId | totalScore | riskLevel | action`

Why it does not use the batch-oriented `capi_demo`:

- transactions must be inserted into `entry-point "transaction-stream"`
- the runner performs domain-specific flattening and alert consolidation

The public continuous C API can commit one schema-bound JSON event or a
path-selected JSON/CSV/XML event batch to a named entry point. The existing
example runner instead does two domain-specific jobs:

1. flatten or extract scenario transactions from the sample JSON
2. insert those `Transaction` facts into the `transaction-stream` entry point via C++

Treat this example as a CEP reference rule set with a dedicated runner. See
[`../../CONTINUOUS_RULE_ENGINE_DESIGN.md`](../../CONTINUOUS_RULE_ENGINE_DESIGN.md)
for the continuous C API contract.

## Sample Fraud Scenarios

### Scenario 1: Card Testing Attack
```
TXN-001: $1.00 at MerchantA (testing if card works)
TXN-002: $1.00 at MerchantB (10 seconds later)
TXN-003: $500.00 at MerchantC (20 seconds later)
→ ALERT: Velocity + Round Amount + Escalating Pattern
```

### Scenario 2: Impossible Travel
```
TXN-001: $50.00 in San Francisco, CA
TXN-002: $200.00 in London, UK (30 minutes later)
→ ALERT: 5,300 miles in 30 minutes = impossible
```

### Scenario 3: Account Takeover
```
Profile: Avg spend $100, Primary country US
TXN-001: $5,000 in Nigeria
TXN-002: $5,000 in Nigeria (2 minutes later)
TXN-003: $4,999 in Nigeria (5 minutes later - just under limit)
→ ALERT: Amount anomaly + New country + High risk country + Velocity
```
