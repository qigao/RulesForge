#include "runtime.hpp"
#include "boiler_room_schema_native.h"
#include "text.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <stdexcept>

namespace boiler_room {
void require(ruleforge_status_t status) {
  if (status != RULES_FORGE_OK)
    throw std::runtime_error(ruleforge_get_last_error_message());
}
namespace {
namespace typed = BoilerRoom_typed;
using Value = std::unique_ptr<DataBindValue, decltype(&data_bind_value_free)>;
void bind_require(DataBindStatus status, DataBindError const &error) {
  if (status == DATA_BIND_ERR_OOM)
    throw std::bad_alloc();
  if (status != DATA_BIND_OK)
    throw std::runtime_error(error.message);
}
struct BoundSchema {
  std::unique_ptr<DataBind, decltype(&data_bind_free)> codec{nullptr, &data_bind_free};
  BoundSchema() {
    DataBind *raw = nullptr;
    DataBindError error{};
    auto status = BoilerRoom_schema_codec()->create(&raw, &error);
    codec.reset(raw);
    bind_require(status, error);
  }
  template <class Owner> Value bind(char const *type, Owner const &object) {
    // DataBind owns both format conversion and schema validation. Generated
    // owners retain their tstr members; the immutable value owns an independent
    // copy and can be staged before any session fact is admitted.
    char *raw = nullptr;
    size_t length = 0;
    DataBindError error{};
    auto status = object.to_json(codec.get(), &raw, &length, &error);
    std::unique_ptr<char, decltype(&data_bind_serialized_free)> text(raw,
                                                                     &data_bind_serialized_free);
    bind_require(status, error);
    return parse(type, {text.get(), length});
  }
  Value parse(char const *type, std::string_view text) {
    DataBindError error{};
    DataBindValue *value = nullptr;
    auto status = data_bind_parse_json(codec.get(), type, text.data(), text.size(), &value, &error);
    Value result(value, &data_bind_value_free);
    bind_require(status, error);
    return result;
  }
};
tstr owned_label(std::string_view value) {
  auto label = canonical_label(value);
  auto result = tstr_from_v(vstr_from_buf(label.data(), label.size()));
  if (!result)
    throw std::bad_alloc();
  return result;
}
void release(ruleforge_status_t status) noexcept {
  if (status != RULES_FORGE_OK) {
    std::fprintf(stderr, "Boiler-room cleanup: %s\n", ruleforge_get_last_error_message());
    std::terminate();
  }
}
WindowEvidence_t window_record(WindowEvidence const &w) {
  return {w.tick,
          w.samples,
          w.elevated,
          w.recent_samples,
          w.prior_samples,
          w.mean,
          w.peak,
          w.recent_mean,
          w.prior_mean,
          w.coverage,
          w.continuous_seconds,
          w.trend_delta,
          w.trend_per_second,
          w.ready,
          w.trend_ready};
}
struct Program {
  ruleforge_program_t handle = nullptr;
  explicit Program(char const *bundle) { require(ruleforge_program_create(bundle, &handle)); }
  ~Program() {
    release(ruleforge_program_close(handle));
    release(ruleforge_program_destroy(handle));
  }
};
struct Query {
  ruleforge_query_result_t handle = nullptr;
  Query(ruleforge_stateful_session_t session, char const *name) {
    require(ruleforge_session_query(session, name, &handle));
  }
  ~Query() { release(ruleforge_query_result_destroy(handle)); }
  int size() const {
    int n = ruleforge_query_result_get_size(handle);
    if (n < 0)
      throw std::runtime_error(ruleforge_get_last_error_message());
    return n;
  }
  ruleforge_fact_t row(int index) const {
    ruleforge_fact_t fact = nullptr;
    require(ruleforge_query_result_get_fact_at_index(handle, index, "row", &fact));
    return fact;
  }
};
double number(ruleforge_fact_t fact, char const *field) {
  double value = 0;
  require(ruleforge_fact_get_field_as_double(fact, field, &value));
  return value;
}
int64_t integer(ruleforge_fact_t fact, char const *field) {
  int64_t value = 0;
  require(ruleforge_fact_get_field_as_int(fact, field, &value));
  return value;
}
bool boolean(ruleforge_fact_t fact, char const *field) {
  int value = 0;
  require(ruleforge_fact_get_field_as_bool(fact, field, &value));
  return value != 0;
}
WindowEvidence read_window(ruleforge_fact_t row) {
  return {integer(row, "tick"),
          integer(row, "samples"),
          integer(row, "elevated"),
          integer(row, "recentSamples"),
          integer(row, "priorSamples"),
          number(row, "mean"),
          number(row, "peak"),
          number(row, "recentMean"),
          number(row, "priorMean"),
          number(row, "coveragePct"),
          number(row, "continuousSeconds"),
          number(row, "trendDelta"),
          number(row, "trendPerSecond"),
          boolean(row, "ready"),
          boolean(row, "trendReady")};
}
std::string_view text(ruleforge_fact_t fact, char const *field) {
  std::array<char, 96> buffer{};
  size_t length = 0;
  require(ruleforge_fact_get_field_as_string(fact, field, buffer.data(), buffer.size(), &length));
  return canonical_label({buffer.data(), length});
}
struct Session {
  BoundSchema &schema;
  ruleforge_stateful_session_t handle = nullptr;
  Session(Program &program, BoundSchema &schema) : schema(schema) {
    require(ruleforge_program_create_session(program.handle, &handle));
  }
  ~Session() { release(ruleforge_session_destroy(handle)); }
  void insert(char const *type, Value const &value) {
    require(ruleforge_session_add_data_bind_value(handle, type, value.get(), nullptr));
  }
  template <class Owner> void insert(char const *type, Owner const &object) {
    insert(type, schema.bind(type, object));
  }
  void fire() {
    int fired = 0;
    require(ruleforge_session_fire_all_rules(handle, 1000, &fired));
    if (fired == 1000)
      throw std::runtime_error("Boiler-room rule firing bound exhausted");
  }
};
void clock_step(Session &session, int64_t tick, bool consecutive) {
  if (tick < 0 || tick > 240000 || tick % frame_ms != 0)
    throw std::invalid_argument("Invalid pump frame time");
  int64_t previous = 0;
  {
    Query q(session.handle, "Clocks");
    if (q.size() != 1)
      throw std::logic_error("Expected one pump clock");
    previous = integer(q.row(0), "tick");
  }
  if (tick <= previous || (consecutive && tick != previous + frame_ms))
    throw std::invalid_argument("Pump frames must advance by one second; explicitly advance the "
                                "clock for missing intervals");
  typed::AdvanceClockOwner step;
  *step.get() = {tick, tick - window_span_ms, tick - window_half_ms};
  session.insert("AdvanceClock", step);
}
constexpr std::array<char const *, 6> asset_names{"B1", "B2", "P1", "P2", "P3", "ROOM"};
} // namespace
struct Runtime::Impl {
  BoundSchema schema;
  Program program;
  std::array<std::unique_ptr<Session>, 6> sessions;
  explicit Impl(char const *bundle, StandbyConnection standby) : program(bundle) {
    if (standby != StandbyConnection::primary_loop && standby != StandbyConnection::secondary_loop)
      throw std::invalid_argument("Unknown standby connection");
    for (size_t index = 0; index < sessions.size(); ++index) {
      sessions[index] = std::make_unique<Session>(program, schema);
      auto &s = *sessions[index];
      typed::ClockOwner clock;
      *clock.get() = {index >= 2 && index < 5 ? -frame_ms : 0, -window_span_ms, -window_half_ms};
      s.insert("Clock", clock);
      if (index < 2 || index == 5) {
        typed::BoilerModelOwner model;
        model->parameters = {1000, 4.18, 9.7};
        s.insert("BoilerModel", model);
      }
      if (index >= 2) {
        typed::WindowPolicyOwner policy;
        *policy.get() = {{window_span_ms, window_half_ms, frame_ms, 2500, 24, 12},
                         40,
                         10,
                         15,
                         40,
                         15,
                         30,
                         40,
                         10};
        s.insert("WindowPolicy", policy);
      }
      auto policy = [&](char const *code, int raise, int clear) {
        typed::AlarmPolicyOwner policy;
        policy->raiseMs = raise * 1000;
        policy->clearMs = clear * 1000;
        policy->code = owned_label(code);
        s.insert("AlarmPolicy", policy);
        typed::AlarmStateOwner state;
        state->code = owned_label(code);
        s.insert("AlarmState", state);
      };
      policy("DATA_STALE", 5, 5);
      if (index < 2) {
        typed::BoilerStateOwner state;
        s.insert("BoilerState", state);
        policy("EFFICIENCY_DEGRADED", 60, 60);
        policy("PLC_REPORTED_FAULT", 0, 0);
      } else if (index < 5) {
        // This is a composite root, whose generated native text conversion is
        // not admitted by SDK 0.1.4. Its fixed initial fixture is bound directly
        // by DataBind; subsequent snapshots use the generated Message owner.
        s.insert(
            "WindowEvidence",
            schema.parse("WindowEvidence",
                         R"({"tick":0,"samples":0,"elevated":0,"recentSamples":0,"priorSamples":0,)"
                         R"("mean":0,"peak":0,"recentMean":0,"priorMean":0,"coveragePct":0,)"
                         R"("continuousSeconds":0,"trendDelta":0,"trendPerSecond":0,)"
                         R"("ready":false,"trendReady":false})"));
        typed::PumpStateOwner state;
        s.insert("PumpState", state);
        policy("PUMP_COMPOSITE_RISK", 20, 60);
        policy("PUMP_RUN_MISMATCH", 10, 10);
      } else {
        typed::RoomStateOwner state;
        s.insert("RoomState", state);
        policy("HEAT_BALANCE_MISMATCH", 30, 60);
        policy("HEAT_SUPPLY_DEFICIT", 120, 60);
        policy("CIRCULATION_INCIDENT", 0, 0);
        for (size_t p = 0; p < 3; ++p) {
          std::string_view loop =
              p == 1 || (p == 2 && standby == StandbyConnection::secondary_loop) ? "B2" : "B1";
          typed::PumpRouteOwner route;
          route->designFlow = 18;
          route->nominalKw = 7.5;
          route->warningFlow = 15;
          route->warningPressure = 120;
          route->primary = p < 2;
          route->pump = owned_label(asset_names[p + 2]);
          route->loop = owned_label(loop);
          s.insert("PumpRoute", route);
        }
        constexpr std::array<char const *, 4> actions{"KEEP", "DERATE", "SWITCH", "STOP"};
        constexpr std::array<double, 4> speeds{1, .85, 1, 0}, factors{1, .45, 1, 0},
            costs{0, 1, 20, 5};
        for (size_t p = 0; p < actions.size(); ++p) {
          typed::PlanSpecOwner plan;
          plan->id = p + 1;
          plan->parameters = {speeds[p], factors[p], costs[p]};
          plan->needsStandby = p == 2;
          plan->action = owned_label(actions[p]);
          plan->standby = owned_label("P3");
          s.insert("PlanSpec", plan);
        }
      }
    }
  }
};
Runtime::Runtime(char const *bundle, StandbyConnection standby)
    : impl_(std::make_unique<Impl>(bundle, standby)) {}
Runtime::~Runtime() = default;
BoilerOutput Runtime::boiler_state(size_t index) {
  Query q(impl_->sessions.at(index)->handle, "Boiler");
  if (q.size() != 1)
    throw std::logic_error("Expected one boiler state");
  auto row = q.row(0);
  return {number(row, "heatKw"),           number(row, "efficiency"), boolean(row, "heatValid"),
          boolean(row, "efficiencyValid"), boolean(row, "fresh"),     boolean(row, "fault")};
}
size_t Runtime::pending_boiler_inputs(size_t index) {
  return Query(impl_->sessions.at(index)->handle, "BoilerInputs").size();
}
BoilerOutput Runtime::boiler(size_t index, BoilerInput const &x) {
  auto &s = *impl_->sessions.at(index);
  typed::BoilerInputOwner input;
  input->reading = {x.tick,      x.water_valid, x.fuel_valid, x.energy_ready,
                    x.supply,    x.return_temp, x.flow,       x.gas_flow,
                    x.water_kwh, x.gas_volume,  x.fuel_epoch, x.fault};
  s.insert("BoilerInput", input);
  s.fire();
  return boiler_state(index);
}
PumpOutput Runtime::pump(size_t index, PumpInput const &x) {
  auto &s = *impl_->sessions.at(index + 2);
  typed::PumpInputOwner input;
  input->reading = {x.tick, x.epoch,    x.sequence, x.fresh,    x.running,
                    x.rms,  x.kurtosis, x.current,  x.pressure, x.flow};
  auto value = s.schema.bind("PumpInput", input);
  clock_step(s, x.tick, true);
  s.insert("PumpInput", value);
  s.fire();
  return pump_state(index);
}
PumpOutput Runtime::advance_pump_clock(size_t index, int64_t tick) {
  auto &s = *impl_->sessions.at(index + 2);
  clock_step(s, tick, false);
  s.fire();
  return pump_state(index);
}
PumpOutput Runtime::pump_state(size_t index) {
  auto &s = *impl_->sessions.at(index + 2);
  Query q(s.handle, "Pump");
  if (q.size() != 1)
    throw std::logic_error("Expected one pump state");
  auto row = q.row(0);
  Query stats(s.handle, "WindowStats");
  if (stats.size() != 1)
    throw std::logic_error("Expected one pump window summary");
  return {number(row, "risk"),
          number(row, "windowMean"),
          number(row, "windowPeak"),
          boolean(row, "fresh"),
          boolean(row, "valid"),
          boolean(row, "mismatch"),
          static_cast<size_t>(Query(s.handle, "RiskSamples").size()),
          read_window(stats.row(0))};
}
void Runtime::room(bool heat_valid, bool demand_valid, double a, double b, double header,
                   double outside, Frame &frame) {
  auto &s = *impl_->sessions[5];
  typed::RoomInputOwner input;
  input->reading = {heat_valid, demand_valid, a, b, header, outside};
  s.insert("RoomInput", input);
  s.fire();
  Query q(s.handle, "Room");
  if (q.size() != 1)
    throw std::logic_error("Expected one room state");
  frame.gap = number(q.row(0), "gapPct");
  frame.deficit = number(q.row(0), "deficitPct");
  frame.demand = number(q.row(0), "demandKw");
  frame.room_valid = boolean(q.row(0), "heatValid");
}
void Runtime::alert(size_t asset, int64_t tick, char const *code, bool valid, bool bad,
                    bool recovered) {
  auto &s = *impl_->sessions.at(asset);
  typed::AlarmInputOwner input;
  input->tick = tick;
  input->valid = valid;
  input->bad = bad;
  input->recovered = recovered;
  input->code = owned_label(code);
  s.insert("AlarmInput", input);
  s.fire();
}
Alarm Runtime::alarm(size_t asset, char const *code) {
  auto label = canonical_label(code);
  Query q(impl_->sessions.at(asset)->handle, "Alarms");
  for (int i = 0; i < q.size(); ++i) {
    auto row = q.row(i);
    if (text(row, "code") == label)
      return {label, integer(row, "phase"), integer(row, "occurrence"), integer(row, "revision"),
              boolean(row, "acknowledged")};
  }
  throw std::logic_error("Unknown example alarm code");
}
bool Runtime::acknowledge(size_t asset, int64_t tick, Alarm const &expected) {
  auto &s = *impl_->sessions.at(asset);
  typed::AckRequestOwner request;
  request->tick = tick;
  request->occurrence = expected.occurrence;
  request->revision = expected.revision;
  request->code = owned_label(expected.code);
  s.insert("AckRequest", request);
  s.fire();
  Query q(s.handle, "AckResults");
  if (q.size() != 1)
    throw std::logic_error("Expected one acknowledgement result before draining");
  return boolean(q.row(0), "accepted");
}
void Runtime::drain(size_t asset, Report &report) {
  auto &s = *impl_->sessions.at(asset);
  {
    Query q(s.handle, "Transitions");
    for (int i = 0; i < q.size(); ++i) {
      if (report.transitions.size() == 128)
        throw std::length_error("Transition output capacity exceeded");
      auto row = q.row(i);
      report.transitions.push_back({integer(row, "tick"), asset_names[asset], text(row, "code"),
                                    integer(row, "kind"), integer(row, "occurrence"),
                                    integer(row, "revision")});
    }
  }
  typed::DrainOwner drain;
  drain->requested = true;
  s.insert("Drain", drain);
  s.fire();
}
DispatchDecision Runtime::dispatch(DispatchInput const &x) {
  auto &s = *impl_->sessions[5];
  if (Query(s.handle, "DispatchRequests").size() != 0)
    throw std::logic_error("Previous dispatch request was not drained");
  if (x.tick < 0 || x.tick > 240000 || x.max_risk < 0 || x.max_risk > 100 || x.warning_risk < 0 ||
      x.warning_risk > 100)
    throw std::invalid_argument("Invalid dispatch tick or risk policy");
  // Build the entire owned input before admitting any fact: serialization errors
  // cannot leave a partially inserted request or a borrowed snapshot behind.
  typed::DispatchRequestOwner input;
  input->tick = x.tick;
  input->thermal = {x.other_heat,    x.available_heat, x.supply,        x.return_temp,
                    x.demand,        x.reserve,        x.other_pump_kw, x.energy_price,
                    x.horizon_hours, x.failure_cost};
  input->maxRisk = x.max_risk;
  input->warningRisk = x.warning_risk;
  input->valid = x.valid;
  input->loop = owned_label("B1");
  auto request = s.schema.bind("DispatchRequest", input);
  std::array<Value, 3> snapshots{Value(nullptr, &data_bind_value_free),
                                 Value(nullptr, &data_bind_value_free),
                                 Value(nullptr, &data_bind_value_free)};
  std::array<Value, 3> windows{Value(nullptr, &data_bind_value_free),
                               Value(nullptr, &data_bind_value_free),
                               Value(nullptr, &data_bind_value_free)};
  for (size_t p = 0; p < snapshots.size(); ++p) {
    auto const &pump = x.pumps[p];
    typed::PumpSnapshotOwner snapshot;
    snapshot->tick = x.tick;
    snapshot->risk = pump.risk;
    snapshot->flow = pump.flow;
    snapshot->pressure = pump.pressure;
    snapshot->fresh = pump.fresh;
    snapshot->running = pump.running;
    snapshot->maintenance = pump.maintenance;
    snapshot->available = pump.available;
    snapshot->pump = owned_label(asset_names[p + 2]);
    snapshots[p] = s.schema.bind("PumpSnapshot", snapshot);
    typed::WindowSnapshotOwner window;
    window->pump = owned_label(asset_names[p + 2]);
    window->metrics = window_record(pump.window);
    windows[p] = s.schema.bind("WindowSnapshot", window);
  }
  s.insert("DispatchRequest", request);
  for (auto const &snapshot : snapshots)
    s.insert("PumpSnapshot", snapshot);
  for (auto const &window : windows)
    s.insert("WindowSnapshot", window);
  s.fire(); // Computation failure propagates; it never fabricates REQUEST_SUPPORT.
  DispatchDecision result;
  for (size_t p = 0; p < x.pumps.size(); ++p)
    result.windows[p] = x.pumps[p].window;
  {
    Query q(s.handle, "WindowAssessments");
    if (q.size() != 3)
      throw std::logic_error("Expected three window assessments");
    for (int n = 0; n < q.size(); ++n) {
      auto row = q.row(n);
      auto name = text(row, "pump");
      if (name.size() != 2 || name[0] != 'P' || name[1] < '1' || name[1] > '3')
        throw std::logic_error("Unknown assessed pump");
      result.window_levels[name[1] - '1'] = text(row, "level");
    }
  }
  {
    Query q(s.handle, "DispatchDecisions");
    if (q.size() != 1)
      throw std::logic_error("Expected one dispatch decision");
    auto row = q.row(0);
    result.tick = integer(row, "tick");
    result.plan_id = integer(row, "planId");
    result.feasible = boolean(row, "feasible");
    result.action = text(row, "action");
    result.reason = text(row, "reason");
    result.score = number(row, "score");
    result.margin = number(row, "marginKw");
  }
  {
    Query q(s.handle, "PlanCandidates");
    if (q.size() != 4)
      throw std::logic_error("Expected exactly four dispatch candidates");
    result.candidates.reserve(4);
    for (int n = 0; n < q.size(); ++n) {
      auto row = q.row(n);
      result.candidates.push_back(
          {integer(row, "planId"), text(row, "action"), text(row, "status"), text(row, "reason"),
           boolean(row, "calculated"), number(row, "capacityKw"), number(row, "marginKw"),
           number(row, "energyKwh"), number(row, "residualRisk"), number(row, "energyCost"),
           number(row, "riskCost"), number(row, "score")});
    }
    std::sort(result.candidates.begin(), result.candidates.end(),
              [](auto const &a, auto const &b) { return a.id < b.id; });
  }
  {
    Query q(s.handle, "DispatchIncidents");
    if (q.size() > 3)
      throw std::length_error("Dispatch incident capacity exceeded");
    result.incidents.reserve(q.size());
    for (int n = 0; n < q.size(); ++n) {
      auto row = q.row(n);
      result.incidents.push_back({text(row, "pump"), text(row, "loop"), number(row, "risk"),
                                  number(row, "flow"), number(row, "pressure")});
    }
  }
  typed::DrainDispatchOwner drain;
  drain->requested = true;
  s.insert("DrainDispatch", drain);
  s.fire();
  return result;
}
size_t Runtime::pending_dispatch_facts() {
  size_t count = 0;
  for (auto name : {"PlanCandidates", "DispatchDecisions", "DispatchIncidents", "DispatchRequests",
                    "PumpSnapshots", "WindowSnapshots", "WindowAssessments"})
    count += Query(impl_->sessions[5]->handle, name).size();
  return count;
}
} // namespace boiler_room
