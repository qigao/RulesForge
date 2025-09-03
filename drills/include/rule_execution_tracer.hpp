#ifndef RULE_EXECUTION_TRACER_HPP
#define RULE_EXECUTION_TRACER_HPP

#include "drools_rete_defs.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

struct RuleTraceEvent {
    enum class Type {
        RULE_FIRED,
        RULE_MATCHED,
        RULE_FAILED_CONDITION,
        FACT_ADDED,
        FACT_RETRACTED,
        NETWORK_PROPAGATION
    };
    
    Type type;
    std::string rule_name;
    std::string description;
    std::chrono::high_resolution_clock::time_point timestamp;
    int64_t duration_us = 0;
    
    // Additional context
    std::vector<int64_t> involved_fact_ids;
    std::string fact_type;
    int64_t fact_id = 0;
    
    RuleTraceEvent(Type t, std::string rule = "", std::string desc = "") 
        : type(t), rule_name(std::move(rule)), description(std::move(desc)),
          timestamp(std::chrono::high_resolution_clock::now()) {}
};

class RuleExecutionTracer {
public:
    RuleExecutionTracer() = default;
    
    void enable_tracing(bool enabled = true) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }
    
    void trace_rule_fired(std::string const& rule_name, 
                         std::vector<int64_t> const& fact_ids = {},
                         int64_t duration_us = 0);
    
    void trace_rule_matched(std::string const& rule_name,
                           std::vector<int64_t> const& fact_ids);
    
    void trace_rule_condition_failed(std::string const& rule_name,
                                    std::string const& condition_desc,
                                    std::vector<int64_t> const& fact_ids);
    
    void trace_fact_added(int64_t fact_id, std::string const& fact_type);
    void trace_fact_retracted(int64_t fact_id, std::string const& fact_type);
    
    void trace_network_propagation(std::string const& node_desc, int64_t fact_id);
    
    std::vector<RuleTraceEvent> const& get_trace() const { return trace_events_; }
    void clear_trace() { trace_events_.clear(); }
    
    // Query helpers
    std::vector<RuleTraceEvent> get_rule_events(std::string const& rule_name) const;
    std::vector<RuleTraceEvent> get_fact_events(int64_t fact_id) const;
    
    // Statistics
    struct RuleStats {
        std::string rule_name;
        int fire_count = 0;
        int match_count = 0;
        int64_t total_execution_time_us = 0;
        int64_t avg_execution_time_us = 0;
    };
    
    std::vector<RuleStats> get_rule_statistics() const;
    
    // Pretty printing
    std::string format_trace(bool include_network_events = false) const;
    std::string format_rule_summary() const;

private:
    bool enabled_ = false;
    std::vector<RuleTraceEvent> trace_events_;
    
    void add_event(RuleTraceEvent event);
};

// RAII helper for timing rule execution
class RuleExecutionTimer {
public:
    RuleExecutionTimer(RuleExecutionTracer& tracer, std::string rule_name,
                      std::vector<int64_t> fact_ids = {})
        : tracer_(tracer), rule_name_(std::move(rule_name)), 
          fact_ids_(std::move(fact_ids)),
          start_(std::chrono::high_resolution_clock::now()) {}
    
    ~RuleExecutionTimer() {
        if (tracer_.is_enabled()) {
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
            tracer_.trace_rule_fired(rule_name_, fact_ids_, duration.count());
        }
    }

private:
    RuleExecutionTracer& tracer_;
    std::string rule_name_;
    std::vector<int64_t> fact_ids_;
    std::chrono::high_resolution_clock::time_point start_;
};

#endif // RULE_EXECUTION_TRACER_HPP