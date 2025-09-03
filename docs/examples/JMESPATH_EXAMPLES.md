# JMESPath Query Examples for Complex Data Structures

This document provides comprehensive examples of JMESPath queries using the sample data files provided in this directory.

## Sample Data Files

- `ecommerce-sample-data.json` - Complex e-commerce order and customer data
- `iot-sensor-sample-data.json` - IoT environmental sensor network data
- `financial-transactions-sample-data.json` - Financial transaction and risk analysis data

## E-commerce Data Queries

### Basic Property Access

```javascript
// Get store name
var storeName = jmespath(ecommerceData, 'data.store_info.name');
// Result: "TechMart"

// Get reporting period  
var period = jmespath(ecommerceData, 'data.monthly_report.period');
// Result: "2024-01"
```

### Array Projections

```javascript
// Get all customer names
var customerNames = jmespath(ecommerceData, 'data.customers[*].name');
// Result: ["Alice Johnson", "Bob Smith"]

// Get all product prices
var productPrices = jmespath(ecommerceData, 'data.products[*].price');
// Result: [1999.00, 79.99]

// Get order totals
var orderTotals = jmespath(ecommerceData, 'data.orders[*].pricing.total');
// Result: [2418.15, 243.24]
```

### Complex Nested Projections

```javascript
// Get all item quantities from all orders
var itemQuantities = jmespath(ecommerceData, 'data.orders[*].items[*].quantity');
// Result: [[1, 2], [3]]

// Flatten item quantities  
var allQuantities = jmespath(ecommerceData, 'data.orders[*].items[*].quantity | sum(@)');
// Result: 6

// Get all customer email addresses
var emails = jmespath(ecommerceData, 'data.customers[*].email');
// Result: ["alice@example.com", "bob@example.com"]
```

### Array Filtering

```javascript
// Find premium customers
var premiumCustomers = jmespath(ecommerceData, 'data.customers[?membership.tier == `premium`]');

// Find high-value orders (> $1000)
var highValueOrders = jmespath(ecommerceData, 'data.orders[?pricing.total > 1000]');

// Find products in electronics category
var electronics = jmespath(ecommerceData, 'data.products[?category == `electronics`]');

// Find delivered orders
var deliveredOrders = jmespath(ecommerceData, 'data.orders[?status == `delivered`]');
```

### Aggregation Functions

```javascript
// Calculate total revenue
var totalRevenue = jmespath(ecommerceData, 'data.orders[*].pricing.total | sum(@)');
// Result: 2661.39

// Find highest order value
var maxOrder = jmespath(ecommerceData, 'data.orders[*].pricing.total | max(@)'); 
// Result: 2418.15

// Count total orders
var orderCount = jmespath(ecommerceData, 'data.orders | length(@)');
// Result: 2

// Average order value
var avgOrderValue = jmespath(ecommerceData, 'data.analytics.metrics.average_order_value');
// Result: 1330.70
```

### Complex Business Logic Queries

```javascript
// Premium customer revenue analysis
var premiumCustomerIds = jmespath(ecommerceData, 'data.customers[?membership.tier == `premium`].id');
var premiumOrders = jmespath(ecommerceData, 'data.orders[?customer_id == `CUST-001`]');
var premiumRevenue = jmespath(JSON.stringify(premiumOrders), '[*].pricing.total | sum(@)');

// Product performance metrics
var bestSellingProductId = jmespath(ecommerceData, 'data.analytics.product_performance.best_selling');
var productName = jmespath(ecommerceData, 'data.products[?id == `PROD-002`][0].name');

// Geographic analysis
var customerStates = jmespath(ecommerceData, 'data.customers[*].address.state');
// Result: ["CA", "TX"]
```

## IoT Sensor Data Queries

### Sensor Status Analysis

```javascript
// Get all online sensors
var onlineSensors = jmespath(iotData, 'data.sensors[?status == `online`]');

// Count offline sensors  
var offlineCount = jmespath(iotData, 'data.sensors[?status == `offline`] | length(@)');
// Result: 1

// Find sensors with low battery
var lowBatterySensors = jmespath(iotData, 'data.sensors[?battery_level < 20]');

// Get sensors by zone
var parkSensors = jmespath(iotData, 'data.sensors[?location.zone == `park`]');
```

### Environmental Measurements

```javascript
// Get all temperature readings
var temperatures = jmespath(iotData, 'data.sensors[?status == `online`][*].measurements.temperature');
// Result: [18.5, 19.8, 21.2, 16.8]

// Calculate average temperature
var avgTemp = jmespath(JSON.stringify(temperatures), '@ | sum(@)') / 
               jmespath(JSON.stringify(temperatures), '@ | length(@)');

// Find highest pollution readings
var pm25Levels = jmespath(iotData, 'data.sensors[?status == `online`][*].measurements.air_quality.pm25');
var maxPM25 = jmespath(JSON.stringify(pm25Levels), '@ | max(@)');
// Result: 45.8

// Get humidity readings above 70%
var highHumidity = jmespath(iotData, 'data.sensors[?measurements.humidity > 70]');
```

### Alert Analysis

```javascript
// Count total alerts across all sensors
var allAlerts = jmespath(iotData, 'data.sensors[*].alerts[*]');
var totalAlerts = jmespath(JSON.stringify(allAlerts), '@ | length(@)');

// Find critical alerts
var criticalAlerts = jmespath(iotData, 'data.sensors[*].alerts[?level == `critical`]');

// Get sensors with air quality warnings
var airQualityAlerts = jmespath(iotData, 'data.sensors[*].alerts[?type == `air_quality`]');

// Find sensors needing maintenance  
var maintenanceNeeded = jmespath(iotData, 'data.sensors[?maintenance.calibration_status == `expired`]');
```

### Geographic and Zone Analysis

```javascript
// Get unique zones
var zones = jmespath(iotData, 'data.sensors[*].location.zone');
// Result: ["financial_district", "shopping_district", "residential", "tourist_district", "park"]

// Find sensors in specific geographic area (lat/lng range)
var downtownSensors = jmespath(iotData, 'data.sensors[?location.lat > 37.77 && location.lat < 37.80]');

// Zone-based environmental summary
var financialDistrictSensors = jmespath(iotData, 'data.sensors[?location.zone == `financial_district`]');
var fdTemperature = jmespath(JSON.stringify(financialDistrictSensors), '[*].measurements.temperature | sum(@)');
```

## Advanced JMESPath Patterns

### Multi-step Processing

```javascript
// Complex e-commerce analysis
var step1 = jmespath(ecommerceData, 'data.orders[?status == `delivered`]');
var step2 = jmespath(JSON.stringify(step1), '[*].items[*]');
var step3 = jmespath(JSON.stringify(step2), '[*].final_price | sum(@)');
// Final result: Total revenue from delivered orders

// IoT sensor health score calculation
var onlineSensors = jmespath(iotData, 'data.sensors[?status == `online`]');
var batteryScores = jmespath(JSON.stringify(onlineSensors), '[*].battery_level | sum(@)');
var avgBatteryHealth = batteryScores / jmespath(JSON.stringify(onlineSensors), '@ | length(@)');
```

### Conditional Logic with JMESPath

```javascript
// Business rule: Identify VIP customers
var vipCondition = jmespath(ecommerceData, 'data.customers[?membership.tier == `premium` && membership.points > 10000]');

// Environmental alert thresholds
var dangerousAirQuality = jmespath(iotData, 'data.sensors[?measurements.air_quality.pm25 > 35]');
var noisePollution = jmespath(iotData, 'data.sensors[?measurements.noise_level > 70]');

// Combined conditions
var problematicSensors = jmespath(iotData, 'data.sensors[?battery_level < 20 || status == `offline`]');
```

### Performance Optimization Patterns

```javascript
// Efficient data extraction
var orderData = jmespath(ecommerceData, 'data.orders');
var orderDataStr = JSON.stringify(orderData);

// Reuse extracted data for multiple queries
var totalRevenue = jmespath(orderDataStr, '[*].pricing.total | sum(@)');
var orderCount = jmespath(orderDataStr, '@ | length(@)');
var avgValue = totalRevenue / orderCount;

// Cache complex queries
var activeCustomerData = jmespath(ecommerceData, 'data.customers[?membership.tier == `premium`]');
var activeCustomerStr = JSON.stringify(activeCustomerData);
var premiumCustomerCount = jmespath(activeCustomerStr, '@ | length(@)');
var avgPremiumPoints = jmespath(activeCustomerStr, '[*].membership.points | sum(@)') / premiumCustomerCount;
```

## Real-world Business Rules Examples

### E-commerce Business Intelligence

```drl
rule "High Value Customer Analysis"
when
    $data : EcommerceReport(period == "2024-01")
then
    // Identify premium customers with high lifetime value
    var premiumCustomers = jmespath($data.json, 'data.customers[?membership.tier == `premium`]');
    var premiumCount = jmespath(JSON.stringify(premiumCustomers), '@ | length(@)');
    
    // Calculate their contribution to revenue
    var allOrders = jmespath($data.json, 'data.orders');
    var premiumRevenue = 0;
    
    // Get premium customer IDs
    var premiumIds = jmespath(JSON.stringify(premiumCustomers), '[*].id');
    
    // Complex revenue calculation for premium customers
    var totalRevenue = jmespath($data.json, 'data.orders[*].pricing.total | sum(@)');
    var premiumPercentage = premiumRevenue / totalRevenue * 100;
    
    console.log("Premium customers represent", premiumPercentage.toFixed(2), "% of revenue");
    
    if (premiumPercentage > 60) {
        drools.insert({
            type: "BusinessInsight",
            category: "customer_analysis", 
            insight: "premium_customer_dominance",
            value: premiumPercentage,
            recommendation: "Focus marketing spend on premium customer retention"
        });
    }
end
```

### IoT Environmental Monitoring

```drl
rule "Environmental Risk Assessment"
when
    $sensors : IoTSensorBatch(location == "downtown")
then
    // Air quality assessment
    var pm25Readings = jmespath($sensors.data, 'data.sensors[?status == `online`][*].measurements.air_quality.pm25');
    var avgPM25 = jmespath(JSON.stringify(pm25Readings), '@ | sum(@)') / 
                   jmespath(JSON.stringify(pm25Readings), '@ | length(@)');
    
    // Temperature extremes
    var temperatures = jmespath($sensors.data, 'data.sensors[?status == `online`][*].measurements.temperature');
    var maxTemp = jmespath(JSON.stringify(temperatures), '@ | max(@)');
    var minTemp = jmespath(JSON.stringify(temperatures), '@ | min(@)');
    
    // Noise pollution check
    var noisyAreas = jmespath($sensors.data, 'data.sensors[?measurements.noise_level > 70]');
    var noiseViolations = jmespath(JSON.stringify(noisyAreas), '@ | length(@)');
    
    // Sensor health monitoring
    var offlineSensors = jmespath($sensors.data, 'data.sensors[?status == `offline`] | length(@)');
    var lowBatterySensors = jmespath($sensors.data, 'data.sensors[?battery_level < 20] | length(@)');
    
    console.log("Environmental Assessment:");
    console.log("- Average PM2.5:", avgPM25.toFixed(2), "μg/m³");
    console.log("- Temperature range:", minTemp, "°C to", maxTemp, "°C");
    console.log("- Noise violations:", noiseViolations, "locations");
    console.log("- Network health: ", offlineSensors, "offline,", lowBatterySensors, "low battery");
    
    // Generate alerts based on thresholds
    if (avgPM25 > 35) {
        drools.insert({
            type: "EnvironmentalAlert",
            severity: "warning",
            parameter: "air_quality_pm25",
            value: avgPM25,
            threshold: 35,
            affected_area: "downtown"
        });
    }
    
    if (noiseViolations > 2) {
        drools.insert({
            type: "EnvironmentalAlert", 
            severity: "info",
            parameter: "noise_pollution",
            value: noiseViolations,
            threshold: 2,
            affected_area: "downtown"
        });
    }
end
```

This comprehensive set of examples demonstrates the power and flexibility of JMESPath integration with the Drills rules engine, enabling complex data analysis and business rule implementation with concise, readable syntax.