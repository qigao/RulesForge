#pragma once
#include "runtime.hpp"

namespace boiler_room_test {
// Explicit synthetic history for constraint/cost tests. Production simulation
// obtains these fields only by querying the owning pump's RETE window.
inline boiler_room::WindowEvidence ready_window(double risk = 0) {
  boiler_room::WindowEvidence w;
  w.samples = 30;
  w.recent_samples = w.prior_samples = 15;
  w.elevated = risk >= 40 ? 30 : 0;
  w.mean = w.peak = w.recent_mean = w.prior_mean = risk;
  w.coverage = 100;
  w.continuous_seconds = risk >= 40 ? 29 : 0;
  w.ready = w.trend_ready = true;
  return w;
}
inline boiler_room::DispatchInput ready_dispatch_input() {
  boiler_room::DispatchInput input;
  for (auto &pump : input.pumps)
    pump.window = ready_window();
  return input;
}
} // namespace boiler_room_test
