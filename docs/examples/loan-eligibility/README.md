# Loan Eligibility Rules Example

A complete loan approval system demonstrating credit scoring, risk assessment, and automated decision-making with RulesForge.

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

### capi_demo Status

The sample JSON is shaped for direct loading because `applications` is a top-level array of `LoanApplication` facts. To run it through the current public C API, first add a `.schema` file for `LoanApplication`, import it from the RFL file with `import schema "...schema"`, and pass the same schema with `capi_demo -s`.

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
