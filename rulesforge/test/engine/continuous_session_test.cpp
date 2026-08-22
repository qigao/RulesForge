#include "engine/continuous_session.hpp"
#include "rfl_parser.hpp"
#include "test_helpers.hpp"
#include "tinytest.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace rulesforge;

namespace {

std::shared_ptr<KnowledgeBase> build_continuous_kb() {
  ParsingResult result;
  auto kb = build_knowledge_base(R"(
    declare Event
      value: int
    end
    declare Alert
      value: int
    end

    rule "Emit Alert"
    when
      $e : Event() from entry-point "events"
    then
      insert Alert { value = 7 }
    end

    query "Alerts"
      $a : Alert()
    end
  )", result);
  if (!result.success) throw_parse_failure(result);
  return kb;
}

std::shared_ptr<KnowledgeBase> build_window_kb() {
  ParsingResult result;
  auto kb = build_knowledge_base(R"(
    declare Event
      value: int
    end
    declare CountResult
      result: double
    end
    declare WindowCount
      count: double
    end

    rule "Count Window"
    when
      $c : CountResult() from accumulate(
        Event() over window:time(100) from entry-point "events",
        count()
      )
    then
      insert WindowCount { count = $c.result }
    end

    query "Counts"
      $c : WindowCount()
    end
  )", result);
  if (!result.success) throw_parse_failure(result);
  return kb;
}

std::shared_ptr<KnowledgeBase> build_two_rule_kb() {
  ParsingResult result;
  auto kb = build_knowledge_base(R"(
    declare Event value: int end
    declare First value: int end
    declare Second value: int end

    rule "First Rule"
    when
      Event() from entry-point "events"
    then
      insert First { value = 1 }
    end

    rule "Second Rule"
    when
      Event() from entry-point "events"
    then
      insert Second { value = 2 }
    end
  )", result);
  if (!result.success) throw_parse_failure(result);
  return kb;
}

std::shared_ptr<KnowledgeBase> build_recovery_kb() {
  ParsingResult result;
  auto kb = build_knowledge_base(R"(
    declare Event value: int end
    declare Alert value: int end

    rule "Emit Primary Alert"
    when
      Event() from entry-point "events"
    then
      insert Alert { value = 1 }
    end

    rule "Emit Additional Alert"
    when
      Event(value == 2) from entry-point "events"
    then
      insert Alert { value = 2 }
    end

    query "Events"
      $e : Event() from entry-point "events"
    end
    query "Alerts"
      $a : Alert()
    end
  )", result);
  if (!result.success) throw_parse_failure(result);
  return kb;
}

EventEnvelope event(std::string id, std::int64_t event_time, std::int64_t value) {
  auto fact = std::make_shared<Fact>();
  fact->type = "Event";
  fact->fields["value"] = value;
  return {std::move(id), "events", event_time, std::move(fact)};
}

ContinuousSessionConfig config(std::vector<std::string> output_types = {}) {
  ContinuousSessionConfig value;
  value.max_active_events = 16;
  value.max_dedup_entries = 32;
  value.max_pending_result_batches = 16;
  value.max_pending_results = 64;
  value.max_input_batch_size = 8;
  value.max_replay_steps = 64;
  value.max_rules_per_step = 64;
  value.allowed_lateness_ms = 10;
  value.event_retention_ms = 100;
  value.dedup_retention_ms = 200;
  value.max_event_time_lead_ms = 1000;
  value.output_fact_types = std::move(output_types);
  return value;
}

} // namespace

suite("Continuous Session") {
  group("entry points") {
    it("rejects an unknown route before mutating working memory") {
      auto kb = build_continuous_kb();
      auto session = kb->create_session();
      auto fact = std::make_shared<Fact>();
      fact->type = "Event";
      fact->fields["value"] = int64_t(1);

      check_throws_as(session->insert_into("missing", fact), std::invalid_argument);
      check_equal(session->get_fact_count(), 0);
      check_equal(static_cast<int>(fact->id), 0);
    }
  }

  group("event processing") {
    it("pushes one event and captures immutable output") {
      ContinuousSession session(build_continuous_kb(), config({"Alert"}));
      auto result = session.push(event("event-1", 100, 7));

      check(result.status == ContinuousStepStatus::Committed);
      check_equal(result.rules_fired, 1);
      check_equal(result.outputs.size(), 1);
      check_equal(result.outputs[0].fact_type, "Alert");
      auto field = result.outputs[0].fields.find("value");
      check(field != result.outputs[0].fields.end());
      check_equal(static_cast<int>(std::get<int64_t>(field->second)), 7);

      session.acknowledge(result.batch_id);
      check_equal(session.metrics().pending_result_batches, 0);
    }

    it("rejects duplicate and late events without changing active state") {
      ContinuousSession session(build_continuous_kb(), config());
      auto first = session.push(event("event-1", 100, 1));
      session.acknowledge(first.batch_id);
      auto watermark = session.advance_watermark(150);
      session.acknowledge(watermark.batch_id);

      check_throws_as(session.push(event("event-1", 151, 2)), std::invalid_argument);
      check_throws_as(session.push(event("event-2", 139, 2)), std::invalid_argument);
      check_equal(session.metrics().active_events, 1);
      check_equal(session.metrics().dedup_entries, 1);
    }

    it("validates a batch before inserting any event") {
      ContinuousSession session(build_continuous_kb(), config());
      std::vector<EventEnvelope> events;
      events.push_back(event("same", 100, 1));
      events.push_back(event("same", 101, 2));

      check_throws_as(session.push_batch(events), std::invalid_argument);
      check_equal(session.metrics().active_events, 0);
      check_equal(session.metrics().dedup_entries, 0);
    }

    it("restores the last committed state after a failed step") {
      auto recovery_config = config({"Alert"});
      recovery_config.max_pending_results = 1;
      ContinuousSession session(build_recovery_kb(), std::move(recovery_config));
      auto first = session.push(event("one", 1, 1));
      session.acknowledge(first.batch_id);

      check_throws_as(session.push(event("two", 2, 2)), std::length_error);
      check(session.is_consistent());
      check_equal(session.execute_query("Events").size(), 1);
      check_equal(session.execute_query("Alerts").size(), 1);
      check_equal(session.metrics().active_events, 1);
      check_equal(session.metrics().dedup_entries, 1);
      check_equal(session.metrics().accepted_events, 1);
      check_equal(session.metrics().replay_recoveries, 1);

      auto resumed = session.push(event("three", 3, 3));
      check_equal(static_cast<int>(resumed.batch_id), 2);
      session.acknowledge(resumed.batch_id);
      check_equal(session.execute_query("Events").size(), 2);
      check_equal(session.execute_query("Alerts").size(), 2);
    }
  }

  group("event time") {
    it("expires out-of-order time-window facts on watermark-only steps") {
      ContinuousSession session(build_window_kb(), config({"WindowCount"}));
      auto later = session.push(event("later", 200, 2));
      session.acknowledge(later.batch_id);
      auto earlier = session.push(event("earlier", 100, 1));
      session.acknowledge(earlier.batch_id);

      auto result = session.advance_watermark(201);
      check(result.events_expired >= 1);
      check(result.rules_fired >= 1);
      check_not_empty(result.outputs);
      session.acknowledge(result.batch_id);
    }

    it("rejects a decreasing watermark") {
      ContinuousSession session(build_continuous_kb(), config());
      auto first = session.advance_watermark(100);
      session.acknowledge(first.batch_id);
      check_throws_as(session.advance_watermark(99), std::invalid_argument);
    }
  }

  group("resource bounds") {
    it("returns drain required when the per-step rule budget is exhausted") {
      auto limits = config({"First", "Second"});
      limits.max_rules_per_step = 1;
      ContinuousSession session(build_two_rule_kb(), limits);

      auto first = session.push(event("one", 1, 1));
      check(first.status == ContinuousStepStatus::DrainRequired);
      check_equal(first.rules_fired, 1);
      session.acknowledge(first.batch_id);

      auto second = session.drain();
      check(second.status == ContinuousStepStatus::Committed);
      check_equal(second.rules_fired, 1);
      session.acknowledge(second.batch_id);
    }

    it("requires result batches to be acknowledged in order") {
      ContinuousSession session(build_continuous_kb(), config());
      auto first = session.push(event("one", 1, 1));
      auto second = session.push(event("two", 2, 2));
      check_throws_as(session.acknowledge(second.batch_id), std::invalid_argument);
      session.acknowledge(first.batch_id);
      session.acknowledge(second.batch_id);
    }

    it("rejects active event growth beyond the configured limit") {
      auto limits = config();
      limits.max_active_events = 1;
      ContinuousSession session(build_continuous_kb(), limits);
      auto first = session.push(event("one", 1, 1));
      session.acknowledge(first.batch_id);
      check_throws_as(session.push(event("two", 2, 2)), std::length_error);
      check_equal(session.metrics().active_events, 1);
    }

    it("releases active capacity before deduplication retention expires") {
      auto limits = config();
      limits.max_active_events = 1;
      limits.event_retention_ms = 10;
      limits.dedup_retention_ms = 100;
      ContinuousSession session(build_continuous_kb(), limits);
      auto first = session.push(event("one", 1, 1));
      session.acknowledge(first.batch_id);
      auto expired = session.advance_watermark(20);
      session.acknowledge(expired.batch_id);

      check_equal(session.metrics().active_events, 0);
      check_equal(session.metrics().dedup_entries, 1);
      check_throws_as(session.push(event("one", 20, 2)), std::invalid_argument);
      auto second = session.push(event("two", 20, 2));
      session.acknowledge(second.batch_id);
      check_equal(session.metrics().active_events, 1);
    }
  }
}
