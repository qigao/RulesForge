#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace boiler_room {
inline constexpr size_t source_count = 9;
enum class Scenario {
  normal,
  circulation,
  inefficient,
  cold,
  dispatch,
  window,
  native_failure,
  source_failure
};
struct Config {
  int seconds = 185; // Logical time, no wall-clock sleeping. Bounded to 0..240.
  Scenario scenario = Scenario::circulation;
  bool transport_faults = true; // Duplicate, retained, reordered and too-late messages.
  enum class PayloadFailure { none, incomplete_registers, malformed_json };
  PayloadFailure payload_failure = PayloadFailure::none;
  bool conflicting_duplicate = false;
  bool restart_gas_meter = false;
};
struct Alarm {
  std::string_view code; // Canonical label in static storage, never a query view.
  int64_t phase = 0, occurrence = 0, revision = 0;
  bool acknowledged = false;
};
struct Transition {
  int64_t tick = 0;
  std::string_view asset, code;
  int64_t kind = 0, occurrence = 0, revision = 0;
  bool operator==(Transition const &) const = default;
};
struct Frame {
  int64_t tick = 0;
  std::array<double, 2> heat{}, efficiency{};
  std::array<bool, 2> heat_valid{}, efficiency_valid{};
  std::array<double, 3> risk{}, window_mean{}, window_peak{};
  std::array<bool, 3> pump_fresh{};
  double gap = 0, deficit = 0, demand = 0;
  bool room_valid = false;
};
struct PlanCandidate {
  int64_t id = 0;
  std::string_view action, status, reason;
  bool calculated = false;
  double capacity = 0, margin = 0, energy_kwh = 0, residual_risk = 0;
  double energy_cost = 0, risk_cost = 0, score = 0;
};
struct WindowEvidence {
  int64_t tick = 0, samples = 0, elevated = 0, recent_samples = 0, prior_samples = 0;
  double mean = 0, peak = 0, recent_mean = 0, prior_mean = 0;
  double coverage = 0, continuous_seconds = 0, trend_delta = 0, trend_per_second = 0;
  bool ready = false, trend_ready = false;
};
struct DispatchIncident {
  std::string_view pump, loop;
  double risk = 0, flow = 0, pressure = 0;
};
struct DispatchDecision {
  int64_t tick = 0, plan_id = 0;
  bool feasible = false;
  std::string_view action, reason;
  double score = 0, margin = 0;
  std::array<WindowEvidence, 3> windows{};
  std::array<std::string_view, 3> window_levels{};
  std::vector<PlanCandidate> candidates;   // Exactly four, in plan-id order.
  std::vector<DispatchIncident> incidents; // At most three.
};
struct Report {
  std::vector<Frame> frames;               // At most 241; retained as deterministic example output.
  std::vector<Transition> transitions;     // At most 128, fail on exhaustion.
  std::vector<DispatchDecision> decisions; // At most 241 counterfactual proposals.
  std::array<size_t, source_count> received{};
  size_t duplicates = 0, retained = 0, late = 0;
  size_t max_window_samples = 0;
  bool acknowledgement_accepted = false, stale_acknowledgement_rejected = false;
};
Report simulate(char const *bundle, Config const &config = {});
char const *source_name(size_t source);
} // namespace boiler_room
