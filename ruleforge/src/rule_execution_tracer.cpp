#include "rule_execution_tracer.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_map>

void RuleExecutionTracer::trace_rule_fired(std::string const &rule_name,
                                           std::vector<int64_t> const &fact_ids,
                                           int64_t duration_us) {
  if (!enabled_)
    return;

  RuleTraceEvent event(RuleTraceEvent::Type::RULE_FIRED, rule_name, "Rule executed successfully");
  event.involved_fact_ids = fact_ids;
  event.duration_us = duration_us;
  add_event(std::move(event));
}

void RuleExecutionTracer::trace_rule_matched(std::string const &rule_name,
                                             std::vector<int64_t> const &fact_ids) {
  if (!enabled_)
    return;

  RuleTraceEvent event(RuleTraceEvent::Type::RULE_MATCHED, rule_name,
                       "Rule conditions matched, added to agenda");
  event.involved_fact_ids = fact_ids;
  add_event(std::move(event));
}

void RuleExecutionTracer::trace_rule_condition_failed(std::string const &rule_name,
                                                      std::string const &condition_desc,
                                                      std::vector<int64_t> const &fact_ids) {
  if (!enabled_)
    return;

  RuleTraceEvent event(RuleTraceEvent::Type::RULE_FAILED_CONDITION, rule_name,
                       "Condition failed: " + condition_desc);
  event.involved_fact_ids = fact_ids;
  add_event(std::move(event));
}

void RuleExecutionTracer::trace_fact_added(int64_t fact_id, std::string const &fact_type) {
  if (!enabled_)
    return;

  RuleTraceEvent event(RuleTraceEvent::Type::FACT_ADDED, "", "Added fact of type " + fact_type);
  event.fact_id = fact_id;
  event.fact_type = fact_type;
  add_event(std::move(event));
}

void RuleExecutionTracer::trace_fact_retracted(int64_t fact_id, std::string const &fact_type) {
  if (!enabled_)
    return;

  RuleTraceEvent event(RuleTraceEvent::Type::FACT_RETRACTED, "",
                       "Retracted fact of type " + fact_type);
  event.fact_id = fact_id;
  event.fact_type = fact_type;
  add_event(std::move(event));
}

void RuleExecutionTracer::trace_network_propagation(std::string const &node_desc, int64_t fact_id) {
  if (!enabled_)
    return;

  RuleTraceEvent event(RuleTraceEvent::Type::NETWORK_PROPAGATION, "",
                       "Network propagation through " + node_desc);
  event.fact_id = fact_id;
  add_event(std::move(event));
}

std::vector<RuleTraceEvent>
RuleExecutionTracer::get_rule_events(std::string const &rule_name) const {
  std::vector<RuleTraceEvent> result;
  for (auto const &event : trace_events_) {
    if (event.rule_name == rule_name) {
      result.push_back(event);
    }
  }
  return result;
}

std::vector<RuleTraceEvent> RuleExecutionTracer::get_fact_events(int64_t fact_id) const {
  std::vector<RuleTraceEvent> result;
  for (auto const &event : trace_events_) {
    if (event.fact_id == fact_id ||
        std::find(event.involved_fact_ids.begin(), event.involved_fact_ids.end(), fact_id) !=
            event.involved_fact_ids.end()) {
      result.push_back(event);
    }
  }
  return result;
}

std::vector<RuleExecutionTracer::RuleStats> RuleExecutionTracer::get_rule_statistics() const {
  std::unordered_map<std::string, RuleStats> stats_map;

  for (auto const &event : trace_events_) {
    if (event.rule_name.empty())
      continue;

    auto &stats = stats_map[event.rule_name];
    stats.rule_name = event.rule_name;

    switch (event.type) {
    case RuleTraceEvent::Type::RULE_FIRED:
      stats.fire_count++;
      stats.total_execution_time_us += event.duration_us;
      break;
    case RuleTraceEvent::Type::RULE_MATCHED:
      stats.match_count++;
      break;
    default:
      break;
    }
  }

  // Calculate averages
  for (auto &[name, stats] : stats_map) {
    if (stats.fire_count > 0) {
      stats.avg_execution_time_us = stats.total_execution_time_us / stats.fire_count;
    }
  }

  std::vector<RuleStats> result;
  for (auto const &[name, stats] : stats_map) {
    result.push_back(stats);
  }

  // Sort by total execution time (descending)
  std::sort(result.begin(), result.end(), [](auto const &a, auto const &b) {
    return a.total_execution_time_us > b.total_execution_time_us;
  });

  return result;
}

std::string RuleExecutionTracer::format_trace(bool include_network_events) const {
  std::ostringstream oss;
  oss << "=== Rule Execution Trace ===\n";
  oss << "Total events: " << trace_events_.size() << "\n\n";

  for (size_t i = 0; i < trace_events_.size(); ++i) {
    auto const &event = trace_events_[i];

    if (!include_network_events && event.type == RuleTraceEvent::Type::NETWORK_PROPAGATION) {
      continue;
    }

    oss << "[" << std::setw(4) << i << "] ";

    switch (event.type) {
    case RuleTraceEvent::Type::RULE_FIRED:
      oss << "  FIRED: " << event.rule_name;
      if (event.duration_us > 0) {
        oss << " (" << event.duration_us << "μs)";
      }
      break;
    case RuleTraceEvent::Type::RULE_MATCHED:
      oss << " MATCHED: " << event.rule_name;
      break;
    case RuleTraceEvent::Type::RULE_FAILED_CONDITION:
      oss << " FAILED: " << event.rule_name;
      break;
    case RuleTraceEvent::Type::FACT_ADDED:
      oss << " ADD_FACT: " << event.fact_type << " (ID: " << event.fact_id << ")";
      break;
    case RuleTraceEvent::Type::FACT_RETRACTED:
      oss << " RETRACT_FACT: " << event.fact_type << " (ID: " << event.fact_id << ")";
      break;
    case RuleTraceEvent::Type::NETWORK_PROPAGATION:
      oss << "   PROPAGATE: " << event.description;
      break;
    }

    if (!event.involved_fact_ids.empty()) {
      oss << " [Facts: ";
      for (size_t j = 0; j < event.involved_fact_ids.size(); ++j) {
        if (j > 0)
          oss << ", ";
        oss << event.involved_fact_ids[j];
      }
      oss << "]";
    }

    if (!event.description.empty() && event.type != RuleTraceEvent::Type::NETWORK_PROPAGATION) {
      oss << " - " << event.description;
    }

    oss << "\n";
  }

  return oss.str();
}

std::string RuleExecutionTracer::format_rule_summary() const {
  std::ostringstream oss;
  auto stats = get_rule_statistics();

  oss << "=== Rule Performance Summary ===\n";
  oss << std::left << std::setw(30) << "Rule Name" << std::setw(8) << "Fired" << std::setw(10)
      << "Matched" << std::setw(12) << "Total μs" << std::setw(10) << "Avg μs" << "\n";
  oss << std::string(70, '-') << "\n";

  for (auto const &stat : stats) {
    oss << std::left << std::setw(30) << stat.rule_name << std::setw(8) << stat.fire_count
        << std::setw(10) << stat.match_count << std::setw(12) << stat.total_execution_time_us
        << std::setw(10) << stat.avg_execution_time_us << "\n";
  }

  return oss.str();
}

void RuleExecutionTracer::add_event(RuleTraceEvent event) {
  trace_events_.push_back(std::move(event));
}
