# JMESPath 复杂数据结构查询示例

本文档提供了使用此目录中提供的示例数据文件进行 JMESPath 查询的全面示例。

## 示例数据文件

- `ecommerce-sample-data.json` - 复杂的电子商务订单和客户数据
- `iot-sensor-sample-data.json` - 物联网环境传感器网络数据
- `financial-transactions-sample-data.json` - 金融交易和风险分析数据

## 电子商务数据查询

### 基本属性访问

```javascript
// 获取商店名称
var storeName = jmespath(ecommerceData, 'data.store_info.name');
// 结果: "TechMart"

// 获取报告周期
var period = jmespath(ecommerceData, 'data.monthly_report.period');
// 结果: "2024-01"
```

### 数组投影

```javascript
// 获取所有客户名称
var customerNames = jmespath(ecommerceData, 'data.customers[*].name');
// 结果: ["Alice Johnson", "Bob Smith"]

// 获取所有产品价格
var productPrices = jmespath(ecommerceData, 'data.products[*].price');
// 结果: [1999.00, 79.99]

// 获取订单总额
var orderTotals = jmespath(ecommerceData, 'data.orders[*].pricing.total');
// 结果: [2418.15, 243.24]
```

### 复杂嵌套投影

```javascript
// 获取所有订单中的所有商品数量
var itemQuantities = jmespath(ecommerceData, 'data.orders[*].items[*].quantity');
// 结果: [[1, 2], [3]]

// 展平商品数量
var allQuantities = jmespath(ecommerceData, 'data.orders[*].items[*].quantity | sum(@)');
// 结果: 6

// 获取所有客户电子邮件地址
var emails = jmespath(ecommerceData, 'data.customers[*].email');
// 结果: ["alice@example.com", "bob@example.com"]
```

### 数组过滤

```javascript
// 查找高级客户
var premiumCustomers = jmespath(ecommerceData, 'data.customers[?membership.tier == `premium`]');

// 查找高价值订单 (> $1000)
var highValueOrders = jmespath(ecommerceData, 'data.orders[?pricing.total > 1000]');

// 查找电子产品类别的产品
var electronics = jmespath(ecommerceData, 'data.products[?category == `electronics`]');

// 查找已交付订单
var deliveredOrders = jmespath(ecommerceData, 'data.orders[?status == `delivered`]');
```

### 聚合函数

```javascript
// 计算总收入
var totalRevenue = jmespath(ecommerceData, 'data.orders[*].pricing.total | sum(@)');
// 结果: 2661.39

// 查找最高订单价值
var maxOrder = jmespath(ecommerceData, 'data.orders[*].pricing.total | max(@)');
// 结果: 2418.15

// 计算总订单数
var orderCount = jmespath(ecommerceData, 'data.orders | length(@)');
// 结果: 2

// 平均订单价值
var avgOrderValue = jmespath(ecommerceData, 'data.analytics.metrics.average_order_value');
// 结果: 1330.70
```

### 复杂业务逻辑查询

```javascript
// 高级客户收入分析
var premiumCustomerIds = jmespath(ecommerceData, 'data.customers[?membership.tier == `premium`].id');
var premiumOrders = jmespath(ecommerceData, 'data.orders[?customer_id == `CUST-001`]');
var premiumRevenue = jmespath(JSON.stringify(premiumOrders), '[*].pricing.total | sum(@)');

// 产品性能指标
var bestSellingProductId = jmespath(ecommerceData, 'data.analytics.product_performance.best_selling');
var productName = jmespath(ecommerceData, 'data.products[?id == `PROD-002`][0].name');

// 地理分析
var customerStates = jmespath(ecommerceData, 'data.customers[*].address.state');
// 结果: ["CA", "TX"]
```

## 物联网传感器数据查询

### 传感器状态分析

```javascript
// 获取所有在线传感器
var onlineSensors = jmespath(iotData, 'data.sensors[?status == `online`]');

// 计算离线传感器数量
var offlineCount = jmespath(iotData, 'data.sensors[?status == `offline`] | length(@)');
// 结果: 1

// 查找电池电量低的传感器
var lowBatterySensors = jmespath(iotData, 'data.sensors[?battery_level < 20]');

// 按区域获取传感器
var parkSensors = jmespath(iotData, 'data.sensors[?location.zone == `park`]');
```

### 环境测量

```javascript
// 获取所有温度读数
var temperatures = jmespath(iotData, 'data.sensors[?status == `online`][*].measurements.temperature');
// 结果: [18.5, 19.8, 21.2, 16.8]

// 计算平均温度
var avgTemp = jmespath(JSON.stringify(temperatures), '@ | sum(@)') /
               jmespath(JSON.stringify(temperatures), '@ | length(@)');

// 查找最高污染读数
var pm25Levels = jmespath(iotData, 'data.sensors[?status == `online`][*].measurements.air_quality.pm25');
var maxPM25 = jmespath(JSON.stringify(pm25Levels), '@ | max(@)');
// 结果: 45.8

// 获取湿度读数高于 70% 的传感器
var highHumidity = jmespath(iotData, 'data.sensors[?measurements.humidity > 70]');
```

### 警报分析

```javascript
// 计算所有传感器中的总警报数
var allAlerts = jmespath(iotData, 'data.sensors[*].alerts[*]');
var totalAlerts = jmespath(JSON.stringify(allAlerts), '@ | length(@)');

// 查找关键警报
var criticalAlerts = jmespath(iotData, 'data.sensors[*].alerts[?level == `critical`]');

// 获取具有空气质量警告的传感器
var airQualityAlerts = jmespath(iotData, 'data.sensors[*].alerts[?type == `air_quality`]');

// 查找需要维护的传感器
var maintenanceNeeded = jmespath(iotData, 'data.sensors[?maintenance.calibration_status == `expired`]');
```

### 地理和区域分析

```javascript
// 获取唯一区域
var zones = jmespath(iotData, 'data.sensors[*].location.zone');
// 结果: ["financial_district", "shopping_district", "residential", "tourist_district", "park"]

// 在特定地理区域（纬度/经度范围）中查找传感器
var downtownSensors = jmespath(iotData, 'data.sensors[?location.lat > 37.77 && location.lat < 37.80]');

// 基于区域的环境摘要
var financialDistrictSensors = jmespath(iotData, 'data.sensors[?location.zone == `financial_district`]');
var fdTemperature = jmespath(JSON.stringify(financialDistrictSensors), '[*].measurements.temperature | sum(@)');
```

## 高级 JMESPath 模式

### 多步处理

```javascript
// 复杂的电子商务分析
var step1 = jmespath(ecommerceData, 'data.orders[?status == `delivered`]');
var step2 = jmespath(JSON.stringify(step1), '[*].items[*]');
var step3 = jmespath(JSON.stringify(step2), '[*].final_price | sum(@)');
// 最终结果: 已交付订单的总收入

// 物联网传感器健康评分计算
var onlineSensors = jmespath(iotData, 'data.sensors[?status == `online`]');
var batteryScores = jmespath(JSON.stringify(onlineSensors), '[*].battery_level | sum(@)');
var avgBatteryHealth = batteryScores / jmespath(JSON.stringify(onlineSensors), '@ | length(@)');
```

### JMESPath 中的条件逻辑

```javascript
// 业务规则: 识别 VIP 客户
var vipCondition = jmespath(ecommerceData, 'data.customers[?membership.tier == `premium` && membership.points > 10000]');

// 环境警报阈值
var dangerousAirQuality = jmespath(iotData, 'data.sensors[?measurements.air_quality.pm25 > 35]');
var noisePollution = jmespath(iotData, 'data.sensors[?measurements.noise_level > 70]');

// 组合条件
var problematicSensors = jmespath(iotData, 'data.sensors[?battery_level < 20 || status == `offline`]');
```

### 性能优化模式

```javascript
// 高效数据提取
var orderData = jmespath(ecommerceData, 'data.orders');
var orderDataStr = JSON.stringify(orderData);

// 重复使用提取的数据进行多次查询
var totalRevenue = jmespath(orderDataStr, '[*].pricing.total | sum(@)');
var orderCount = jmespath(orderDataStr, '@ | length(@)');
var avgValue = totalRevenue / orderCount;

// 缓存复杂查询
var activeCustomerData = jmespath(ecommerceData, 'data.customers[?membership.tier == `premium`]');
var activeCustomerStr = JSON.stringify(activeCustomerData);
var premiumCustomerCount = jmespath(activeCustomerStr, '@ | length(@)');
var avgPremiumPoints = jmespath(activeCustomerStr, '[*].membership.points | sum(@)') / premiumCustomerCount;
```

## 真实世界业务规则示例

### 电子商务商业智能

```rfl
rule "High Value Customer Analysis"
when
    $data : EcommerceReport(period == "2024-01")
then
    // 识别具有高生命周期价值的高级客户
    var premiumCustomers = jmespath($data.json, 'data.customers[?membership.tier == `premium`]');
    var premiumCount = jmespath(JSON.stringify(premiumCustomers), '@ | length(@)');

    // 计算他们对收入的贡献
    var allOrders = jmespath($data.json, 'data.orders');
    var premiumRevenue = 0;

    // 获取高级客户 ID
    var premiumIds = jmespath(JSON.stringify(premiumCustomers), '[*].id');

    // 高级客户的复杂收入计算
    var totalRevenue = jmespath($data.json, 'data.orders[*].pricing.total | sum(@)');
    var premiumPercentage = premiumRevenue / totalRevenue * 100;

    console.log("高级客户占收入的", premiumPercentage.toFixed(2), "%");

    if (premiumPercentage > 60) {
        rfl.insert({
            type: "BusinessInsight",
            category: "customer_analysis",
            insight: "premium_customer_dominance",
            value: premiumPercentage,
            recommendation: "将营销支出重点放在高级客户留存上"
        });
    }
end
```

### 物联网环境监测

```rfl
rule "Environmental Risk Assessment"
when
    $sensors : IoTSensorBatch(location == "downtown")
then
    // 空气质量评估
    var pm25Readings = jmespath($sensors.data, 'data.sensors[?status == `online`][*].measurements.air_quality.pm25');
    var avgPM25 = jmespath(JSON.stringify(pm25Readings), '@ | sum(@)') /
                   jmespath(JSON.stringify(pm25Readings), '@ | length(@)');

    // 温度极值
    var temperatures = jmespath($sensors.data, 'data.sensors[?status == `online`][*].measurements.temperature');
    var maxTemp = jmespath(JSON.stringify(temperatures), '@ | max(@)');
    var minTemp = jmespath(JSON.stringify(temperatures), '@ | min(@)');

    // 噪音污染检查
    var noisyAreas = jmespath($sensors.data, 'data.sensors[?measurements.noise_level > 70]');
    var noiseViolations = jmespath(JSON.stringify(noisyAreas), '@ | length(@)');

    // 传感器健康监测
    var offlineSensors = jmespath($sensors.data, 'data.sensors[?status == `offline`] | length(@)');
    var lowBatterySensors = jmespath($sensors.data, 'data.sensors[?battery_level < 20] | length(@)');

    console.log("环境评估:");
    console.log("- 平均 PM2.5:", avgPM25.toFixed(2), "μg/m³");
    console.log("- 温度范围:", minTemp, "°C 到", maxTemp, "°C");
    console.log("- 噪音违规:", noiseViolations, "个位置");
    console.log("- 网络健康: ", offlineSensors, "离线,", lowBatterySensors, "低电量");

    // 根据阈值生成警报
    if (avgPM25 > 35) {
        rfl.insert({
            type: "EnvironmentalAlert",
            severity: "warning",
            parameter: "air_quality_pm25",
            value: avgPM25,
            threshold: 35,
            affected_area: "downtown"
        });
    }

    if (noiseViolations > 2) {
        rfl.insert({
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

这套全面的示例展示了 JMESPath 与 Drills 规则引擎集成的强大功能和灵活性，通过简洁、可读的语法实现复杂的数据分析和业务规则。