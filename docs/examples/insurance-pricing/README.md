# Auto Insurance Pricing Rules Example

A comprehensive auto insurance underwriting system demonstrating risk assessment, premium calculation, and policy decision-making.

## Business Scenario

An insurance company needs to automate policy pricing with:

1. **Driver Risk Profiling** - Age, experience, violation history
2. **Vehicle Risk Assessment** - Type, age, safety features
3. **Geographic Risk Factors** - Location-based adjustments
4. **Coverage Selection** - Liability, collision, comprehensive
5. **Premium Calculation** - Base rate × risk multipliers
6. **Discount Application** - Multi-policy, good driver, safety features

## Data Model

```
InsuranceApplication
├── applicationId: String
├── applicantName: String
├── dateOfBirth: String
├── licenseYears: int
├── accidentCount: int (last 5 years)
├── violationCount: int (last 3 years)
├── dui: boolean
├── vehicleYear: int
├── vehicleMake: String
├── vehicleModel: String
├── vehicleType: String ("sedan", "suv", "truck", "sports", "motorcycle")
├── antiTheft: boolean
├── airbags: boolean
├── absSystem: boolean
├── zipCode: String
├── annualMileage: int
├── garageParked: boolean
├── multiPolicy: boolean (home + auto bundle)
└── existingCustomer: boolean

DriverRiskProfile (derived)
├── applicationId: String
├── ageCategory: String ("Young", "Adult", "Senior")
├── experienceLevel: String ("New", "Moderate", "Experienced")
├── drivingRecord: String ("Clean", "Minor Issues", "Major Issues", "Uninsurable")
└── riskScore: int (0-100)

VehicleRiskProfile (derived)
├── applicationId: String
├── vehicleCategory: String ("Low Risk", "Standard", "High Risk", "Very High Risk")
├── safetyRating: String ("Excellent", "Good", "Average", "Poor")
└── theftRisk: String ("Low", "Medium", "High")

PremiumCalculation (derived)
├── applicationId: String
├── basePremium: double
├── driverMultiplier: double
├── vehicleMultiplier: double
├── locationMultiplier: double
├── discountPercent: double
├── finalPremium: double
└── monthlyPayment: double

PolicyDecision (derived)
├── applicationId: String
├── approved: boolean
├── annualPremium: double
├── coverageLevel: String
├── deductible: double
└── reason: String
```

## Risk Factor Matrix

| Factor | Low Risk | Medium Risk | High Risk |
|--------|----------|-------------|-----------|
| Age | 30-65 | 25-29, 66-75 | <25, >75 |
| Experience | >5 years | 2-5 years | <2 years |
| Accidents | 0 | 1 | 2+ |
| Violations | 0 | 1-2 | 3+ |
| Vehicle Type | Sedan, Minivan | SUV, Truck | Sports, Motorcycle |
| Vehicle Age | 1-5 years | 6-10 years | 11+ years |

## Premium Calculation Formula

```
Final Premium = Base Premium × Driver Multiplier × Vehicle Multiplier × Location Multiplier × (1 - Discount%)

Base Premium = $800 (liability only) / $1,200 (standard) / $1,800 (full coverage)

Discounts:
- Multi-policy bundle: 15%
- Good driver (no accidents/violations): 10%
- Safety features (all 3): 8%
- Low mileage (<7,500/year): 5%
- Garage parked: 3%
- Existing customer: 5%
```

## Running the Example

### Quick Run with capi_demo

```bash
capi_demo \
  -d docs/examples/insurance-pricing/insurance-pricing.drl \
  -j docs/examples/insurance-pricing/insurance-applications-sample.json \
  -m applications:com.insurance.auto.InsuranceApplication \
  -q PolicyDecisions \
  -b decision \
  -f applicationId,approved,annualPremium,coverageLevel,reason
```

### C++ Integration

```cpp
#include "knowledge_base.hpp"

int main() {
    ParseResult result;
    auto kb = build_knowledge_base_from_file("insurance-pricing.drl", result);
    auto session = kb->create_session();

    auto application = std::make_shared<Fact>();
    application->type = "com.insurance.auto.InsuranceApplication";
    application->fields["applicationId"] = "INS-2024-001";
    application->fields["applicantName"] = "John Smith";
    application->fields["dateOfBirth"] = "1985-03-15";
    application->fields["licenseYears"] = static_cast<int64_t>(12);
    application->fields["accidentCount"] = static_cast<int64_t>(0);
    application->fields["violationCount"] = static_cast<int64_t>(1);
    application->fields["dui"] = static_cast<int64_t>(0);
    application->fields["vehicleYear"] = static_cast<int64_t>(2022);
    application->fields["vehicleMake"] = "Toyota";
    application->fields["vehicleModel"] = "Camry";
    application->fields["vehicleType"] = "sedan";
    application->fields["antiTheft"] = static_cast<int64_t>(1);
    application->fields["airbags"] = static_cast<int64_t>(1);
    application->fields["absSystem"] = static_cast<int64_t>(1);
    application->fields["zipCode"] = "94105";
    application->fields["annualMileage"] = static_cast<int64_t>(12000);
    application->fields["garageParked"] = static_cast<int64_t>(1);
    application->fields["multiPolicy"] = static_cast<int64_t>(1);
    application->fields["existingCustomer"] = static_cast<int64_t>(0);

    session->add_fact(application);
    session->fire_all_rules();

    auto decisions = session->execute_query("PolicyDecisions");
    // Process results...

    return 0;
}
```

## Expected Results

For the sample application (John Smith, 39 years old):
- Driver Profile: Adult, Experienced, Minor Issues (1 violation)
- Vehicle Profile: Standard risk, Excellent safety
- Location: Urban California
- Discounts: Multi-policy (15%) + Safety (8%) + Garage (3%) = 26%

**Expected Annual Premium**: ~$1,150 for standard coverage
