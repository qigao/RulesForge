# Travel Booking Rules Example

A travel agency booking system demonstrating multi-service orchestration, eligibility checks, dynamic pricing, and visa requirement validation.

## Business Scenario

A travel agency needs automated booking processing with:

1. **Flight Selection** - Route optimization, fare class rules
2. **Hotel Matching** - Availability, star rating, location
3. **Package Bundling** - Flight + hotel discounts
4. **Visa Requirements** - Destination-based eligibility
5. **Travel Insurance** - Risk-based pricing
6. **Loyalty Rewards** - Points earning and redemption

## Data Model

```
TravelRequest
├── requestId: String
├── customerId: String
├── loyaltyTier: String ("member", "silver", "gold", "platinum")
├── loyaltyPoints: int
├── origin: String (airport code)
├── destination: String (airport code)
├── departureDate: long
├── returnDate: long
├── travelers: int
├── cabinClass: String ("economy", "premium_economy", "business", "first")
├── hotelStars: int (1-5)
├── includeInsurance: boolean
└── nationality: String (country code)

FlightOption
├── flightId: String
├── airline: String
├── origin: String
├── destination: String
├── departureTime: long
├── arrivalTime: long
├── cabinClass: String
├── basePrice: double
├── seatsAvailable: int
└── aircraft: String

HotelOption
├── hotelId: String
├── name: String
├── city: String
├── stars: int
├── roomType: String
├── pricePerNight: double
├── availableRooms: int
├── amenities: String[]
└── distanceToCenter: double

VisaRequirement
├── nationality: String
├── destination: String
├── required: boolean
├── visaType: String
├── processingDays: int
└── cost: double

// Derived Facts

FlightQuote (derived)
├── requestId: String
├── flightId: String
├── basePrice: double
├── taxes: double
├── loyaltyDiscount: double
├── totalPrice: double
└── pointsEarned: int

HotelQuote (derived)
├── requestId: String
├── hotelId: String
├── nights: int
├── pricePerNight: double
├── totalPrice: double
├── loyaltyDiscount: double
└── pointsEarned: int

PackageQuote (derived)
├── requestId: String
├── flightQuoteId: String
├── hotelQuoteId: String
├── packageDiscount: double
├── insuranceCost: double
├── visaCost: double
├── totalPrice: double
├── totalPointsEarned: int
└── pointsRedeemed: int

TravelAlert (derived)
├── requestId: String
├── alertType: String
├── severity: String
├── message: String
└── actionRequired: boolean

BookingDecision (derived)
├── requestId: String
├── canBook: boolean
├── issues: String[]
├── totalPrice: double
└── estimatedPointsEarned: int
```

## Business Rules Summary

### Flight Pricing Rules
| Rule | Condition | Effect |
|------|-----------|--------|
| Peak Season | Dec 15 - Jan 5, Jun 15 - Aug 31 | +25% fare |
| Advance Purchase | >30 days ahead | -10% fare |
| Last Minute | <7 days ahead | +15% fare |
| Weekend Departure | Fri-Sun departure | +5% fare |

### Loyalty Discounts
| Tier | Flight Discount | Hotel Discount | Points Multiplier |
|------|-----------------|----------------|-------------------|
| Member | 0% | 0% | 1x |
| Silver | 5% | 5% | 1.5x |
| Gold | 10% | 10% | 2x |
| Platinum | 15% | 15% | 3x |

### Package Bundle Rules
| Rule | Condition | Discount |
|------|-----------|----------|
| Flight + Hotel | Same destination | 10% off hotel |
| Flight + Hotel + Insurance | Complete package | 15% off hotel + free insurance upgrade |
| Points Redemption | >10,000 points | Redeem for up to $100 credit |

### Visa Requirements
| Route | Visa Required | Processing Time |
|-------|---------------|-----------------|
| US → EU (Schengen) | No (ESTA-like) | N/A |
| US → UK | No (ETA) | 3 days |
| US → China | Yes | 10 business days |
| US → Brazil | No | N/A |
| US → Australia | Yes (eVisitor) | 1 day |
| US → India | Yes | 7 business days |

## Running the Example

### Quick Run with capi_demo

```bash
capi_demo \
  -d docs/examples/travel-booking/travel-booking.rfl \
  -j docs/examples/travel-booking/travel-test-data.json \
  -m visaRequirements:com.travel.VisaRequirement \
  -m flightOptions:com.travel.FlightOption \
  -m hotelOptions:com.travel.HotelOption \
  -q PackageQuotes \
  -b quote \
  -f requestId,totalPrice,totalPointsEarned,packageDiscount
```

### C++ Integration

```cpp
#include "knowledge_base.hpp"

int main() {
    ParseResult result;
    auto kb = build_knowledge_base_from_file("travel-booking.rfl", result);
    auto session = kb->create_session();

    // Load visa requirements
    auto visaUS_FR = std::make_shared<Fact>();
    visaUS_FR->type = "com.travel.VisaRequirement";
    visaUS_FR->fields["nationality"] = "US";
    visaUS_FR->fields["destination"] = "FR";
    visaUS_FR->fields["required"] = static_cast<int64_t>(0);
    session->add_fact(visaUS_FR);

    // Load flight options
    auto flight = std::make_shared<Fact>();
    flight->type = "com.travel.FlightOption";
    flight->fields["flightId"] = "AA100";
    flight->fields["airline"] = "American Airlines";
    flight->fields["origin"] = "JFK";
    flight->fields["destination"] = "CDG";
    flight->fields["cabinClass"] = "business";
    flight->fields["basePrice"] = 2500.0;
    flight->fields["seatsAvailable"] = static_cast<int64_t>(12);
    session->add_fact(flight);

    // Create travel request
    auto request = std::make_shared<Fact>();
    request->type = "com.travel.TravelRequest";
    request->fields["requestId"] = "REQ-001";
    request->fields["customerId"] = "CUST-001";
    request->fields["loyaltyTier"] = "gold";
    request->fields["loyaltyPoints"] = static_cast<int64_t>(25000);
    request->fields["origin"] = "JFK";
    request->fields["destination"] = "CDG";
    request->fields["travelers"] = static_cast<int64_t>(2);
    request->fields["cabinClass"] = "business";
    request->fields["nationality"] = "US";
    session->add_fact(request);

    session->fire_all_rules();

    auto quotes = session->execute_query("PackageQuotes");
    // Process results...

    return 0;
}
```

## Sample Scenarios

### Scenario 1: Business Trip to Paris
```
Customer: Gold member with 25,000 points
Route: JFK → CDG (Paris)
Class: Business
Hotel: 4-star, 5 nights
Insurance: Yes

Result:
- Flight: $2,500 base - 10% gold = $2,250
- Hotel: $250/night × 5 - 10% gold - 10% package = $1,012
- Insurance: $89 (included in package)
- Points redemption: 10,000 points = $100 credit
- Total: $3,251 (earned 6,500 new points)
```

### Scenario 2: Family Vacation to Tokyo
```
Customer: Silver member, US nationality
Route: LAX → NRT (Tokyo)
Travelers: 4
Class: Economy
Hotel: 3-star, 7 nights
Insurance: Yes

Result:
- Visa Alert: Japan requires visa for stays >90 days (not required for this trip)
- Flight: $1,200 × 4 = $4,800 - 5% = $4,560
- Hotel: $150/night × 7 - 5% - 10% package = $897
- Insurance: $156 (4 travelers)
- Total: $5,613
```
