# Drills Rules Engine - Professional & Enterprise Guide

*Production-grade business rules for complex enterprise scenarios*

## Executive Summary

Drills is a high-performance C++ rules engine implementing the **Rete algorithm** with JavaScript integration, designed for enterprise applications requiring:

- **High Throughput**: 100K+ rule evaluations/second
- **Low Latency**: Sub-millisecond rule execution
- **Memory Efficiency**: Object pooling and optimized data structures  
- **Truth Maintenance**: Automatic dependency tracking and fact retraction
- **Complex Event Processing**: Temporal pattern matching
- **Thread Safety**: Immutable knowledge bases with per-thread sessions

## Enterprise Architecture Patterns

### 1. High-Volume Transaction Processing

For financial services, e-commerce, or telecommunications:

```drl
// Real-time fraud detection
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
            timestamp > ($txn.timestamp - 300000), // Last 5 minutes
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
        reason: `Velocity: $${recentAmount + amount} in 5 minutes`,
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
        timestamp > ($txn.timestamp - 3600000), // Last hour
        timestamp < $txn.timestamp
    )
then
    let distance = calculateDistance(txn.location, lastTxn.location);
    if (distance > 500) { // 500+ miles in under an hour
        drools.insert({
            type: "FraudAlert", 
            transactionId: txn.id,
            reason: `Geographic: ${distance}mi in ${(txn.timestamp - lastTxn.timestamp)/60000}min`,
            riskScore: Math.min(100, distance / 10)
        });
    }
end
```

**Production Implementation:**
```cpp
class TransactionProcessor {
private:
    std::shared_ptr<KnowledgeBase> fraud_kb;
    thread_local std::unique_ptr<StatefulSession> session;
    
public:
    TransactionProcessor() {
        // Load fraud detection rules once
        std::string rules = load_fraud_rules();
        ParsingResult result;
        fraud_kb = build_knowledge_base(rules, result);
        
        if (!result.success) {
            throw std::runtime_error("Failed to compile fraud rules");
        }
    }
    
    FraudCheckResult process_transaction(const TransactionData& txn_data) {
        // Thread-local session for safety
        if (!session) {
            session = fraud_kb->create_session();
        }
        
        // Convert to optimized fact
        auto transaction = TRANSACTION()
            .id(txn_data.id)
            .accountId(txn_data.account_id) 
            .amount(txn_data.amount)
            .merchantId(txn_data.merchant_id)
            .timestamp(txn_data.timestamp)
            .location(txn_data.location)
            .build();
            
        session->add_fact(transaction);
        
        // Execute rules
        int rules_fired = session->fire_all_rules();
        
        // Check for alerts
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
        
        // Clean up session for next transaction
        session->retract_facts_of_type("Transaction");
        session->retract_facts_of_type("FraudAlert");
        
        return result;
    }
};
```

### 2. Complex Approval Workflows

For document processing, loan approvals, or compliance:

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

// Tier 1: Auto-approve low-risk applications
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
        reason: "Auto-approved: Excellent credit profile",
        conditions: ["Standard terms apply"]
    });
    
    drools.modify(app, {status: "APPROVED"});
    
    console.log(`Auto-approved loan ${app.id} for $${app.amount}`);
end

// Tier 2: Auto-decline high-risk applications  
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
        `Credit score too low: ${app.creditScore}` :
        `Debt-to-income too high: ${Math.round(app.debtToIncome * 100)}%`;
        
    drools.insert({
        type: "ApprovalDecision",
        applicationId: app.id,
        decision: "DECLINED", 
        reason: reason,
        conditions: []
    });
    
    drools.modify(app, {status: "DECLINED"});
end

// Tier 3: Manual review for edge cases
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
    let assignee = app.amount > 250000 ? "senior-underwriter" : "underwriter";
    
    drools.insert({
        type: "ManualReview",
        applicationId: app.id,
        reason: `Large loan amount: $${app.amount}`,
        assignedTo: assignee
    });
    
    drools.modify(app, {status: "MANUAL_REVIEW"});
end

// Conditional approval for borderline cases
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
        reason: "Approved with conditions due to moderate credit profile",
        conditions: [
            "Higher interest rate: prime + 2.5%",
            "Require mortgage insurance",
            "Maximum 30-year term"
        ]
    });
    
    drools.modify(app, {status: "CONDITIONAL_APPROVAL"});
end
```

### 3. Real-Time Pricing & Revenue Optimization

For dynamic pricing in retail, airlines, or SaaS:

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

// Dynamic pricing based on inventory levels
rule "Low Inventory - Price Increase"
salience 1000
when
    $product: Product(inventory < 50, inventory > 0, $id: id, $basePrice: basePrice)
    not PriceAdjustment(productId == $id)
then
    let scarcityMultiplier = 1 + (50 - product.inventory) / 100; // Up to 50% increase
    let newPrice = Math.round(product.basePrice * scarcityMultiplier * 100) / 100;
    
    drools.insert({
        type: "PriceAdjustment",
        productId: product.id,
        oldPrice: product.basePrice,
        newPrice: newPrice,
        reason: `Low inventory: ${product.inventory} units remaining`,
        effectiveUntil: Date.now() + (24 * 60 * 60 * 1000) // 24 hours
    });
    
    drools.modify(product, {basePrice: newPrice});
end

// Competitive pricing response  
rule "Match Competitor Pricing"
salience 900
when
    $product: Product(
        competitorPrice > 0,
        competitorPrice < basePrice * 0.95, // More than 5% cheaper
        inventory > 100, // Sufficient inventory
        $id: id
    )
    not PriceAdjustment(productId == $id)
then
    let newPrice = Math.max(
        product.competitorPrice + 0.01, // Beat by 1 cent
        product.basePrice * 0.80 // Never go below 80% of base
    );
    
    drools.insert({
        type: "PriceAdjustment", 
        productId: product.id,
        oldPrice: product.basePrice,
        newPrice: newPrice,
        reason: `Competitive response: competitor at $${product.competitorPrice}`,
        effectiveUntil: Date.now() + (6 * 60 * 60 * 1000) // 6 hours
    });
    
    drools.modify(product, {basePrice: newPrice});
end

// Demand-based pricing
rule "High Demand - Premium Pricing"
salience 800
when
    $product: Product(
        demand > 2.0, // 200% of normal demand
        inventory > 200, // Good inventory
        $id: id,
        $basePrice: basePrice
    )
    not PriceAdjustment(productId == $id)
then
    let demandMultiplier = Math.min(1.25, 1 + (product.demand - 1) * 0.1); // Max 25% increase
    let newPrice = Math.round(product.basePrice * demandMultiplier * 100) / 100;
    
    drools.insert({
        type: "PriceAdjustment",
        productId: product.id, 
        oldPrice: product.basePrice,
        newPrice: newPrice,
        reason: `High demand: ${Math.round(product.demand * 100)}% of normal`,
        effectiveUntil: Date.now() + (12 * 60 * 60 * 1000) // 12 hours
    });
    
    drools.modify(product, {basePrice: newPrice});
end
```

## Performance Optimization for Enterprise

### Memory-Optimized Fact Creation

```cpp
// Use typed builders with object pooling for high-throughput scenarios
class OptimizedTransactionProcessor {
private:
    static constexpr size_t BATCH_SIZE = 1000;
    std::vector<std::shared_ptr<Fact>> transaction_batch;
    
public:
    void process_transaction_batch(const std::vector<TransactionData>& transactions) {
        transaction_batch.clear();
        transaction_batch.reserve(BATCH_SIZE);
        
        // Use optimized fact builders
        for (const auto& txn : transactions) {
            auto fact = FAST_TRANSACTION()
                .id(txn.id)
                .accountId(txn.account_id)
                .amount(txn.amount)
                .timestamp(txn.timestamp)
                .build();
                
            transaction_batch.push_back(fact);
        }
        
        // Batch insertion for better performance
        session->add_facts(transaction_batch);
        session->fire_all_rules();
        
        // Process results...
    }
};
```

### Rule Performance Monitoring

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
        
        // Process facts
        session->add_fact(convert_to_fact(data));
        int rules_fired = session->fire_all_rules();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        // Collect metrics
        metrics->record_processing_time(duration.count());
        metrics->record_rules_fired(rules_fired);
        
        // Check for slow rules
        auto performance_summary = session->get_rule_performance_summary();
        for (const auto& [rule_name, stats] : performance_summary) {
            if (stats.average_time_ms > 5.0) { // Flag slow rules
                metrics->record_slow_rule(rule_name, stats.average_time_ms);
            }
        }
        
        return extract_results(session);
    }
};
```

## Enterprise Integration Patterns

### 1. Event-Driven Architecture

```cpp
class RulesEventProcessor {
private:
    std::shared_ptr<EventBus> event_bus;
    std::shared_ptr<KnowledgeBase> kb;
    
public:
    void setup_event_handlers() {
        event_bus->subscribe<TransactionEvent>([this](const TransactionEvent& event) {
            auto session = kb->create_session();
            
            // Convert event to fact
            auto transaction_fact = EVENT_TO_TRANSACTION(event).build();
            session->add_fact(transaction_fact);
            
            // Process rules
            session->fire_all_rules();
            
            // Publish results
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

### 2. REST API Integration

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
            
            // Add product facts  
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
            
            // Execute pricing rules
            session->fire_all_rules();
            
            // Extract price adjustments
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

## Security & Compliance

### 1. Audit Trail Implementation

```cpp
class AuditableRulesEngine {
private:
    std::shared_ptr<AuditLogger> audit_logger;
    std::shared_ptr<KnowledgeBase> kb;
    
public:
    ProcessingResult process_with_audit(const InputData& data, const std::string& user_id) {
        auto session = kb->create_session();
        session->enable_tracing(true);
        
        // Log input
        audit_logger->log_input(user_id, data);
        
        // Process
        session->add_fact(convert_to_fact(data));
        int rules_fired = session->fire_all_rules();
        
        // Log rule executions
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
        
        // Log output
        audit_logger->log_output(user_id, results);
        
        return results;
    }
};
```

### 2. Rule Validation & Testing

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
        
        // 1. Syntax validation
        ParsingResult parse_result;
        auto kb = build_knowledge_base(drl_source, parse_result);
        
        if (!parse_result.success) {
            result.is_valid = false;
            for (const auto& error : parse_result.errors) {
                result.errors.push_back(error.to_string());
            }
            return result;
        }
        
        // 2. Business logic validation
        auto validation_session = kb->create_session();
        
        // Add test facts
        add_comprehensive_test_data(validation_session);
        
        // Execute rules with performance monitoring
        auto start = std::chrono::high_resolution_clock::now();
        validation_session->enable_tracing(true);
        
        int rules_fired = validation_session->fire_all_rules();
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        result.performance.execution_time_ms = duration.count();
        result.performance.rules_fired = rules_fired;
        
        // 3. Check for potential issues
        auto performance_summary = validation_session->get_rule_performance_summary();
        for (const auto& [rule_name, stats] : performance_summary) {
            if (stats.average_time_ms > 10.0) {
                result.warnings.push_back(
                    "Rule '" + rule_name + "' is slow: " + 
                    std::to_string(stats.average_time_ms) + "ms average"
                );
            }
            
            if (stats.execution_count == 0) {
                result.warnings.push_back(
                    "Rule '" + rule_name + "' never fired during validation"
                );
            }
        }
        
        result.is_valid = result.errors.empty();
        return result;
    }
};
```

## Deployment & Operations

### Configuration Management

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

### Health Checks & Monitoring

```cpp
class RulesEngineHealthCheck {
private:
    std::shared_ptr<KnowledgeBase> kb;
    std::chrono::steady_clock::time_point last_health_check;
    
public:
    HealthStatus check_health() {
        HealthStatus status;
        
        try {
            // Check knowledge base compilation
            if (!kb) {
                status.is_healthy = false;
                status.error_message = "Knowledge base not initialized";
                return status;
            }
            
            // Perform lightweight rule execution test
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
            
            // Check performance thresholds
            if (duration.count() > 1000) { // 1 second threshold
                status.warnings.push_back("Rule execution time exceeded threshold");
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

## Best Practices for Enterprise Deployment

### 1. Rule Governance
- **Version Control**: Store DRL files in Git with proper branching
- **Code Review**: All rule changes require peer review
- **Testing**: Comprehensive test suites for rule validation
- **Staging**: Deploy to staging environment before production

### 2. Performance Monitoring
- **Rule-level metrics**: Track execution time per rule
- **Memory usage**: Monitor fact cache and object pool usage
- **Throughput tracking**: Measure transactions processed per second
- **Alert thresholds**: Set up alerts for performance degradation

### 3. Security Considerations  
- **Input validation**: Sanitize all external data before fact creation
- **Rule isolation**: Separate rule bases for different security contexts
- **Audit logging**: Log all rule executions for compliance
- **Access control**: Role-based access to rule management

### 4. Scalability Patterns
- **Horizontal scaling**: Multiple engine instances with load balancing  
- **Caching strategies**: Cache compiled knowledge bases
- **Async processing**: Use event queues for non-real-time processing
- **Database integration**: Efficient fact loading from enterprise databases

---

**Ready for implementation?** See the [Developer Guide](DEVELOPER_GUIDE.md) for extending the engine and the [API Reference](API_REFERENCE.md) for complete technical documentation.