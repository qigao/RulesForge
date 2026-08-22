#include "engine/continuous_session.hpp"
#include "rfl_parser.hpp"
#include "tinytest.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace rulesforge;

namespace {

constexpr std::size_t kEventsPerRun = 1000;
constexpr std::size_t kBatchSize = 100;
constexpr std::size_t kSamples = 10;

std::shared_ptr<KnowledgeBase> build_benchmark_kb() {
  ParsingResult result;
  auto kb = build_knowledge_base(R"(
    declare Event
      value: int
    end

    rule "Observe Event"
    when
      Event() from entry-point "events"
    then
    end
  )", result);
  if (!result.success || !kb) {
    throw std::runtime_error("Failed to build continuous-session benchmark knowledge base");
  }
  return kb;
}

ContinuousSessionConfig benchmark_config() {
  ContinuousSessionConfig config;
  config.max_active_events = kBatchSize * 2;
  config.max_dedup_entries = kBatchSize * 3;
  config.max_pending_result_batches = 2;
  config.max_pending_results = 1;
  config.max_input_batch_size = kBatchSize;
  config.max_replay_steps = (kEventsPerRun / kBatchSize) * 2;
  config.max_rules_per_step = static_cast<int>(kBatchSize);
  config.event_retention_ms = static_cast<std::int64_t>(kBatchSize);
  config.dedup_retention_ms = static_cast<std::int64_t>(kBatchSize * 2);
  config.max_event_time_lead_ms = static_cast<std::int64_t>(kBatchSize);
  return config;
}

EventEnvelope make_event(std::size_t index) {
  auto fact = std::make_shared<Fact>();
  fact->type = "Event";
  fact->fields["value"] = static_cast<std::int64_t>(index);
  return {"event-" + std::to_string(index), "events",
          static_cast<std::int64_t>(index), std::move(fact)};
}

} // namespace

suite("Continuous Session Benchmarks") {
  bench("measures bounded steady-state ingest") {
    auto kb = build_benchmark_kb();

    benchmark("push 1K events with periodic watermarks", kSamples,
              static_cast<double>(kEventsPerRun)) {
      ContinuousSession session(kb, benchmark_config());
      std::vector<EventEnvelope> batch;
      batch.reserve(kBatchSize);

      for (std::size_t offset = 0; offset < kEventsPerRun; offset += kBatchSize) {
        batch.clear();
        for (std::size_t i = 0; i < kBatchSize; ++i) {
          batch.push_back(make_event(offset + i));
        }
        auto pushed = session.push_batch(batch);
        session.acknowledge(pushed.batch_id);

        auto watermark = session.advance_watermark(
            static_cast<std::int64_t>(offset + kBatchSize - 1));
        session.acknowledge(watermark.batch_id);
      }
    }
  }
}
