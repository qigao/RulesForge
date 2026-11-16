#include "channel.hpp"
#include "dispatch_fixture.hpp"
#include "runtime.hpp"
#include "simulation.hpp"
#include <atomic>
#include <thread>
#include <tinytest.hpp>

using namespace boiler_room;
using namespace boiler_room_test;
namespace {
int64_t transition_time(Report const &r, char const *asset, char const *code, int kind) {
  for (auto const &t : r.transitions)
    if (t.asset == asset && t.code == code && t.kind == kind)
      return t.tick;
  return -1;
}

DispatchInput failing_primary() {
  auto input = ready_dispatch_input();
  input.pumps[0].window = ready_window(100);
  input.pumps[0].risk = 100;
  input.pumps[0].flow = 12;
  input.pumps[0].pressure = 90;
  input.pumps[2].maintenance = true;
  return input;
}
PlanCandidate plan(DispatchDecision const &decision, std::string_view action) {
  for (auto const &candidate : decision.candidates)
    if (candidate.action == action || (action == "OBSERVE" && candidate.action == "KEEP"))
      return candidate;
  throw std::logic_error("Missing dispatch candidate " + std::string(action));
}
} // namespace

suite("Rule constrained boiler-room advice") {
  it("preserves normal supply and chooses the lowest cost feasible plan") {
    Runtime runtime(BOILER_BUNDLE);
    auto decision = runtime.dispatch(ready_dispatch_input());
    check_true(decision.feasible);
    check_equal(decision.action, std::string_view("KEEP"));
    check_within(decision.score, 2.5, 1e-12);
    check_within(plan(decision, "KEEP").capacity, 743.111111111111, 1e-9);
    check_equal(plan(decision, "DERATE").reason, std::string_view("WINDOW_NOT_CONFIRMED"));
    check_equal(plan(decision, "SWITCH").reason, std::string_view("WINDOW_NOT_CONFIRMED"));
    check_empty(decision.incidents);
    check_equal(runtime.pending_dispatch_facts(), 0u);
  }
  it("explains why high demand and a maintenance lock leave no safe plan") {
    Runtime runtime(BOILER_BUNDLE);
    auto decision = runtime.dispatch(failing_primary());
    check_false(decision.feasible);
    check_equal(decision.action, std::string_view("REQUEST_SUPPORT"));
    check_equal(decision.reason, std::string_view("NO_SAFE_PLAN"));
    check_equal(plan(decision, "SWITCH").reason, std::string_view("MAINTENANCE_LOCK"));
    check_false(plan(decision, "SWITCH").calculated);
    check_equal(plan(decision, "DERATE").reason, std::string_view("INSUFFICIENT_HEAT"));
    check_equal(plan(decision, "STOP").reason, std::string_view("INSUFFICIENT_HEAT"));
    check_equal(decision.incidents.size(), 1u);
    check_equal(decision.incidents[0].pump, std::string("P1"));
    check_equal(decision.incidents[0].loop, std::string("B1"));
  }
  it("permits derating only when both heat reserve and residual risk meet policy") {
    Runtime runtime(BOILER_BUNDLE);
    auto input = failing_primary();
    input.demand = 550;
    auto decision = runtime.dispatch(input);
    auto const &derate = plan(decision, "DERATE");
    check_equal(decision.action, std::string_view("DERATE"));
    check_within(derate.capacity, 561.977777777778, 1e-9);
    check_within(derate.margin, 1.977777777778, 1e-9);
    check_within(derate.energy_kwh, 2.401484375, 1e-12);
    check_within(derate.residual_risk, 45.0, 1e-12);
    check_within(derate.risk_cost, 11.25, 1e-12);
    check_within(decision.score, 14.1711875, 1e-10);
    check_equal(plan(decision, "KEEP").reason, std::string_view("RISK_LIMIT"));
    input.max_risk = 40;
    auto stricter = runtime.dispatch(input);
    check_equal(stricter.action, std::string_view("REQUEST_SUPPORT"));
    check_equal(plan(stricter, "DERATE").reason, std::string_view("RISK_LIMIT"));
  }
  it("changes the recommendation when failure cost changes while two plans remain feasible") {
    Runtime runtime(BOILER_BUNDLE);
    auto input = failing_primary();
    input.demand = 550;
    input.pumps[2].maintenance = false;
    auto inexpensive_risk = runtime.dispatch(input);
    check_equal(inexpensive_risk.action, std::string_view("DERATE"));
    check_equal(plan(inexpensive_risk, "SWITCH").status, std::string_view("ACCEPTED"));
    input.failure_cost = 500;
    auto expensive_risk = runtime.dispatch(input);
    check_equal(expensive_risk.action, std::string_view("SWITCH"));
    check_equal(plan(expensive_risk, "DERATE").status, std::string_view("ACCEPTED"));
    check_within(expensive_risk.score, 22.5, 1e-12);
    check_less(expensive_risk.score, plan(expensive_risk, "DERATE").score);
    check_equal(runtime.pending_dispatch_facts(), 0u);
  }
  it("rejects a standby on the wrong circuit before computing its proposal") {
    Runtime runtime(BOILER_BUNDLE, StandbyConnection::secondary_loop);
    auto input = failing_primary();
    input.pumps[2].maintenance = false;
    auto decision = runtime.dispatch(input);
    check_equal(decision.action, std::string_view("REQUEST_SUPPORT"));
    check_equal(plan(decision, "SWITCH").reason, std::string_view("TOPOLOGY_MISMATCH"));
    check_false(plan(decision, "SWITCH").calculated);
  }
  it("breaks equal feasible costs by stable plan id") {
    Runtime runtime(BOILER_BUNDLE);
    auto input = ready_dispatch_input();
    input.pumps[0].risk = 40;
    input.pumps[0].window = ready_window(80);
    input.max_risk = 80;
    input.failure_cost = 100;
    auto decision = runtime.dispatch(input);
    check_within(plan(decision, "KEEP").score, 22.5, 1e-12);
    check_equal(plan(decision, "KEEP").score, plan(decision, "SWITCH").score);
    check_equal(decision.action, std::string_view("KEEP"));
    check_equal(decision.plan_id, 1);
    check_equal(plan(decision, "SWITCH").status, std::string_view("ACCEPTED"));
  }
  it("rejects unhealthy standby vibration without treating its zero flow as a running fault") {
    Runtime runtime(BOILER_BUNDLE);
    PumpInput reading;
    reading.fresh = true;
    reading.running = false;
    reading.rms = 8;
    reading.kurtosis = 9;
    reading.epoch = 1;
    reading.sequence = 1;
    auto observation = runtime.pump(2, reading);
    check_within(observation.risk, 100.0, 1e-12);
    check_false(observation.mismatch);
    auto input = failing_primary();
    input.pumps[2].maintenance = false;
    input.pumps[2].risk = observation.risk;
    input.pumps[2].fresh = observation.fresh;
    auto decision = runtime.dispatch(input);
    check_equal(decision.action, std::string_view("REQUEST_SUPPORT"));
    check_equal(plan(decision, "SWITCH").reason, std::string_view("RISK_LIMIT"));
  }
  it("uses fresh available standby evidence and forbids double counting an already running pump") {
    Runtime runtime(BOILER_BUNDLE);
    auto input = failing_primary();
    input.pumps[2].maintenance = false;
    input.pumps[2].fresh = false;
    check_equal(plan(runtime.dispatch(input), "SWITCH").reason, std::string_view("STANDBY_STALE"));
    input.pumps[2].fresh = true;
    input.pumps[2].available = false;
    check_equal(plan(runtime.dispatch(input), "SWITCH").reason, std::string_view("STANDBY_UNAVAILABLE"));
    input.pumps[2].available = true;
    input.pumps[2].running = true;
    check_equal(plan(runtime.dispatch(input), "SWITCH").reason, std::string_view("STANDBY_UNAVAILABLE"));
    input.pumps[2].running = false;
    check_equal(runtime.dispatch(input).action, std::string_view("SWITCH"));
  }
  it("gates missing thermal primary and secondary evidence instead of using default zeros") {
    Runtime runtime(BOILER_BUNDLE);
    for (int kind = 0; kind < 4; ++kind) {
      auto input = ready_dispatch_input();
      if (kind == 0)
        input.valid = false;
      if (kind == 1)
        input.pumps[0].fresh = false;
      if (kind == 2)
        input.pumps[1].fresh = false;
      if (kind == 3)
        input.pumps[1].risk = 100;
      auto decision = runtime.dispatch(input);
      check_false(decision.feasible);
      for (auto const &candidate : decision.candidates) {
        check_false(candidate.calculated);
        check_equal(candidate.status, std::string_view("REJECTED"));
      }
      check_equal(runtime.pending_dispatch_facts(), 0u);
    }
  }
  it("allows stopping only when the remaining circuit can supply demand plus reserve") {
    Runtime runtime(BOILER_BUNDLE);
    auto input = failing_primary();
    input.demand = 300;
    auto decision = runtime.dispatch(input);
    check_equal(decision.action, std::string_view("STOP"));
    check_within(decision.score, 6.0, 1e-12);
    check_within(plan(decision, "STOP").capacity, 325.111111111111, 1e-9);
    input.pumps[0].maintenance = true;
    auto locked = runtime.dispatch(input);
    check_equal(locked.action, std::string_view("STOP"));
    check_equal(plan(locked, "DERATE").reason, std::string_view("PRIMARY_LOCKED"));
  }
  it("propagates script and native failures without fabricating a support recommendation") {
    for (int kind = 0; kind < 2; ++kind) {
      Runtime runtime(BOILER_BUNDLE);
      auto input = ready_dispatch_input();
      if (kind == 0)
        input.failure_cost = -1;
      else
        input.return_temp = 80;
      check_throws_with(runtime.dispatch(input), "binding=dispatch.assess");
      check_greater(runtime.pending_dispatch_facts(), 0u);
      check_throws_with(runtime.dispatch(ready_dispatch_input()),
                        "Previous dispatch request was not drained");
    }
  }
  it("replays the nine-thread operational conflict with exact advice changes and bounded facts") {
    Config config;
    config.scenario = Scenario::dispatch;
    auto report = simulate(BOILER_BUNDLE, config);
    check_equal(report.decisions.size(), 186u);
    constexpr std::array<int, 9> ticks{0, 52, 70, 90, 100, 132, 133, 135, 160};
    const std::array<std::string_view, 9> expected{"REQUEST_SUPPORT", "KEEP",   "DERATE",
                                              "REQUEST_SUPPORT", "SWITCH", "SWITCH",
                                              "REQUEST_SUPPORT", "SWITCH", "SWITCH"};
    for (size_t n = 0; n < ticks.size(); ++n)
      check_equal(report.decisions[ticks[n]].action, expected[n]);
    for (auto const &decision : report.decisions) {
      check_equal(decision.candidates.size(), 4u);
      if (!decision.feasible)
        continue;
      auto const &chosen = plan(decision, decision.action);
      check_equal(chosen.status, std::string_view("ACCEPTED"));
      check_greater_equal(chosen.margin, 0.0);
      check_less_equal(chosen.residual_risk, 50.0);
      for (auto const &candidate : decision.candidates)
        if (candidate.status == "ACCEPTED")
          check_less_equal(decision.score, candidate.score);
    }
    auto repeat = simulate(BOILER_BUNDLE, config);
    for (size_t n = 0; n < report.decisions.size(); ++n) {
      check_equal(report.decisions[n].action, repeat.decisions[n].action);
      check_within(report.decisions[n].score, repeat.decisions[n].score, 1e-12);
    }
  }
}
suite("Nine-source boiler-room simulation") {
  it("runs independent bounded channels through many wraps without losing records") {
    SourceChannel<std::array<int64_t, 16>> channel;
    std::atomic<bool> producer_ok{true};
    std::thread producer([&] {
      for (int64_t n = 0; n < 10000; ++n) {
        std::array<int64_t, 16> record{};
        record.fill(n);
        if (!channel.send(record)) {
          producer_ok = false;
          break;
        }
      }
      channel.close();
    });
    // Assertions run after joining, so a failed assertion cannot strand a producer.
    bool ordered = true;
    int64_t count = 0;
    while (auto record = channel.receive()) {
      for (auto value : *record)
        ordered &= value == count;
      ++count;
    }
    producer.join();
    check_true(producer_ok.load());
    check_true(ordered);
    check_equal(count, 10000);
  }
  it("cancels a full channel and wakes a blocked producer") {
    SourceChannel<std::array<int64_t, 256>> channel;
    std::atomic<int> attempts{0};
    std::atomic<bool> cancelled{false};
    std::thread producer([&] {
      std::array<int64_t, 256> value{};
      do {
        ++attempts;
      } while (channel.send(value));
      cancelled = true;
    });
    // The byte ring reserves one byte; fifteen 2-KiB records fit. The
    // sixteenth attempt cannot complete until the owner reads or cancels.
    while (attempts.load() < 16)
      std::this_thread::yield();
    channel.cancel();
    producer.join();
    check_true(cancelled.load());
    check_false(channel.receive().has_value());
  }
  it("drains queued values before propagating the original producer failure") {
    SourceChannel<int64_t> channel;
    check_true(channel.send(42));
    channel.close(std::make_exception_ptr(std::runtime_error("source offline")));
    auto value = channel.receive();
    check_true(value.has_value());
    check_equal(*value, 42);
    check_throws_with(channel.receive(), "source offline");
  }
  it("wakes an empty-channel receiver on cancellation") {
    SourceChannel<int64_t> channel;
    std::atomic<bool> empty{false};
    std::thread consumer([&] { empty = !channel.receive().has_value(); });
    channel.cancel();
    consumer.join();
    check_true(empty.load());
  }
  it("computes normal thermal efficiency over aligned thirty second energy windows") {
    Config c;
    c.seconds = 60;
    c.scenario = Scenario::normal;
    auto r = simulate(BOILER_BUNDLE, c);
    check_equal(r.frames.size(), 61u);
    check_empty(r.transitions);
    check_false(r.frames[29].efficiency_valid[0]);
    check_true(r.frames[30].efficiency_valid[0]);
    check_within(r.frames[30].heat[0], 418.0, 1e-9);
    check_within(r.frames[30].efficiency[0], 100 * 418.0 / (48 * 9.7), 1e-5);
    check_equal(r.max_window_samples, 30u);
    check_greater(r.duplicates, 0u);
    check_equal(r.retained, 6u);
    check_true((r.received == std::array<size_t, source_count>{65, 65, 65, 66, 66, 66, 15, 66, 9}));
    c.transport_faults = false;
    auto clean = simulate(BOILER_BUNDLE, c);
    for (size_t n = 0; n < r.frames.size(); ++n) {
      check_within(r.frames[n].heat[0], clean.frames[n].heat[0], 1e-9);
      check_within(r.frames[n].efficiency[0], clean.frames[n].efficiency[0], 1e-9);
      check_within(r.frames[n].window_mean[0], clean.frames[n].window_mean[0], 1e-9);
    }
  }
  it("correlates pump and header faults then acknowledges and clears after recovery") {
    Config c;
    auto r = simulate(BOILER_BUNDLE, c);
    check_equal(transition_time(r, "P1", "PUMP_COMPOSITE_RISK", 1), 80000);
    check_equal(transition_time(r, "ROOM", "HEAT_BALANCE_MISMATCH", 1), 95000);
    check_equal(transition_time(r, "ROOM", "CIRCULATION_INCIDENT", 1), 95000);
    check_equal(transition_time(r, "P1", "DATA_STALE", 1), 108000);
    check_equal(transition_time(r, "P1", "PUMP_COMPOSITE_RISK", 3), 110000);
    check_equal(transition_time(r, "P1", "PUMP_COMPOSITE_RISK", 2), 180000);
    check_equal(transition_time(r, "ROOM", "CIRCULATION_INCIDENT", 2), 180000);
    check_true(r.acknowledgement_accepted);
    check_true(r.stale_acknowledgement_rejected);
    check_false(r.frames[103].pump_fresh[0]);
    check_true(r.frames[103].pump_fresh[1]);
    check_within(r.frames[95].gap, 15.0, 1e-8);
    check_equal(r.late, 1u);
    check_less_equal(r.max_window_samples, 30u);
    auto again = simulate(BOILER_BUNDLE, c);
    check_true(r.transitions == again.transitions);
    check_true(r.received == again.received);
  }
  it("separates efficiency degradation and weather-driven capacity deficit") {
    Config c;
    c.scenario = Scenario::inefficient;
    c.seconds = 125;
    auto low = simulate(BOILER_BUNDLE, c);
    check_equal(transition_time(low, "B1", "EFFICIENCY_DEGRADED", 1), 90000);
    check_equal(transition_time(low, "P1", "PUMP_COMPOSITE_RISK", 1), -1);
    c.scenario = Scenario::cold;
    auto cold = simulate(BOILER_BUNDLE, c);
    check_equal(transition_time(cold, "ROOM", "HEAT_SUPPLY_DEFICIT", 1), 120000);
    check_equal(transition_time(cold, "ROOM", "HEAT_BALANCE_MISMATCH", 1), -1);
  }
  it("fails immediately on incomplete register groups") {
    Config c;
    c.scenario = Scenario::normal;
    c.seconds = 60;
    c.payload_failure = Config::PayloadFailure::incomplete_registers;
    check_throws_with(simulate(BOILER_BUNDLE, c), "Incomplete Modbus register group");
  }
  it("rejects conflicting business IDs and propagates producer errors without hanging") {
    Config c;
    c.seconds = 80;
    c.conflicting_duplicate = true;
    check_throws_with(simulate(BOILER_BUNDLE, c), "Conflicting source event ID");
    c.conflicting_duplicate = false;
    c.scenario = Scenario::source_failure;
    check_throws_with(simulate(BOILER_BUNDLE, c), "Injected P1 source failure");
  }
  it("invalidates efficiency across a gas-meter restart until both endpoints share an epoch") {
    Config c;
    c.scenario = Scenario::normal;
    c.seconds = 115;
    c.restart_gas_meter = true;
    auto r = simulate(BOILER_BUNDLE, c);
    check_true(r.frames[79].efficiency_valid[0]);
    check_false(r.frames[80].efficiency_valid[0]);
    check_false(r.frames[109].efficiency_valid[0]);
    check_true(r.frames[110].efficiency_valid[0]);
  }
  it("rolls back a failed native formula and cancels the nine producers on owner failure") {
    Runtime runtime(BOILER_BUNDLE);
    BoilerInput input;
    input.water_valid = input.fuel_valid = true;
    input.supply = 75;
    input.return_temp = 55;
    input.flow = 18;
    auto before = runtime.boiler(0, input);
    input.tick = 1000;
    input.flow = -1;
    check_throws_with(runtime.boiler(0, input), "binding=thermal.calculate");
    check_equal(runtime.pending_boiler_inputs(0), 1u);
    check_within(runtime.boiler_state(0).heat, before.heat, 1e-9);
    Config c;
    c.seconds = 80;
    c.scenario = Scenario::native_failure;
    check_throws_with(simulate(BOILER_BUNDLE, c), "binding=thermal.calculate");
  }
  it("rejects unbounded durations before creating threads") {
    Config c;
    c.seconds = 241;
    check_throws_as(simulate(BOILER_BUNDLE, c), std::invalid_argument);
  }
}
