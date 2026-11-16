#define TINYTEST_NO_MAIN
#include "dispatch_fixture.hpp"
#include <tinytest.hpp>

using namespace boiler_room;
using namespace boiler_room_test;
namespace {
PumpInput reading(int second, double rms = 2) {
  PumpInput input;
  input.tick = second * frame_ms;
  input.sequence = second + 1;
  input.epoch = 1;
  input.fresh = input.running = true;
  input.rms = rms;
  input.kurtosis = 3;
  input.current = 5;
  input.pressure = 150;
  input.flow = 18;
  return input;
}
PumpOutput feed(Runtime &runtime, int first, int last, double rms = 2) {
  PumpOutput out;
  for (int t = first; t <= last; ++t)
    out = runtime.pump(0, reading(t, rms));
  return out;
}
PlanCandidate candidate(DispatchDecision const &decision, std::string_view action) {
  for (auto const &value : decision.candidates)
    if (value.action == action)
      return value;
  throw std::logic_error("Missing candidate " + std::string(action));
}
} // namespace

suite("RETE event-time windows drive operational advice") {
  it("aggregates valid counts halves peak and actual continuous exposure") {
    Runtime runtime(BOILER_BUNDLE);
    auto baseline = feed(runtime, 0, 59);
    check_equal(baseline.window.samples, 30);
    check_true(baseline.window.ready);
    check_true(baseline.window.trend_ready);
    check_within(baseline.window.coverage, 100, 1e-12);
    auto spike = feed(runtime, 60, 60, 4);
    check_equal(spike.window.elevated, 1);
    check_within(spike.window.peak, 50, 1e-12);
    check_within(spike.window.mean, 50.0 / 30, 1e-10);
    check_within(spike.window.recent_mean, 50.0 / 15, 1e-10);
    check_within(spike.window.prior_mean, 0, 1e-12);
    check_within(spike.window.continuous_seconds, 0, 1e-12);
    auto sustained = feed(runtime, 61, 70, 4);
    check_equal(sustained.window.elevated, 11);
    check_within(sustained.window.continuous_seconds, 10, 1e-12);
    auto full = feed(runtime, 71, 90, 4);
    check_equal(full.window.elevated, 30);
    check_equal(full.window_samples, 30u);
    check_within(full.window.mean, 50, 1e-12);
    check_within(full.window.continuous_seconds, 29, 1e-12);
  }
  it("breaks continuous exposure on a missing frame and excludes it from the mean") {
    Runtime runtime(BOILER_BUNDLE);
    feed(runtime, 0, 59);
    feed(runtime, 60, 65, 4);
    auto absent = reading(66, 4);
    absent.fresh = false;
    auto missing = runtime.pump(0, absent);
    check_false(missing.window.ready);
    check_equal(missing.window.samples, 29);
    check_within(missing.window.continuous_seconds, 0, 1e-12);
    auto restarted = feed(runtime, 67, 67, 4);
    check_true(restarted.window.ready);
    check_within(restarted.window.continuous_seconds, 0, 1e-12);
    auto out = feed(runtime, 68, 72, 4);
    check_equal(out.window.samples, 29);
    check_equal(out.window.elevated, 12);
    check_within(out.window.mean, 600.0 / 29, 1e-10);
    check_within(out.window.continuous_seconds, 5, 1e-12);
  }
  it("keeps recovered telemetry unknown until coverage is restored and gates half-window trends") {
    Runtime runtime(BOILER_BUNDLE);
    feed(runtime, 0, 59);
    for (int t = 60; t <= 66; ++t) {
      auto input = reading(t);
      input.fresh = false;
      runtime.pump(0, input);
    }
    auto returned = feed(runtime, 67, 67);
    check_true(returned.fresh);
    check_false(returned.window.ready);
    check_equal(returned.window.samples, 23);
    auto covered = feed(runtime, 68, 90);
    check_equal(covered.window.samples, 24);
    check_true(covered.window.ready);
    check_within(covered.window.coverage, 80, 1e-12);
    check_false(covered.window.trend_ready);
    auto balanced = feed(runtime, 91, 93);
    check_true(balanced.window.trend_ready);
  }
  it("expires evidence without a new telemetry message and never counts a gap as healthy") {
    Runtime runtime(BOILER_BUNDLE);
    feed(runtime, 0, 59, 4);
    auto expired = runtime.advance_pump_clock(0, 90000);
    check_equal(expired.window.tick, 90000);
    check_equal(expired.window.samples, 0);
    check_false(expired.window.ready);
    check_false(expired.fresh);
    check_equal(expired.window_samples, 1u); // The explicit current UNKNOWN frame.
    check_within(expired.window.mean, 0, 1e-12);
    check_within(expired.window.peak, 0, 1e-12);
    auto resumed = runtime.pump(0, reading(91, 4));
    check_false(resumed.window.ready);
    check_within(resumed.window.continuous_seconds, 0, 1e-12);
  }
  it("clears the old source epoch and warms up independent new evidence") {
    Runtime runtime(BOILER_BUNDLE);
    feed(runtime, 0, 59, 4);
    auto input = reading(60);
    input.epoch = 2;
    auto restarted = runtime.pump(0, input);
    check_equal(restarted.window_samples, 1u);
    check_equal(restarted.window.samples, 0);
    check_false(restarted.window.ready);
    check_within(restarted.window.peak, 0, 1e-12);
    PumpOutput output;
    for (int t = 61; t <= 112; ++t) {
      input = reading(t);
      input.epoch = 2;
      output = runtime.pump(0, input);
    }
    check_equal(output.window.samples, 24);
    check_true(output.window.ready);
    check_within(output.window.peak, 0, 1e-12);
    check_less_equal(output.window_samples, 30u);
  }
  it("refuses duplicate backwards and implicit skipped frames before changing the window") {
    Runtime runtime(BOILER_BUNDLE);
    auto first = runtime.pump(0, reading(0));
    check_throws_with(runtime.pump(0, reading(0)), "Pump frames must advance");
    check_throws_with(runtime.pump(0, reading(2)), "Pump frames must advance");
    check_equal(runtime.pump_state(0).window_samples, first.window_samples);
    runtime.advance_pump_clock(0, 1000);
    auto next = runtime.pump(0, reading(2));
    check_equal(next.window_samples, 3u);
    check_throws_with(runtime.advance_pump_clock(0, 1000), "Pump frames must advance");
  }
  it("requires current covered history instead of fabricating a healthy default") {
    Runtime runtime(BOILER_BUNDLE);
    auto missing = runtime.dispatch(DispatchInput{});
    check_equal(missing.action, std::string_view("REQUEST_SUPPORT"));
    check_equal(missing.window_levels[0], std::string_view("UNKNOWN"));
    auto input = ready_dispatch_input();
    input.pumps[0].window.tick = -1000;
    auto stale = runtime.dispatch(input);
    check_equal(stale.window_levels[0], std::string_view("UNKNOWN"));
    check_equal(candidate(stale, "KEEP").reason, std::string_view("PRIMARY_WINDOW_INCOMPLETE"));
    input.pumps[0].window.tick = 1000;
    auto incomplete = runtime.dispatch(input);
    check_equal(incomplete.window_levels[0], std::string_view("UNKNOWN"));
    check_equal(runtime.pending_dispatch_facts(), 0u);
  }
  it("chooses observation or derating from different histories with the same current risk") {
    Runtime runtime(BOILER_BUNDLE);
    auto input = ready_dispatch_input();
    input.demand = 550;
    input.pumps[2].maintenance = true;
    input.pumps[0].risk = 50;
    auto &w = input.pumps[0].window;
    w.mean = 50.0 / 30;
    w.peak = 50;
    w.recent_mean = 50.0 / 15;
    w.trend_delta = w.recent_mean;
    w.trend_per_second = w.trend_delta / 15;
    w.elevated = 1;
    auto isolated = runtime.dispatch(input);
    check_equal(isolated.window_levels[0], std::string_view("TRANSIENT"));
    check_equal(isolated.action, std::string_view("OBSERVE"));
    check_equal(isolated.plan_id, 1);
    check_equal(candidate(isolated, "DERATE").reason, std::string_view("WINDOW_NOT_CONFIRMED"));
    input.pumps[0].window = ready_window(50);
    auto sustained = runtime.dispatch(input);
    check_equal(sustained.window_levels[0], std::string_view("CONFIRMED"));
    check_equal(sustained.action, std::string_view("DERATE"));
  }
  it("uses a covered rising trend in risk cost and ignores an unavailable trend") {
    Runtime runtime(BOILER_BUNDLE);
    auto input = ready_dispatch_input();
    input.demand = 550;
    input.pumps[2].maintenance = true;
    input.pumps[0].risk = 35;
    auto &w = input.pumps[0].window;
    w.mean = 20;
    w.peak = 38;
    w.recent_mean = 35;
    w.prior_mean = 5;
    w.trend_delta = 30;
    w.trend_per_second = 2;
    auto rising = runtime.dispatch(input);
    check_equal(rising.window_levels[0], std::string_view("DEGRADING"));
    check_equal(rising.action, std::string_view("DERATE"));
    check_within(candidate(rising, "DERATE").residual_risk, 24.75, 1e-10);
    w.samples = 24;
    w.prior_samples = 9;
    w.coverage = 80;
    w.mean = 23.75;
    w.trend_ready = false;
    auto unavailable = runtime.dispatch(input);
    check_equal(unavailable.window_levels[0], std::string_view("NORMAL"));
    check_equal(unavailable.action, std::string_view("KEEP"));
    check_within(candidate(unavailable, "KEEP").residual_risk, 35, 1e-12);
  }
  it("demonstrates a spike deterioration outage and evidence rebuilding across nine threads") {
    Config config;
    config.scenario = Scenario::window;
    config.seconds = 240;
    auto report = simulate(BOILER_BUNDLE, config);
    check_equal(report.decisions.size(), 241u);
    check_within(report.frames[60].risk[0], 50, 1e-12);
    check_within(report.frames[130].risk[0], 50, 1e-12);
    check_equal(report.decisions[60].action, std::string_view("OBSERVE"));
    check_equal(report.decisions[130].action, std::string_view("DERATE"));
    check_equal(report.decisions[130].window_levels[0], std::string_view("CONFIRMED"));
    check_equal(report.decisions[160].action, std::string_view("SWITCH"));
    check_equal(report.decisions[173].action, std::string_view("REQUEST_SUPPORT"));
    check_true(report.frames[180].pump_fresh[2]);
    check_equal(report.decisions[180].window_levels[2], std::string_view("UNKNOWN"));
    check_equal(candidate(report.decisions[180], "SWITCH").reason,
                std::string_view("STANDBY_WINDOW_INCOMPLETE"));
    check_equal(report.decisions[232].action, std::string_view("SWITCH"));
    check_less_equal(report.max_window_samples, 30u);
  }
}
