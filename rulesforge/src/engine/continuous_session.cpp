#include "engine/continuous_session.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace rulesforge {
namespace {

bool checked_subtract(std::int64_t value, std::int64_t delta, std::int64_t& result) {
  if (delta < 0) return false;
  if (value < std::numeric_limits<std::int64_t>::min() + delta) {
    result = std::numeric_limits<std::int64_t>::min();
  } else {
    result = value - delta;
  }
  return true;
}

} // namespace

ContinuousSession::ContinuousSession(std::shared_ptr<KnowledgeBase> knowledge_base,
                                     ContinuousSessionConfig config)
    : knowledge_base_(std::move(knowledge_base)), config_(std::move(config)) {
  if (!knowledge_base_) throw std::invalid_argument("Knowledge base is required");
  validate_config();
  output_fact_types_.reserve(config_.output_fact_types.size());
  for (auto const& type : config_.output_fact_types) {
    if (type.empty()) throw std::invalid_argument("Output fact type cannot be empty");
    output_fact_types_.insert(type);
  }
  active_events_.reserve(config_.max_active_events);
  dedup_event_times_.reserve(config_.max_dedup_entries);
  committed_operations_.reserve(std::min<std::size_t>(config_.max_replay_steps, 4096));
  session_ = knowledge_base_->create_session();
  session_->enable_event_time_mode();
}

void ContinuousSession::validate_config() const {
  if (config_.max_active_events == 0 || config_.max_dedup_entries == 0
      || config_.max_pending_result_batches == 0 || config_.max_pending_results == 0
      || config_.max_input_batch_size == 0 || config_.max_replay_steps == 0
      || config_.max_rules_per_step <= 0 || config_.allowed_lateness_ms < 0
      || config_.event_retention_ms <= 0 || config_.dedup_retention_ms <= 0
      || config_.max_event_time_lead_ms < 0) {
    throw std::invalid_argument("Continuous session limits must be positive and bounded");
  }
}

EventEnvelope ContinuousSession::clone_envelope(EventEnvelope const& event) {
  EventEnvelope copy;
  copy.event_id = event.event_id;
  copy.entry_point = event.entry_point;
  copy.event_time_ms = event.event_time_ms;
  if (event.fact) {
    copy.fact = std::make_shared<Fact>(*event.fact);
    copy.fact->id = 0;
  }
  return copy;
}

void ContinuousSession::validate_events(std::span<EventEnvelope const> events) {
  if (!is_consistent()) throw std::runtime_error("Continuous session is inconsistent");
  if (events.empty() || events.size() > config_.max_input_batch_size) {
    ++metrics_.rejected_resource_limits;
    throw std::invalid_argument("Event batch is empty or exceeds max_input_batch_size");
  }
  if (events.size() > config_.max_active_events
      || active_events_.size() > config_.max_active_events - events.size()) {
    ++metrics_.rejected_resource_limits;
    throw std::length_error("Active event limit exceeded");
  }
  if (events.size() > config_.max_dedup_entries
      || dedup_event_times_.size() > config_.max_dedup_entries - events.size()) {
    ++metrics_.rejected_resource_limits;
    throw std::length_error("Deduplication entry limit exceeded");
  }
  if (committed_operations_.size() >= config_.max_replay_steps) {
    ++metrics_.rejected_resource_limits;
    throw std::length_error("Replay step limit exceeded");
  }

  std::unordered_set<std::string> batch_ids;
  batch_ids.reserve(events.size());
  std::int64_t late_boundary = std::numeric_limits<std::int64_t>::min();
  if (watermark_ms_) checked_subtract(*watermark_ms_, config_.allowed_lateness_ms, late_boundary);

  for (auto const& event : events) {
    if (event.event_id.empty() || event.entry_point.empty() || !event.fact) {
      throw std::invalid_argument("Event ID, entry point, and fact are required");
    }
    if (dedup_event_times_.contains(event.event_id) || !batch_ids.insert(event.event_id).second) {
      ++metrics_.rejected_duplicates;
      throw std::invalid_argument("Duplicate event ID: " + event.event_id);
    }
    if (watermark_ms_ && event.event_time_ms < late_boundary) {
      ++metrics_.rejected_late_events;
      throw std::invalid_argument("Late event rejected: " + event.event_id);
    }
    if (watermark_ms_) {
      auto const max_lead = config_.max_event_time_lead_ms;
      bool const beyond_lead = *watermark_ms_ > std::numeric_limits<std::int64_t>::max() - max_lead
          ? false
          : event.event_time_ms > *watermark_ms_ + max_lead;
      if (beyond_lead) {
        throw std::invalid_argument("Event time exceeds configured watermark lead");
      }
    }
    session_->validate_entry_point_route(event.entry_point, event.fact->type);
  }
}

ContinuousStepResult ContinuousSession::push(EventEnvelope event) {
  return push_batch(std::span<EventEnvelope const>(&event, 1));
}

ContinuousStepResult ContinuousSession::push_batch(std::span<EventEnvelope const> events) {
  validate_events(events);
  Operation operation;
  operation.kind = OperationKind::Events;
  operation.events.reserve(events.size());
  for (auto const& event : events) operation.events.push_back(clone_envelope(event));
  return execute_operation(operation, true, true);
}

ContinuousStepResult ContinuousSession::advance_watermark(std::int64_t watermark_ms) {
  if (!is_consistent()) throw std::runtime_error("Continuous session is inconsistent");
  if (watermark_ms_ && watermark_ms < *watermark_ms_) {
    throw std::invalid_argument("Watermark cannot move backwards");
  }
  if (committed_operations_.size() >= config_.max_replay_steps) {
    ++metrics_.rejected_resource_limits;
    throw std::length_error("Replay step limit exceeded");
  }
  Operation operation;
  operation.kind = OperationKind::Watermark;
  operation.watermark_ms = watermark_ms;
  return execute_operation(operation, true, true);
}

ContinuousStepResult ContinuousSession::drain() {
  if (!is_consistent()) throw std::runtime_error("Continuous session is inconsistent");
  if (committed_operations_.size() >= config_.max_replay_steps) {
    ++metrics_.rejected_resource_limits;
    throw std::length_error("Replay step limit exceeded");
  }
  Operation operation;
  operation.kind = OperationKind::Drain;
  return execute_operation(operation, true, true);
}

ContinuousStepResult ContinuousSession::execute_operation(Operation const& operation,
                                                          bool record_operation,
                                                          bool publish) {
  auto const first_new_fact_id = session_->next_fact_id();
  std::size_t expired = 0;
  try {
    if (operation.kind == OperationKind::Events) {
      apply_events(operation.events);
    } else if (operation.kind == OperationKind::Watermark) {
      expired = apply_watermark(operation.watermark_ms);
    }
    int const rules_fired = fire_step();
    if (record_operation) {
      committed_operations_.push_back(operation);
      if (operation.kind == OperationKind::Events) {
        metrics_.accepted_events += operation.events.size();
      }
      metrics_.expired_events += expired;
    }
    if (publish) {
      try {
        return publish_result(rules_fired, expired, first_new_fact_id);
      } catch (...) {
        if (record_operation) {
          committed_operations_.pop_back();
          if (operation.kind == OperationKind::Events) {
            metrics_.accepted_events -= operation.events.size();
          }
          metrics_.expired_events -= expired;
        }
        throw;
      }
    }
    ContinuousStepResult result;
    result.rules_fired = rules_fired;
    result.events_expired = expired;
    result.watermark_ms = watermark_ms_;
    result.status = session_->pending_activation_count() == 0
        ? ContinuousStepStatus::Committed : ContinuousStepStatus::DrainRequired;
    return result;
  } catch (...) {
    if (!replaying_) {
      try {
        recover_by_replay();
      } catch (...) {
        consistent_ = false;
      }
    } else {
      consistent_ = false;
    }
    throw;
  }
}

void ContinuousSession::apply_events(std::span<EventEnvelope const> events) {
  for (auto const& event : events) {
    auto fact = std::make_shared<Fact>(*event.fact);
    fact->id = 0;
    session_->insert_event_into(event.entry_point, fact, event.event_time_ms);
    active_events_.push_back({event.event_id, event.entry_point, event.event_time_ms, fact});
    dedup_event_times_[event.event_id] = event.event_time_ms;
  }
}

std::size_t ContinuousSession::apply_watermark(std::int64_t watermark_ms) {
  std::size_t expired = session_->advance_event_time(watermark_ms);
  watermark_ms_ = watermark_ms;
  std::int64_t retention_boundary;
  checked_subtract(watermark_ms, config_.event_retention_ms, retention_boundary);

  auto out = active_events_.begin();
  for (auto it = active_events_.begin(); it != active_events_.end(); ++it) {
    if (it->event_time_ms <= retention_boundary) {
      session_->retract_from(it->entry_point, it->fact.get());
      ++expired;
      continue;
    }
    if (out != it) *out = std::move(*it);
    ++out;
  }
  active_events_.erase(out, active_events_.end());
  expire_dedup_entries();
  return expired;
}

void ContinuousSession::expire_dedup_entries() {
  if (!watermark_ms_) return;
  std::int64_t boundary;
  checked_subtract(*watermark_ms_, config_.dedup_retention_ms, boundary);
  for (auto it = dedup_event_times_.begin(); it != dedup_event_times_.end();) {
    if (it->second <= boundary) it = dedup_event_times_.erase(it);
    else ++it;
  }
}

int ContinuousSession::fire_step() {
  int const fired = session_->fire_all_rules_fail_fast(config_.max_rules_per_step);
  return fired;
}

std::vector<ContinuousOutput> ContinuousSession::collect_outputs(
    std::int64_t first_new_fact_id) const {
  std::vector<ContinuousOutput> outputs;
  for (auto* fact : session_->facts_snapshot()) {
    if (!fact || fact->id < first_new_fact_id || !output_fact_types_.contains(fact->type)) continue;
    outputs.push_back({fact->id, fact->type, fact->fields});
  }
  std::sort(outputs.begin(), outputs.end(), [](auto const& lhs, auto const& rhs) {
    return lhs.fact_id < rhs.fact_id;
  });
  return outputs;
}

ContinuousStepResult ContinuousSession::publish_result(int rules_fired,
                                                       std::size_t events_expired,
                                                       std::int64_t first_new_fact_id) {
  ContinuousStepResult result;
  result.watermark_ms = watermark_ms_;
  result.rules_fired = rules_fired;
  result.events_expired = events_expired;
  result.status = session_->pending_activation_count() == 0
      ? ContinuousStepStatus::Committed : ContinuousStepStatus::DrainRequired;
  result.outputs = collect_outputs(first_new_fact_id);
  if (pending_batches_.size() >= config_.max_pending_result_batches
      || result.outputs.size() > config_.max_pending_results
      || metrics_.pending_results > config_.max_pending_results - result.outputs.size()) {
    ++metrics_.rejected_resource_limits;
    throw std::length_error("Pending result limit exceeded");
  }
  result.batch_id = next_batch_id_;
  pending_batches_.push_back({result.batch_id, result.outputs.size()});
  ++next_batch_id_;
  metrics_.pending_results += result.outputs.size();
  return result;
}

void ContinuousSession::acknowledge(std::uint64_t batch_id) {
  if (pending_batches_.empty() || pending_batches_.front().id != batch_id) {
    throw std::invalid_argument("Result batches must be acknowledged in order");
  }
  metrics_.pending_results -= pending_batches_.front().output_count;
  pending_batches_.pop_front();
}

void ContinuousSession::recover_by_replay() {
  auto replacement = knowledge_base_->create_session();
  replacement->enable_event_time_mode();
  auto failed_session = std::move(session_);
  session_ = std::move(replacement);
  replaying_ = true;
  watermark_ms_.reset();
  active_events_.clear();
  dedup_event_times_.clear();
  auto accepted = metrics_.accepted_events;
  auto expired = metrics_.expired_events;
  try {
    for (auto const& operation : committed_operations_) {
      execute_operation(operation, false, false);
    }
  } catch (...) {
    replaying_ = false;
    consistent_ = false;
    throw;
  }
  metrics_.accepted_events = accepted;
  metrics_.expired_events = expired;
  ++metrics_.replay_recoveries;
  consistent_ = true;
  replaying_ = false;
}

ContinuousMetrics ContinuousSession::metrics() const {
  auto result = metrics_;
  result.active_events = active_events_.size();
  result.dedup_entries = dedup_event_times_.size();
  result.pending_result_batches = pending_batches_.size();
  return result;
}

QueryResult ContinuousSession::execute_query(std::string const& query_name,
                                             std::vector<Fact*> const& args) {
  if (!is_consistent()) throw std::runtime_error("Continuous session is inconsistent");
  return session_->execute_query(query_name, args);
}

} // namespace rulesforge
