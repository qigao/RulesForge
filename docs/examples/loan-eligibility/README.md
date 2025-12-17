# Loan Eligibility Rules Example

A complete loan approval system demonstrating credit scoring, risk assessment, and automated decision-making using the Drills rule engine.

## Business Scenario

A financial institution needs to automate loan application processing with the following requirements:

1. **Credit Score Evaluation** - Categorize applicants based on credit scores
2. **Debt-to-Income Analysis** - Calculate and evaluate DTI ratios
3. **Employment Verification** - Consider employment stability
4. **Risk Assessment** - Assign risk categories based on multiple factors
5. **Loan Decision** - Approve/reject with appropriate interest rates
6. **Compliance Rules** - Ensure regulatory requirements are met

## Data Model

```
LoanApplication
├── applicantId: String
├── applicantName: String
├── requestedAmount: double
├── requestedTermMonths: int
├── purpose: String ("home", "auto", "personal", "business")
├── creditScore: int (300-850)
├── annualIncome: double
├── monthlyDebt: double
├── employmentYears: double
├── employmentType: String ("full-time", "part-time", "self-employed", "unemployed")
├── hasCollateral: boolean
├── collateralValue: double
└── existingCustomer: boolean

CreditCategory (derived)
├── applicationId: String
├── category: String ("Excellent", "Good", "Fair", "Poor", "Very Poor")
└── score: int

RiskAssessment (derived)
├── applicationId: String
├── riskLevel: String ("Low", "Medium", "High", "Very High")
├── factors: String[]
└── debtToIncomeRatio: double

LoanDecision (derived)
├── applicationId: String
├── approved: boolean
├── approvedAmount: double
├── interestRate: double
├── monthlyPayment: double
├── reason: String
└── conditions: String[]
```

## Rule Categories

### 1. Credit Categorization Rules (salience: 100)
Classify applicants into credit tiers based on FICO scores.

### 2. DTI Calculation Rules (salience: 90)
Calculate debt-to-income ratios and flag high-risk applicants.

### 3. Risk Assessment Rules (salience: 80)
Combine multiple factors to determine overall risk level.

### 4. Loan Decision Rules (salience: 70)
Make approval decisions based on risk and eligibility criteria.

### 5. Interest Rate Rules (salience: 60)
Determine appropriate interest rates based on risk profile.

### 6. Compliance Rules (salience: 50)
Ensure all regulatory requirements are met.

## Running the Example

### Quick Run with capi_demo

```bash
capi_demo \
  -d docs/examples/loan-eligibility/loan-eligibility.drl \
  -j docs/examples/loan-eligibility/loan-applications-sample.json \
  -m applications:com.bank.loan.LoanApplication \
  -q LoanDecisions \
  -b decision \
  -f applicationId,approved,approvedAmount,interestRate,reason
```

### C++ Integration

```cpp
#include "knowledge_base.hpp"

int main() {
    // Load rules
    ParseResult result;
    auto kb = build_knowledge_base_from_file("loan-eligibility.drl", result);

    // Create session
    auto session = kb->create_session();

    // Insert loan application
    auto application = std::make_shared<Fact>();
    application->type = "com.bank.loan.LoanApplication";
    application->fields["applicantId"] = "APP-2024-001";
    application->fields["applicantName"] = "John Smith";
    application->fields["requestedAmount"] = 250000.0;
    application->fields["requestedTermMonths"] = static_cast<int64_t>(360);
    application->fields["purpose"] = "home";
    application->fields["creditScore"] = static_cast<int64_t>(720);
    application->fields["annualIncome"] = 95000.0;
    application->fields["monthlyDebt"] = 1200.0;
    application->fields["employmentYears"] = 5.5;
    application->fields["employmentType"] = "full-time";
    application->fields["hasCollateral"] = static_cast<int64_t>(1);
    application->fields["collateralValue"] = 300000.0;
    application->fields["existingCustomer"] = static_cast<int64_t>(1);

    session->add_fact(application);
    session->fire_all_rules();

    // Query decision
    auto decisions = session->execute_query("LoanDecisions");
    for (auto& row : decisions) {
        if (auto decision = row.get("$decision")) {
            std::cout << "Approved: " << decision->fields.at("approved") << std::endl;
            std::cout << "Rate: " << decision->fields.at("interestRate") << "%" << std::endl;
        }
    }

    return 0;
}
```

## Expected Behavior

For the sample application (John Smith):
- Credit Score: 720 → **Good** category
- DTI: 15.2% → **Low** risk
- Employment: 5.5 years full-time → **Stable**
- Collateral: 120% LTV → **Secured**

**Expected Decision**: Approved at 6.25% APR for $250,000

## Business Rules Summary

| Rule | Condition | Action |
|------|-----------|--------|
| Excellent Credit | score >= 750 | Category = "Excellent" |
| Good Credit | score >= 700 | Category = "Good" |
| Low DTI | DTI < 20% | Risk factor: Low |
| High DTI | DTI > 43% | Auto-reject |
| Stable Employment | years >= 2 | Risk reduction |
| Collateral Boost | LTV < 80% | Rate reduction 0.25% |
| Existing Customer | existingCustomer = true | Rate reduction 0.15% |
