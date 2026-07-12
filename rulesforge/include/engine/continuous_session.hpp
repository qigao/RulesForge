#ifndef CONTINUOUS_SESSION_HPP
#define CONTINUOUS_SESSION_HPP

#include "core/fact.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rulesforge {

struct ContinuousSessionConfig {
  std::size_t max_active_events = 10000;
  std::size_t max_dedup_entries = 20000;
  std::size_t max_pending_result_batches = 128;
  std::size_t max_pending_results = 10000;
  std::size_t max_input_batch_size = 1000;
  std::size_t max_replay_steps = 100000;
  int max_rules_per_step = 10000;
  std::int64_t allowed_lateness_ms = 0;
  std::int64_t event_retention_ms = 3600000;
  std::int64_t dedup_retention_ms = 3600000;
  std::int64_t max_event_time_lead_ms = 86400000;
  std::vector<std::string> output_fact_types;
};

struct EventEnvelope {
  std::string event_id;
  std::string entry_point;
  std::int64_t event_time_ms = 0;
  std::shared_ptr<Fact> fact;
};

struct ContinuousOutput {
  std::int64_t fact_id = 0;
  std::string fact_type;
  rulesforge::InternedKeyMap<ConstraintValue> fields;
};

enum class ContinuousStepStatus {
  Committed,
  DrainRequired,
};

struct ContinuousStepResult {
  ContinuousStepStatus status = ContinuousStepStatus::Committed;
  std::uint64_t batch_id = 0;
  std::optional<std::int64_t> watermark_ms;
  int rules_fired = 0;
  std::size_t events_expired = 0;
  std::vector<ContinuousOutput> outputs;
};

struct ContinuousMetrics {
  std::uint64_t accepted_events = 0;
  std::uint64_t expired_events = 0;
  std::uint64_t rejected_duplicates = 0;
  std::uint64_t rejected_late_events = 0;
  std::uint64_t rejected_resource_limits = 0;
  std::uint64_t replay_recoveries = 0;
  std::size_t active_events = 0;
  std::size_t dedup_entries = 0;
  std::size_t pending_result_batches = 0;
  std::size_t pending_results = 0;
};

class ContinuousSession {
public:
  ContinuousSession(std::shared_ptr<KnowledgeBase> knowledge_base,
                    ContinuousSessionConfig config);

  ContinuousStepResult push(EventEnvelope event);
  ContinuousStepResult push_batch(std::span<EventEnvelope const> events);
  ContinuousStepResult advance_watermark(std::int64_t watermark_ms);
  ContinuousStepResult drain();
  void acknowledge(std::uint64_t batch_id);

  ContinuousMetrics metrics() const;
  QueryResult execute_query(std::string const& query_name,
                            std::vector<Fact*> const& args = {});
  bool is_consistent() const { return consistent_ && session_->is_consistent(); }

private:
  enum class OperationKind { Events, Watermark, Drain };
  struct Operation {
    OperationKind kind = OperationKind::Events;
    std::vector<EventEnvelope> events;
    std::int64_t watermark_ms = 0;
  };
  struct ActiveEvent {
    std::string event_id;
    std::string entry_point;
    std::int64_t event_time_ms = 0;
    std::shared_ptr<Fact> fact;
  };
  struct PendingBatch {
    std::uint64_t id = 0;
    std::size_t output_count = 0;
  };

  static EventEnvelope clone_envelope(EventEnvelope const& event);
  void validate_config() const;
  void validate_events(std::span<EventEnvelope const> events);
  ContinuousStepResult execute_operation(Operation const& operation,
                                         bool record_operation,
                                         bool publish_result);
  void apply_events(std::span<EventEnvelope const> events);
  std::size_t apply_watermark(std::int64_t watermark_ms);
  int fire_step();
  std::vector<ContinuousOutput> collect_outputs(std::int64_t first_new_fact_id) const;
  ContinuousStepResult publish_result(int rules_fired,
                                      std::size_t events_expired,
                                      std::int64_t first_new_fact_id);
  void recover_by_replay();
  void expire_dedup_entries();

  std::shared_ptr<KnowledgeBase> knowledge_base_;
  ContinuousSessionConfig config_;
  std::unique_ptr<StatefulSession> session_;
  std::optional<std::int64_t> watermark_ms_;
  std::vector<ActiveEvent> active_events_;
  std::unordered_map<std::string, std::int64_t> dedup_event_times_;
  std::vector<Operation> committed_operations_;
  std::deque<PendingBatch> pending_batches_;
  std::unordered_set<std::string> output_fact_types_;
  ContinuousMetrics metrics_;
  std::uint64_t next_batch_id_ = 1;
  bool consistent_ = true;
  bool replaying_ = false;
};

} // namespace rulesforge

#endif // CONTINUOUS_SESSION_HPP
