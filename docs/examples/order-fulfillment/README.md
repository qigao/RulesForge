# Order Fulfillment Rules Example

An e-commerce order processing system demonstrating inventory management, shipping optimization, discount calculation, and fulfillment workflow automation.

## Business Scenario

An e-commerce platform needs automated order processing with:

1. **Inventory Validation** - Check stock availability across warehouses
2. **Pricing & Discounts** - Apply promotional rules and loyalty discounts
3. **Shipping Selection** - Optimize carrier and method based on criteria
4. **Fulfillment Routing** - Select optimal warehouse for shipping
5. **Order Splitting** - Handle multi-warehouse fulfillment
6. **Priority Processing** - VIP customer fast-tracking

## Data Model

```
Order
├── orderId: String
├── customerId: String
├── customerTier: String ("standard", "silver", "gold", "platinum")
├── orderDate: long
├── shippingAddress: Object
│   ├── country: String
│   ├── state: String
│   ├── city: String
│   └── zipCode: String
├── requestedDeliveryDate: long
├── giftOrder: boolean
└── expressShipping: boolean

OrderItem
├── itemId: String
├── orderId: String
├── productId: String
├── productName: String
├── quantity: int
├── unitPrice: double
├── category: String
└── weight: double (lbs)

Inventory
├── warehouseId: String
├── productId: String
├── quantityAvailable: int
├── quantityReserved: int
└── reorderPoint: int

Warehouse
├── warehouseId: String
├── name: String
├── state: String
├── zipCode: String
├── latitude: double
├── longitude: double
└── shippingCapabilities: String[] ("ground", "express", "overnight", "international")

Promotion
├── promoId: String
├── promoCode: String
├── discountType: String ("percent", "fixed", "bogo")
├── discountValue: double
├── minOrderAmount: double
├── validCategories: String[]
├── startDate: long
└── endDate: long

// Derived Facts

OrderValidation (derived)
├── orderId: String
├── isValid: boolean
├── issues: String[]
└── validatedAt: long

PricingResult (derived)
├── orderId: String
├── subtotal: double
├── discountAmount: double
├── shippingCost: double
├── taxAmount: double
├── totalAmount: double
└── appliedPromotions: String[]

FulfillmentPlan (derived)
├── orderId: String
├── warehouseId: String
├── carrier: String
├── shippingMethod: String
├── estimatedDelivery: long
├── shippingCost: double
└── itemIds: String[]

OrderStatus (derived)
├── orderId: String
├── status: String
├── message: String
└── timestamp: long
```

## Business Rules Summary

### Inventory Rules
| Rule | Condition | Action |
|------|-----------|--------|
| Stock Check | quantity > available | Flag insufficient stock |
| Reserve Stock | valid order | Reduce available, increase reserved |
| Low Stock Alert | available < reorderPoint | Generate reorder alert |

### Discount Rules
| Rule | Condition | Discount |
|------|-----------|----------|
| Platinum Loyalty | tier = "platinum" | 15% off |
| Gold Loyalty | tier = "gold" | 10% off |
| Silver Loyalty | tier = "silver" | 5% off |
| Bulk Order | quantity >= 10 | 10% off item |
| Free Shipping | subtotal >= $75 | Free ground shipping |

### Shipping Rules
| Rule | Condition | Method |
|------|-----------|--------|
| Express Request | expressShipping = true | 2-day delivery |
| Gift Order | giftOrder = true | Priority handling |
| Heavy Items | weight > 50 lbs | Freight carrier |
| International | country != "US" | International carrier |

### Warehouse Selection
| Priority | Criteria |
|----------|----------|
| 1 | Closest warehouse with full inventory |
| 2 | Warehouse in same state |
| 3 | Split order across warehouses |

## Running the Example

### Quick Run with capi_demo

```bash
capi_demo \
  -d docs/examples/order-fulfillment/order-fulfillment.drl \
  -j docs/examples/order-fulfillment/order-test-data.json \
  -m warehouses:com.ecommerce.Warehouse \
  -m inventory:com.ecommerce.Inventory \
  -m products:com.ecommerce.Product \
  -q FulfillmentPlans \
  -b plan \
  -f orderId,warehouseId,carrier,shippingMethod,shippingCost
```

### C++ Integration

```cpp
#include "knowledge_base.hpp"

int main() {
    ParseResult result;
    auto kb = build_knowledge_base_from_file("order-fulfillment.drl", result);
    auto session = kb->create_session();

    // Load warehouse data
    auto warehouse = std::make_shared<Fact>();
    warehouse->type = "com.ecommerce.Warehouse";
    warehouse->fields["warehouseId"] = "WH-WEST";
    warehouse->fields["name"] = "West Coast Distribution";
    warehouse->fields["state"] = "CA";
    warehouse->fields["zipCode"] = "90001";
    session->add_fact(warehouse);

    // Load inventory
    auto inventory = std::make_shared<Fact>();
    inventory->type = "com.ecommerce.Inventory";
    inventory->fields["warehouseId"] = "WH-WEST";
    inventory->fields["productId"] = "PROD-001";
    inventory->fields["quantityAvailable"] = static_cast<int64_t>(100);
    session->add_fact(inventory);

    // Create order
    auto order = std::make_shared<Fact>();
    order->type = "com.ecommerce.Order";
    order->fields["orderId"] = "ORD-2024-001";
    order->fields["customerId"] = "CUST-001";
    order->fields["customerTier"] = "gold";
    order->fields["expressShipping"] = static_cast<int64_t>(0);
    session->add_fact(order);

    // Add order item
    auto item = std::make_shared<Fact>();
    item->type = "com.ecommerce.OrderItem";
    item->fields["itemId"] = "ITEM-001";
    item->fields["orderId"] = "ORD-2024-001";
    item->fields["productId"] = "PROD-001";
    item->fields["quantity"] = static_cast<int64_t>(2);
    item->fields["unitPrice"] = 49.99;
    session->add_fact(item);

    session->fire_all_rules();

    // Query fulfillment plan
    auto plans = session->execute_query("FulfillmentPlans");
    // Process results...

    return 0;
}
```
