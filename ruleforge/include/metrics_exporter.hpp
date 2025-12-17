#ifndef METRICS_EXPORTER_HPP
#define METRICS_EXPORTER_HPP

#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

/**
 * @brief P1-002 FIX: Metrics data structure for export
 */
struct SessionMetrics {
    // Counters
    int64_t rules_fired_total = 0;
    int64_t facts_inserted_total = 0;
    int64_t facts_retracted_total = 0;

    // Gauges
    int64_t facts_count = 0;
    size_t memory_used_bytes = 0;
    size_t memory_max_bytes = 0;
    int memory_usage_percent = 0;
    size_t activations_count = 0;

    // Rule-specific metrics
    std::unordered_map<std::string, int64_t> rule_fire_counts;
    std::unordered_map<std::string, int64_t> rule_execution_time_us;  // total microseconds

    // Timing
    std::chrono::steady_clock::time_point last_fire_time;
    int64_t last_rule_duration_us = 0;
};

/**
 * @brief P1-002 FIX: Interface for exporting metrics to monitoring systems
 */
class IMetricsExporter {
public:
    virtual ~IMetricsExporter() = default;

    /**
     * @brief Export metrics in the format specific to this exporter
     * @param metrics Current session metrics
     * @return Formatted metrics string
     */
    virtual std::string export_metrics(SessionMetrics const& metrics) const = 0;

    /**
     * @brief Get the content type for HTTP responses
     */
    virtual std::string content_type() const = 0;
};

/**
 * @brief P1-002 FIX: Prometheus format metrics exporter
 * Exports metrics in Prometheus exposition format for scraping
 */
class PrometheusExporter : public IMetricsExporter {
public:
    explicit PrometheusExporter(std::string prefix = "ruleforge")
        : prefix_(std::move(prefix)) {}

    std::string export_metrics(SessionMetrics const& metrics) const override {
        std::ostringstream oss;

        // Counter: total rules fired
        oss << "# HELP " << prefix_ << "_rules_fired_total Total number of rules fired\n";
        oss << "# TYPE " << prefix_ << "_rules_fired_total counter\n";
        oss << prefix_ << "_rules_fired_total " << metrics.rules_fired_total << "\n\n";

        // Counter: facts inserted
        oss << "# HELP " << prefix_ << "_facts_inserted_total Total facts inserted\n";
        oss << "# TYPE " << prefix_ << "_facts_inserted_total counter\n";
        oss << prefix_ << "_facts_inserted_total " << metrics.facts_inserted_total << "\n\n";

        // Counter: facts retracted
        oss << "# HELP " << prefix_ << "_facts_retracted_total Total facts retracted\n";
        oss << "# TYPE " << prefix_ << "_facts_retracted_total counter\n";
        oss << prefix_ << "_facts_retracted_total " << metrics.facts_retracted_total << "\n\n";

        // Gauge: current fact count
        oss << "# HELP " << prefix_ << "_facts_count Current number of facts in working memory\n";
        oss << "# TYPE " << prefix_ << "_facts_count gauge\n";
        oss << prefix_ << "_facts_count " << metrics.facts_count << "\n\n";

        // Gauge: memory usage
        oss << "# HELP " << prefix_ << "_memory_used_bytes Memory used by session arena\n";
        oss << "# TYPE " << prefix_ << "_memory_used_bytes gauge\n";
        oss << prefix_ << "_memory_used_bytes " << metrics.memory_used_bytes << "\n\n";

        oss << "# HELP " << prefix_ << "_memory_max_bytes Maximum memory limit for session\n";
        oss << "# TYPE " << prefix_ << "_memory_max_bytes gauge\n";
        oss << prefix_ << "_memory_max_bytes " << metrics.memory_max_bytes << "\n\n";

        oss << "# HELP " << prefix_ << "_memory_usage_percent Memory usage percentage\n";
        oss << "# TYPE " << prefix_ << "_memory_usage_percent gauge\n";
        oss << prefix_ << "_memory_usage_percent " << metrics.memory_usage_percent << "\n\n";

        // Gauge: pending activations
        oss << "# HELP " << prefix_ << "_activations_count Pending activations in agenda\n";
        oss << "# TYPE " << prefix_ << "_activations_count gauge\n";
        oss << prefix_ << "_activations_count " << metrics.activations_count << "\n\n";

        // Per-rule fire counts
        if (!metrics.rule_fire_counts.empty()) {
            oss << "# HELP " << prefix_ << "_rule_fires_total Fire count per rule\n";
            oss << "# TYPE " << prefix_ << "_rule_fires_total counter\n";
            for (auto const& [rule_name, count] : metrics.rule_fire_counts) {
                oss << prefix_ << "_rule_fires_total{rule=\"" << escape_label(rule_name) << "\"} "
                    << count << "\n";
            }
            oss << "\n";
        }

        // Per-rule execution time (in seconds for Prometheus convention)
        if (!metrics.rule_execution_time_us.empty()) {
            oss << "# HELP " << prefix_ << "_rule_execution_seconds_total Total execution time per rule\n";
            oss << "# TYPE " << prefix_ << "_rule_execution_seconds_total counter\n";
            for (auto const& [rule_name, time_us] : metrics.rule_execution_time_us) {
                double seconds = static_cast<double>(time_us) / 1'000'000.0;
                oss << prefix_ << "_rule_execution_seconds_total{rule=\"" << escape_label(rule_name) << "\"} "
                    << seconds << "\n";
            }
            oss << "\n";
        }

        return oss.str();
    }

    std::string content_type() const override {
        return "text/plain; version=0.0.4; charset=utf-8";
    }

private:
    std::string prefix_;

    static std::string escape_label(std::string const& s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            if (c == '"' || c == '\\' || c == '\n') {
                result += '\\';
            }
            result += c;
        }
        return result;
    }
};

/**
 * @brief P1-002 FIX: JSON format metrics exporter
 * Exports metrics in JSON format for generic consumption
 */
class JsonMetricsExporter : public IMetricsExporter {
public:
    std::string export_metrics(SessionMetrics const& metrics) const override {
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"rules_fired_total\": " << metrics.rules_fired_total << ",\n";
        oss << "  \"facts_inserted_total\": " << metrics.facts_inserted_total << ",\n";
        oss << "  \"facts_retracted_total\": " << metrics.facts_retracted_total << ",\n";
        oss << "  \"facts_count\": " << metrics.facts_count << ",\n";
        oss << "  \"memory_used_bytes\": " << metrics.memory_used_bytes << ",\n";
        oss << "  \"memory_max_bytes\": " << metrics.memory_max_bytes << ",\n";
        oss << "  \"memory_usage_percent\": " << metrics.memory_usage_percent << ",\n";
        oss << "  \"activations_count\": " << metrics.activations_count << ",\n";

        oss << "  \"rules\": {\n";
        bool first = true;
        for (auto const& [rule_name, count] : metrics.rule_fire_counts) {
            if (!first) oss << ",\n";
            first = false;
            auto time_it = metrics.rule_execution_time_us.find(rule_name);
            int64_t time_us = (time_it != metrics.rule_execution_time_us.end()) ? time_it->second : 0;
            oss << "    \"" << rule_name << "\": {\"fires\": " << count
                << ", \"execution_time_us\": " << time_us << "}";
        }
        oss << "\n  }\n";
        oss << "}\n";
        return oss.str();
    }

    std::string content_type() const override {
        return "application/json";
    }
};

#endif // METRICS_EXPORTER_HPP


