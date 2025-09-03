# Drills 规则引擎 - 专业与企业指南

*为复杂的企业场景提供生产级业务规则*

## 执行摘要

Drills 是一个高性能的 C++ 规则引擎，实现了 **Rete 算法** 并集成了 JavaScript，专为需要以下功能的企业应用程序设计：

-   **高吞吐量**：每秒 10 万次以上的规则评估
-   **低延迟**：亚毫秒级规则执行
-   **内存效率**：对象池和优化的数据结构
-   **真值维护**：自动依赖跟踪和事实撤销
-   **复杂事件处理**：时间模式匹配
-   **线程安全**：不可变知识库与每线程会话

## 企业架构模式

### 1. 高吞吐量事务处理

适用于金融服务、电子商务或电信：

```drl
// 实时欺诈检测
declare Transaction
    id: String
    accountId: String
    amount: double
    merchantId: String
    timestamp: long
    location: String
end

declare FraudAlert
    transactionId: String
    reason: String
    riskScore: double
end

rule "Velocity Check"
salience 1000
when
    $txn: Transaction($accountId: accountId, $amount: amount)
    $recentAmount: Number() from accumulate(
        Transaction(
            accountId == $accountId,
            timestamp > ($txn.timestamp - 300000), // 过去 5 分钟
            id != $txn.id,
            $amt: amount
        ),
        sum($amt)
    )
    eval($recentAmount + $amount > 10000.0)
then
    drools.insert({
        type: "FraudAlert",
        transactionId: txn.id,
        reason: `速度: 5 分钟内 $${recentAmount + amount}`,
        riskScore: Math.min(100, (recentAmount + amount) / 100)
    });
end

rule "Geographic Anomaly"
salience 900
when
    $txn: Transaction($accountId: accountId, $location: location)
    $lastTxn: Transaction(
        accountId == $accountId,
        location != $location,
        timestamp > ($txn.timestamp - 3600000), // 过去 1 小时
        timestamp < $txn.timestamp
    )
then
    let distance = calculateDistance(txn.location, lastTxn.location);
    if (distance > 500) { // 1 小时内 500+ 英里
        drools.insert({
            type: "FraudAlert", 
            transactionId: txn.id,
            reason: `地理: ${distance} 英里，用时 ${(txn.timestamp - lastTxn.timestamp)/60000} 分钟`,
            riskScore: Math.min(100, distance / 10)
        });
    }
end
```

**生产实现：**
```cpp
class TransactionProcessor {
private:
    std::shared_ptr<KnowledgeBase> fraud_kb;
    thread_local std::unique_ptr<StatefulSession> session;
    
public:
    TransactionProcessor() {
        // 加载欺诈检测规则一次
        std::string rules = load_fraud_rules();
        ParsingResult result;
        fraud_kb = build_knowledge_base(rules, result);
        
        if (!result.success) {
            throw std::runtime_error("Failed to compile fraud rules");
        }
    }
    
    FraudCheckResult process_transaction(const TransactionData& txn_data) {
        // 线程局部会话以确保安全
        if (!session) {
            session = fraud_kb->create_session();
        }
        
        // 转换为优化事实
        auto transaction = TRANSACTION()
            .id(txn_data.id)
            .accountId(txn_data.account_id)
            .amount(txn_data.amount)
            .merchantId(txn_data.merchant_id)
            .timestamp(txn_data.timestamp)
            .location(txn_data.location)
            .build();
            
        session->add_fact(transaction);
        
        // 执行规则
        int rules_fired = session->fire_all_rules();
        
        // 检查警报
        auto alerts = session->get_facts_of_type("FraudAlert");
        FraudCheckResult result;
        result.transaction_id = txn_data.id;
        result.is_suspicious = !alerts.empty();
        
        for (const auto& alert : alerts) {
            FraudAlert fraud_alert;
            fraud_alert.reason = get_string_field(alert, "reason");
            fraud_alert.risk_score = get_double_field(alert, "riskScore");
            result.alerts.push_back(fraud_alert);
        }
        
        // 清理会话以进行下一次事务
        session->retract_facts_of_type("Transaction");
        session->retract_facts_of_type("FraudAlert");
        
        return result;
    }
};
```

### 2. 复杂审批工作流

适用于文档处理、贷款审批或合规性：

```drl
declare LoanApplication
    id: String
    applicantId: String
    amount: double
    purpose: String
    creditScore: int
    income: double
    debtToIncome: double
    status: String
end

declare ApprovalDecision
    applicationId: String
    decision: String
    reason: String
    conditions: String[]
end

declare ManualReview  
    applicationId: String
    reason: String
    assignedTo: String
end

// 级别 1: 自动批准低风险申请
rule "Auto Approve - Excellent Credit"
salience 1000
agenda-group "approval"
when
    $app: LoanApplication(
        amount <= 50000,
        creditScore >= 800,
        debtToIncome <= 0.28,
        income >= amount * 3,
        status == "UNDERWRITING"
    )
then
    drools.insert({
        type: "ApprovalDecision",
        applicationId: app.id,
        decision: "APPROVED",
        reason: "自动批准: 优秀的信用状况",
        conditions: ["适用标准条款"]
    });
    
    drools.modify(app, {status: "APPROVED"});
    
    console.log(`自动批准贷款 ${app.id}，金额为 $${app.amount}`);
end

// 级别 2: 自动拒绝高风险申请  
rule "Auto Decline - High Risk"
salience 1000
agenda-group "approval"
when
    $app: LoanApplication(
        creditScore < 600,
        status == "UNDERWRITING"
    )
    or
    $app: LoanApplication(
        debtToIncome > 0.45,
        status == "UNDERWRITING"
    )
then
    let reason = app.creditScore < 600 ? 
        `信用评分过低: ${app.creditScore}` :
        `债务收入比过高: ${Math.round(app.debtToIncome * 100)}%`;
        
    drools.insert({
        type: "ApprovalDecision",
        applicationId: app.id,
        decision: "DECLINED", 
        reason: reason,
        conditions: []
    });
    
    drools.modify(app, {status: "DECLINED"});
end

// 级别 3: 边缘情况的人工审查
rule "Require Manual Review"
salience 500
agenda-group "approval"
when
    $app: LoanApplication(
        status == "UNDERWRITING",
        amount > 100000
    )
    not ApprovalDecision(applicationId == $app.id)
    not ManualReview(applicationId == $app.id)
then
    let assignee = app.amount > 250000 ? "高级承销商" : "承销商";
    
    drools.insert({
        type: "ManualReview",
        applicationId: app.id,
        reason: `大额贷款: $${app.amount}`,
        assignedTo: assignee
    });
    
    drools.modify(app, {status: "MANUAL_REVIEW"});
end

// 边缘情况的条件批准
rule "Conditional Approval"
salience 750
agenda-group "approval"  
when
    $app: LoanApplication(
        creditScore >= 650,
        creditScore < 750,
        debtToIncome <= 0.40,
        income >= amount * 2.5,
        status == "UNDERWRITING"
    )
then
    drools.insert({
        type: "ApprovalDecision",
        applicationId: app.id,
        decision: "CONDITIONAL_APPROVAL",
        reason: "由于中等信用状况，有条件批准",
        conditions: [
            "更高利率: 优惠利率 + 2.5%",
            "需要抵押贷款保险",
            "最长 30 年期限"
        ]
    });
    
    drools.modify(app, {status: "CONDITIONAL_APPROVAL"});
end
```

### 3. 实时定价与收益优化

适用于零售、航空或 SaaS 中的动态定价：

```drl
declare Product
    id: String
    category: String
    basePrice: double
    inventory: int
    demand: double
    competitorPrice: double
end

declare PriceAdjustment
    productId: String
    oldPrice: double
    newPrice: double
    reason: String
    effectiveUntil: long
end

declare InventoryAlert
    productId: String
    currentLevel: int
    alertType: String
end

// 基于库存水平的动态定价
rule "Low Inventory - Price Increase"
salience 1000
when
    $product: Product(inventory < 50, inventory > 0, $id: id, $basePrice: basePrice)
    not PriceAdjustment(productId == $id)
then
    let scarcityMultiplier = 1 + (50 - product.inventory) / 100; // 最高增加 50%
    let newPrice = Math.round(product.basePrice * scarcityMultiplier * 100) / 100;
    
    drools.insert({
        type: "PriceAdjustment",
        productId: product.id,
        oldPrice: product.basePrice,
        newPrice: newPrice,
        reason: `库存不足: 剩余 ${product.inventory} 单位`,
        effectiveUntil: Date.now() + (24 * 60 * 60 * 1000) // 24 小时
    });
    
    drools.modify(product, {basePrice: newPrice});
end

// 竞争性定价响应  
rule "Match Competitor Pricing"
salience 900
when
    $product: Product(
        competitorPrice > 0,
        competitorPrice < basePrice * 0.95, // 便宜超过 5%
        inventory > 100, // 库存充足
        $id: id
    )
    not PriceAdjustment(productId == $id)
then
    let newPrice = Math.max(
        product.competitorPrice + 0.01, // 便宜 1 美分
        product.basePrice * 0.80 // 永不低于基础价格的 80%
    );
    
    drools.insert({
        type: "PriceAdjustment", 
        productId: product.id,
        oldPrice: product.basePrice,
        newPrice: newPrice,
        reason: `竞争性响应: 竞争对手价格为 $${product.competitorPrice}`,
        effectiveUntil: Date.now() + (6 * 60 * 60 * 1000) // 6 小时
    });
    
    drools.modify(product, {basePrice: newPrice});
end

// 基于需求的定价
rule "High Demand - Premium Pricing"
salience 800
when
    $product: Product(
        demand > 2.0, // 正常需求的 200%
        inventory > 200, // 库存良好
        $id: id,
        $basePrice: basePrice
    )
    not PriceAdjustment(productId == $id)
then
    let demandMultiplier = Math.min(1.25, 1 + (product.demand - 1) * 0.1); // 最高增加 25%
    let newPrice = Math.round(product.basePrice * demandMultiplier * 100) / 100;
    
    drools.insert({
        type: "PriceAdjustment",
        productId: product.id, 
        oldPrice: product.basePrice,
        newPrice: newPrice,
        reason: `高需求: 正常需求的 ${Math.round(product.demand * 100)}%`,
        effectiveUntil: Date.now() + (12 * 60 * 60 * 1000) // 12 小时
    });
    
    drools.modify(product, {basePrice: newPrice});
end
```

## 企业性能优化

### 内存优化事实创建

```cpp
// 使用带对象池的类型化构建器，适用于高吞吐量场景
class OptimizedTransactionProcessor {
private:
    static constexpr size_t BATCH_SIZE = 1000;
    std::vector<std::shared_ptr<Fact>> transaction_batch;
    
public:
    void process_transaction_batch(const std::vector<TransactionData>& transactions) {
        transaction_batch.clear();
        transaction_batch.reserve(BATCH_SIZE);
        
        // 使用优化事实构建器
        for (const auto& txn : transactions) {
            auto fact = FAST_TRANSACTION()
                .id(txn.id)
                .accountId(txn.account_id)
                .amount(txn.amount)
                .timestamp(txn.timestamp)
                .build();
                
            transaction_batch.push_back(fact);
        }
        
        // 批量插入以获得更好的性能
        session->add_facts(transaction_batch);
        session->fire_all_rules();
        
        // 处理结果...
    }
};
```

### 规则性能监控

```cpp
class ProductionRulesEngine {
private:
    std::shared_ptr<KnowledgeBase> kb;
    thread_local std::unique_ptr<StatefulSession> session;
    std::shared_ptr<MetricsCollector> metrics;
    
public:
    ProcessingResult process_with_monitoring(const InputData& data) {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        if (!session) {
            session = kb->create_session();
            session->enable_tracing(true);
        }
        
        // 处理事实
        session->add_fact(convert_to_fact(data));
        int rules_fired = session->fire_all_rules();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        // 收集指标
        metrics->record_processing_time(duration.count());
        metrics->record_rules_fired(rules_fired);
        
        // 检查慢规则
        auto performance_summary = session->get_rule_performance_summary();
        for (const auto& [rule_name, stats] : performance_summary) {
            if (stats.average_time_ms > 5.0) { // 标记慢规则
                metrics->record_slow_rule(rule_name, stats.average_time_ms);
            }
        }
        
        return extract_results(session);
    }
};
```

## 企业集成模式

### 1. 事件驱动架构

```cpp
class RulesEventProcessor {
private:
    std::shared_ptr<EventBus> event_bus;
    std::shared_ptr<KnowledgeBase> kb;
    
public:
    void setup_event_handlers() {
        event_bus->subscribe<TransactionEvent>([this](const TransactionEvent& event) {
            auto session = kb->create_session();
            
            // 将事件转换为事实
            auto transaction_fact = EVENT_TO_TRANSACTION(event).build();
            session->add_fact(transaction_fact);
            
            // 处理规则
            session->fire_all_rules();
            
            // 发布结果
            auto alerts = session->get_facts_of_type("FraudAlert");
            for (const auto& alert : alerts) {
                FraudAlertEvent fraud_event;
                fraud_event.transaction_id = get_string_field(alert, "transactionId");
                fraud_event.reason = get_string_field(alert, "reason");
                fraud_event.risk_score = get_double_field(alert, "riskScore");
                
                event_bus->publish(fraud_event);
            }
        });
    }
};
```

### 2. REST API 集成

```cpp
class RulesAPIController {
private:
    std::shared_ptr<KnowledgeBase> pricing_kb;
    std::shared_ptr<KnowledgeBase> approval_kb;
    
public:
    // POST /api/pricing/calculate
    httplib::Server::Handler calculate_pricing = [this](const httplib::Request& req, httplib::Response& res) {
        try {
            PricingRequest pricing_req = json::parse(req.body);
            
            auto session = pricing_kb->create_session();
            
            // 添加产品事实
            for (const auto& product_data : pricing_req.products) {
                auto product = PRODUCT()
                    .id(product_data.id)
                    .category(product_data.category)
                    .basePrice(product_data.base_price)
                    .inventory(product_data.inventory)
                    .demand(product_data.demand)
                    .competitorPrice(product_data.competitor_price)
                    .build();
                    
                session->add_fact(product);
            }
            
            // 执行定价规则
            session->fire_all_rules();
            
            // 提取价格调整
            PricingResponse response;
            auto adjustments = session->get_facts_of_type("PriceAdjustment");
            
            for (const auto& adj : adjustments) {
                PriceUpdate update;
                update.product_id = get_string_field(adj, "productId");
                update.old_price = get_double_field(adj, "oldPrice");
                update.new_price = get_double_field(adj, "newPrice");
                update.reason = get_string_field(adj, "reason");
                update.effective_until = get_long_field(adj, "effectiveUntil");
                
                response.price_updates.push_back(update);
            }
            
            res.set_content(json(response).dump(), "application/json");
            
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    };
};
```

## 安全与合规

### 1. 审计跟踪实现

```cpp
class AuditableRulesEngine {
private:
    std::shared_ptr<AuditLogger> audit_logger;
    std::shared_ptr<KnowledgeBase> kb;
    
public:
    ProcessingResult process_with_audit(const InputData& data, const std::string& user_id) {
        auto session = kb->create_session();
        session->enable_tracing(true);
        
        // 记录输入
        audit_logger->log_input(user_id, data);
        
        // 处理
        session->add_fact(convert_to_fact(data));
        int rules_fired = session->fire_all_rules();
        
        // 记录规则执行
        auto trace = session->get_execution_trace();
        for (const auto& entry : trace) {
            audit_logger->log_rule_execution(
                user_id,
                entry.rule_name,
                entry.timestamp,
                entry.input_facts,
                entry.output_facts
            );
        }
        
        auto results = extract_results(session);
        
        // 记录输出
        audit_logger->log_output(user_id, results);
        
        return results;
    }
};
```

### 2. 规则验证与测试

```cpp
class RuleValidationFramework {
public:
    struct ValidationResult {
        bool is_valid;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        PerformanceStats performance;
    };
    
    ValidationResult validate_rules(const std::string& drl_source) {
        ValidationResult result;
        
        // 1. 语法验证
        ParsingResult parse_result;
        auto kb = build_knowledge_base(drl_source, parse_result);
        
        if (!parse_result.success) {
            result.is_valid = false;
            for (const auto& error : parse_result.errors) {
                result.errors.push_back(error.to_string());
            }
            return result;
        }
        
        // 2. 业务逻辑验证
        auto validation_session = kb->create_session();
        
        // 添加测试事实
        add_comprehensive_test_data(validation_session);
        
        // 执行规则并监控性能
        auto start = std::chrono::high_resolution_clock::now();
        validation_session->enable_tracing(true);
        
        int rules_fired = validation_session->fire_all_rules();
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        result.performance.execution_time_ms = duration.count();
        result.performance.rules_fired = rules_fired;
        
        // 3. 检查潜在问题
        auto performance_summary = validation_session->get_rule_performance_summary();
        for (const auto& [rule_name, stats] : performance_summary) {
            if (stats.average_time_ms > 10.0) { // 标记慢规则
                result.warnings.push_back(
                    "规则 '" + rule_name + "' 运行缓慢: " + 
                    std::to_string(stats.average_time_ms) + "ms 平均"
                );
            }
            
            if (stats.execution_count == 0) {
                result.warnings.push_back(
                    "规则 '" + rule_name + "' 在验证期间从未触发"
                );
            }
        }
        
        result.is_valid = result.errors.empty();
        return result;
    }
};
```

## 部署与操作

### 配置管理

```cpp
class ProductionConfiguration {
public:
    struct RulesConfig {
        std::string rules_source_path;
        int max_rule_executions = 10000;
        bool enable_tracing = false;
        bool enable_performance_monitoring = true;
        size_t fact_cache_size = 100000;
        std::chrono::seconds rule_timeout{30};
    };
    
    static RulesConfig load_from_environment() {
        RulesConfig config;
        
        config.rules_source_path = get_env_var("DRILLS_RULES_PATH", "./rules/production.drl");
        config.max_rule_executions = get_env_int("DRILLS_MAX_EXECUTIONS", 10000);
        config.enable_tracing = get_env_bool("DRILLS_ENABLE_TRACING", false);
        config.fact_cache_size = get_env_int("DRILLS_CACHE_SIZE", 100000);
        
        return config;
    }
};
```

### 健康检查与监控

```cpp
class RulesEngineHealthCheck {
private:
    std::shared_ptr<KnowledgeBase> kb;
    std::chrono::steady_clock::time_point last_health_check;
    
public:
    HealthStatus check_health() {
        HealthStatus status;
        
        try {
            // 检查知识库编译
            if (!kb) {
                status.is_healthy = false;
                status.error_message = "知识库未初始化";
                return status;
            }
            
            // 执行轻量级规则执行测试
            auto test_session = kb->create_session();
            auto test_fact = create_health_check_fact();
            
            test_session->add_fact(test_fact);
            
            auto start = std::chrono::steady_clock::now();
            int rules_fired = test_session->fire_all_rules();
            auto end = std::chrono::steady_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            
            status.is_healthy = true;
            status.response_time_ms = duration.count();
            status.rules_fired = rules_fired;
            
            // 检查性能阈值
            if (duration.count() > 1000) { // 1 秒阈值
                status.warnings.push_back("规则执行时间超过阈值");
            }
            
        } catch (const std::exception& e) {
            status.is_healthy = false;
            status.error_message = e.what();
        }
        
        last_health_check = std::chrono::steady_clock::now();
        return status;
    }
};
```

## 企业部署最佳实践

### 1. 规则治理
- **版本控制**：将 DRL 文件存储在 Git 中，并进行适当的分支管理
- **代码审查**：所有规则更改都需要同行审查
- **测试**：全面的测试套件用于规则验证
- **预发布环境**：部署到预发布环境，然后才部署到生产环境

### 2. 性能监控
- **规则级指标**：跟踪每条规则的执行时间
- **内存使用**：监控事实缓存和对象池的使用情况
- **吞吐量跟踪**：测量每秒处理的事务数
- **警报阈值**：设置性能下降的警报

### 3. 安全注意事项
- **输入验证**：在事实创建之前清理所有外部数据
- **规则隔离**：为不同的安全上下文分离规则库
- **审计日志**：记录所有规则执行以符合合规性
- **访问控制**：基于角色的规则管理访问

### 4. 可伸缩性模式
- **水平扩展**：多个引擎实例与负载均衡
- **缓存策略**：缓存已编译的知识库
- **异步处理**：使用事件队列进行非实时处理
- **数据库集成**：从企业数据库高效加载事实

---

**准备好实施了吗？** 请参阅 [用户指南](USER_GUIDE.md) 以扩展引擎，以及 [部署指南](DEPLOYMENT.md) 以获取完整的技术文档。
