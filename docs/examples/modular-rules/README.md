# Modular Rules Example

This example demonstrates how to organize rules across multiple files using the `import` statement for better maintainability and reuse.

## Directory Structure

```
modular-rules/
├── common/
│   └── types.rfl           # Shared type declarations
├── validation-rules.rfl    # Validation rules (imports common.types)
├── pricing-rules.rfl       # Pricing rules (imports common.types)
└── main.rfl                # Main entry point (imports all)
```

## Files

### common/types.rfl
Contains shared type declarations used across all rule files:
- `Customer` - Customer information with tier status
- `Order` - Order details with items and totals
- `ValidationResult` - Result of validation checks
- `PricingResult` - Calculated pricing information

### validation-rules.rfl
Validation rules that check:
- Customer has valid name and email
- Order has at least one item
- Order total is positive

### pricing-rules.rfl
Pricing rules that calculate:
- Base discount based on customer tier
- Volume discount for large orders
- Final price after all discounts

### main.rfl
Entry point that imports all modules and can add orchestration rules.

## Usage

```cpp
#include "rfl_parser.hpp"

ParsingResult result;
std::vector<std::string> base_dirs = {"./docs/examples/modular-rules"};

// Load from main entry point - imports are resolved automatically
auto kb = build_knowledge_base("main.rfl", base_dirs, result);

if (result.success) {
    auto session = kb->create_session();

    // Insert customer
    auto customer = std::make_shared<Fact>();
    customer->type = "ecommerce.Customer";
    customer->fields["id"] = (int64_t)1;
    customer->fields["name"] = "John Doe";
    customer->fields["email"] = "john@example.com";
    customer->fields["tier"] = "Gold";
    session->add_fact(customer);

    // Insert order
    auto order = std::make_shared<Fact>();
    order->type = "ecommerce.Order";
    order->fields["customerId"] = (int64_t)1;
    order->fields["itemCount"] = (int64_t)5;
    order->fields["subtotal"] = 250.0;
    session->add_fact(order);

    // Fire rules - validation and pricing rules from imported files will execute
    session->fire_all_rules();
}
```

This example is about parser/import structure, not a turnkey `capi_demo` scenario. Use it when you need multi-file rulesets with shared declarations.

## Key Concepts

1. **Shared Types**: Define types once in `common/types.rfl`, use everywhere
2. **Modular Rules**: Separate concerns into different files (validation, pricing, etc.)
3. **Single Entry Point**: Use `main.rfl` to import all needed modules
4. **Automatic Resolution**: Import paths are resolved relative to base directories
