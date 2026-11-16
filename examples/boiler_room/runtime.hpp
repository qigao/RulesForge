#pragma once
#include "rules_forge.h"
#include "simulation.hpp"
#include <memory>

namespace boiler_room {
inline constexpr int64_t window_span_ms = 30000, window_half_ms = 15000, frame_ms = 1000;
void require(ruleforge_status_t status);
struct BoilerInput {
  int64_t tick = 0, fuel_epoch = 0;
  bool water_valid = false, fuel_valid = false, energy_ready = false, fault = false;
  double supply = 0, return_temp = 0, flow = 0, gas_flow = 0, water_kwh = 0, gas_volume = 0;
};
struct BoilerOutput {
  double heat = 0, efficiency = 0;
  bool heat_valid = false, efficiency_valid = false, fresh = false, fault = false;
};
struct PumpInput {
  int64_t tick = 0, epoch = 0, sequence = 0;
  bool fresh = false, running = false;
  double rms = 0, kurtosis = 0, current = 0, pressure = 0, flow = 0;
};
struct PumpOutput {
  double risk = 0, mean = 0, peak = 0;
  bool fresh = false, valid = false, mismatch = false;
  size_t window_samples = 0;
  WindowEvidence window;
};
enum class StandbyConnection { primary_loop, secondary_loop };
struct DispatchPump {
  double risk = 0, flow = 18, pressure = 150;
  bool fresh = true, running = true, maintenance = false, available = true;
  WindowEvidence window;
};
struct DispatchInput {
  int64_t tick = 0;
  bool valid = true;
  double other_heat = 1000 * 4.18 * 14 * 20 / 3600, available_heat = 418;
  double supply = 75, return_temp = 55, demand = 700, reserve = 10;
  double other_pump_kw = 5, energy_price = .8, horizon_hours = .25, failure_cost = 100;
  double max_risk = 50, warning_risk = 80;
  std::array<DispatchPump, 3> pumps{};
  DispatchInput() { pumps[2].running = false; }
};
// Every method, including destruction, runs on the room-owner thread.
class Runtime {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  explicit Runtime(char const *bundle, StandbyConnection standby = StandbyConnection::primary_loop);
  ~Runtime();
  Runtime(Runtime const &) = delete;
  BoilerOutput boiler(size_t index, BoilerInput const &input);
  PumpOutput pump(size_t index, PumpInput const &input);
  PumpOutput pump_state(size_t index);
  PumpOutput advance_pump_clock(size_t index, int64_t tick);
  void room(bool heat_valid, bool demand_valid, double a, double b, double header, double outside,
            Frame &frame);
  void alert(size_t asset, int64_t tick, char const *code, bool valid, bool bad, bool recovered);
  Alarm alarm(size_t asset, char const *code);
  bool acknowledge(size_t asset, int64_t tick, Alarm const &expected);
  void drain(size_t asset, Report &report);
  DispatchDecision dispatch(DispatchInput const &input);
  size_t pending_dispatch_facts();
  // Used by the formal rollback test through the same example runtime.
  size_t pending_boiler_inputs(size_t index);
  BoilerOutput boiler_state(size_t index);
};
} // namespace boiler_room
