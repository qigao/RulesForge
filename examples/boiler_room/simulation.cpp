#include "simulation.hpp"
#include "channel.hpp"
#include "runtime.hpp"
#include "text.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <data_bind.h>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace boiler_room {
namespace {
constexpr int64_t second = 1000, lateness = 3000;
constexpr size_t history_capacity = 64, dedup_capacity = 8192;
constexpr std::array<char const *, source_count> names{
    "B1/PLC",       "B2/PLC",    "P1-P3/VFD",   "P1/vibration",    "P2/vibration",
    "P3/vibration", "B1-B2/gas", "ROOM/header", "ROOM/environment"};
struct Envelope {
  int64_t delivery = 0, event = 0, boot = 1, sequence = 0;
  uint32_t source = 0, word_count = 0, json_size = 0;
  bool retained = false;
  std::array<uint16_t, 16> words{};
  std::array<char, 512> json{};
};
struct Sample {
  int64_t event = 0, boot = 0, sequence = 0;
  bool valid = false;
  std::array<double, 16> values{};
};
using Channel = SourceChannel<Envelope>;
bool has_dispatch(Scenario scenario) {
  return scenario == Scenario::dispatch || scenario == Scenario::window;
}
bool degraded(Config const &c, int t) {
  if (c.scenario == Scenario::window)
    return t >= 160;
  if (c.scenario == Scenario::dispatch)
    return t >= 60 && t < 160;
  return c.scenario == Scenario::circulation && t >= 60 && t < 120;
}
double boiler_flow(Config const &c, size_t boiler, int t) {
  if (c.scenario == Scenario::native_failure && boiler == 0 && t == 70)
    return -1;
  return boiler == 0 ? (degraded(c, t) ? 12.0 : 18.0) : 14.0;
}
double gas_rate(Config const &c, size_t boiler, int t) {
  if (boiler == 1)
    return 37;
  if (c.scenario == Scenario::inefficient)
    return 60;
  return degraded(c, t) ? 32 : 48;
}
double heat(Config const &c, size_t boiler, int t) {
  return 1000 * 4.18 * boiler_flow(c, boiler, t) * 20 / 3600;
}
Envelope make_message(Config const &c, size_t source, int t, std::array<double, 2> gas) {
  Envelope e;
  e.source = static_cast<uint32_t>(source);
  e.delivery = e.event = t * second;
  e.sequence = t + 1;
  e.boot = c.restart_gas_meter && source == 6 && t >= 80 ? 2 : 1;
  std::array<double, 8> values{};
  if (source < 2) {
    e.word_count = 5;
    e.words[0] = 7500;
    e.words[1] = 5500;
    e.words[2] = static_cast<uint16_t>(static_cast<int16_t>(boiler_flow(c, source, t) * 100));
    e.words[3] = 25000;
    e.words[4] = 0;
  } else if (source == 2) {
    e.word_count = 15;
    for (size_t p = 0; p < 3; ++p) {
      bool bad = p == 0 && degraded(c, t), run = p < 2;
      size_t at = p * 5;
      e.words[at] = run ? 5000 : 0;
      e.words[at + 1] = run ? (bad ? 300 : 500) : 0;
      e.words[at + 2] = run ? (bad ? 9000 : 15000) : 0;
      e.words[at + 3] = run ? (bad ? 1200 : 1800) : 0;
      e.words[at + 4] = run ? 1 : 0;
    }
  } else {
    if (source <= 5) {
      bool bad = source == 3 && degraded(c, t);
      values[0] = bad ? 8 : 2;
      values[1] = bad ? 9 : 3;
      if (source == 3 && c.scenario == Scenario::window) {
        if (t == 60)
          values[0] = 8;
        else if (t >= 100 && t < 120)
          values[0] = 2.0 + 2.0 * (t - 100) / 19.0;
        else if (t >= 120 && t < 160)
          values[0] = 4;
      }
    } else if (source == 6) {
      values[0] = gas[0];
      values[1] = gas[1];
      values[2] = gas_rate(c, 0, t);
      values[3] = gas_rate(c, 1, t);
    } else if (source == 7) {
      values[0] = heat(c, 0, t) + heat(c, 1, t);
      if (c.scenario == Scenario::circulation && t >= 65 && t < 120)
        values[0] *= .85;
    } else {
      values[0] = c.scenario == Scenario::cold ? -10 : 0;
      if (c.scenario == Scenario::dispatch)
        values[0] = (740.0 - (t >= 70 && t < 90 ? 550.0 : 700.0)) / 15.0;
      if (c.scenario == Scenario::window)
        values[0] = (740.0 - (t < 160 ? 550.0 : 700.0)) / 15.0;
    }
    int length =
        std::snprintf(e.json.data(), e.json.size(),
                      "{\"values\":[%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g,%.12g]}", values[0],
                      values[1], values[2], values[3], values[4], values[5], values[6], values[7]);
    if (length < 0 || static_cast<size_t>(length) >= e.json.size())
      throw std::length_error("Simulated MQTT payload too large");
    e.json_size = static_cast<uint32_t>(length);
  }
  return e;
}
void produce(Channel &channel, Config const &c, size_t source) {
  std::array<double, 2> gas{1000, 1000};
  std::vector<Envelope> pending;
  pending.reserve(4);
  auto enqueue = [&](Envelope value) {
    if (pending.size() == 4)
      throw std::length_error("Source reorder capacity exceeded");
    pending.push_back(value);
  };
  auto flush = [&](int64_t time) {
    std::stable_sort(pending.begin(), pending.end(), [](auto const &a, auto const &b) {
      return std::tie(a.delivery, a.sequence) < std::tie(b.delivery, b.sequence);
    });
    while (!pending.empty() && pending.front().delivery <= time) {
      if (!channel.send(pending.front()))
        return false;
      pending.erase(pending.begin());
    }
    return true;
  };
  for (int t = 0; t <= c.seconds + 3; ++t) {
    if (t > 0)
      for (size_t b = 0; b < 2; ++b)
        gas[b] += gas_rate(c, b, t - 1) / 3600;
    if (c.restart_gas_meter && t == 80)
      gas = {0, 0};
    if (c.scenario == Scenario::source_failure && source == 3 && t == 70)
      throw std::runtime_error("Injected P1 source failure");
    bool sample = !(source == 6 && t % 5) && !(source == 8 && t % 10);
    if (source == 3 && c.scenario == Scenario::circulation && t >= 100 && t < 120)
      sample = false;
    if (source == 5 && c.scenario == Scenario::dispatch && t >= 130 && t < 135)
      sample = false;
    if (source == 5 && c.scenario == Scenario::window && t >= 170 && t < 180)
      sample = false;
    if (sample) {
      auto e = make_message(c, source, t, gas);
      if (t == 50 && source == 0 &&
          c.payload_failure == Config::PayloadFailure::incomplete_registers)
        --e.word_count;
      if (t == 50 && source == 3 && c.payload_failure == Config::PayloadFailure::malformed_json)
        e.json[0] = '!';
      if (c.transport_faults && t == 0 && source >= 3) {
        auto retained = e;
        retained.retained = true;
        retained.event = -60000;
        retained.sequence = -1;
        enqueue(retained);
      }
      if (c.transport_faults && t == 45)
        e.delivery += 2000;
      enqueue(e);
      if ((c.transport_faults || c.conflicting_duplicate) && t == 40) {
        auto duplicate = e;
        if (c.conflicting_duplicate && source == 0)
          ++duplicate.words[0];
        enqueue(duplicate);
      }
      if (c.transport_faults && t == 130 && source == 3) {
        auto old = make_message(c, source, 64, gas);
        old.delivery = 130000;
        old.sequence = 10000;
        enqueue(old);
      }
    }
    if (!flush(t * second))
      return;
  }
  flush((c.seconds + 6) * second);
}
class Producers {
  std::array<Channel, source_count> channels_;
  std::array<std::thread, source_count> threads_;
  std::mutex gate_mutex_;
  std::condition_variable gate_;
  bool started_ = false;

public:
  // Start after construction so the owner destructor also handles a partially
  // created thread set. No catch/rethrow is needed to roll back thread startup.
  void start(Config const &c) {
    for (size_t source = 0; source < source_count; ++source) {
      threads_[source] = std::thread([&, source, c] {
        {
          std::unique_lock lock(gate_mutex_);
          gate_.wait(lock, [&] { return started_; });
        }
        try {
          produce(channels_[source], c, source);
          channels_[source].close();
        } catch (...) {
          channels_[source].close(std::current_exception());
        }
      });
    }
    {
      std::lock_guard lock(gate_mutex_);
      started_ = true;
    }
    gate_.notify_all();
  }
  ~Producers() { stop(); }
  void stop() {
    for (auto &channel : channels_)
      channel.cancel();
    {
      std::lock_guard lock(gate_mutex_);
      started_ = true;
    }
    gate_.notify_all();
    for (auto &thread : threads_)
      if (thread.joinable())
        thread.join();
  }
  std::optional<Envelope> receive(size_t source) { return channels_[source].receive(); }
};
class Decoder {
  using Codec = std::unique_ptr<DataBind, decltype(&data_bind_free)>;
  Codec codec_{nullptr, &data_bind_free};

public:
  Decoder() {
    constexpr char schema[] = "schema Telemetry [id(22),version(1),byte_order(little)]; "
                              "message Payload { double[8] values; }";
    DataBind *raw = nullptr;
    DataBindError error{};
    if (data_bind_create_from_text(schema, sizeof(schema) - 1, &raw, &error) != DATA_BIND_OK)
      throw std::runtime_error(error.message);
    codec_.reset(raw);
  }
  Sample decode(Envelope const &e) {
    Sample result;
    result.event = e.event;
    result.boot = e.boot;
    result.sequence = e.sequence;
    if (e.source < 3) {
      if (e.word_count != (e.source < 2 ? 5u : 15u))
        throw std::runtime_error("Incomplete Modbus register group");
      for (size_t n = 0; n < e.word_count; ++n)
        result.values[n] = static_cast<int16_t>(e.words[n]) / 100.0;
      if (e.source < 2)
        result.values[4] = e.words[4];
      else
        for (size_t p = 0; p < 3; ++p)
          result.values[p * 5 + 4] = e.words[p * 5 + 4];
    } else {
      DataBindValue *raw = nullptr;
      DataBindError error{};
      auto status =
          data_bind_parse_json(codec_.get(), "Payload", e.json.data(), e.json_size, &raw, &error);
      std::unique_ptr<DataBindValue, decltype(&data_bind_value_free)> value(raw,
                                                                            &data_bind_value_free);
      if (status == DATA_BIND_ERR_OOM)
        throw std::bad_alloc();
      if (status != DATA_BIND_OK)
        throw std::runtime_error(error.message);
      auto values = data_bind_value_get(value.get(), "values");
      for (size_t n = 0; n < 8; ++n) {
        if (data_bind_value_get_double(data_bind_value_at(values, n), &result.values[n]) !=
            DATA_BIND_OK)
          throw std::runtime_error("Telemetry values must be doubles");
        if (!std::isfinite(result.values[n]))
          throw std::runtime_error("Non-finite telemetry value");
      }
    }
    result.valid = true;
    return result;
  }
};
// Full canonical payload is retained, not a hash whose collision could hide a conflict.
bool same_payload(Envelope const &a, Envelope const &b) {
  return a.event == b.event && a.word_count == b.word_count && a.json_size == b.json_size &&
         payload_equal(vstr_from_buf(reinterpret_cast<char const *>(a.words.data()),
                                     a.word_count * sizeof(uint16_t)),
                       vstr_from_buf(reinterpret_cast<char const *>(b.words.data()),
                                     b.word_count * sizeof(uint16_t))) &&
         payload_equal(vstr_from_buf(a.json.data(), a.json_size),
                       vstr_from_buf(b.json.data(), b.json_size));
}
class Inputs {
  Decoder decoder_;
  std::array<std::deque<Sample>, source_count> history_;
  std::map<std::tuple<uint32_t, int64_t, int64_t>, Envelope> seen_;

public:
  void accept(Envelope const &e, int64_t sealed, Report &report) {
    if (e.word_count > e.words.size() || e.json_size > e.json.size())
      throw std::invalid_argument("Source envelope exceeds payload capacity");
    ++report.received.at(e.source);
    if (e.retained) {
      ++report.retained;
      return;
    }
    auto key = std::make_tuple(e.source, e.boot, e.sequence);
    if (auto it = seen_.find(key); it != seen_.end()) {
      if (!same_payload(it->second, e))
        throw std::runtime_error("Conflicting source event ID");
      ++report.duplicates;
      return;
    }
    if (seen_.size() == dedup_capacity)
      throw std::length_error("Deduplication capacity exceeded");
    seen_.emplace(key, e); // Duration is capped at 240 s, within the 300 s identity retention.
    if (e.event <= sealed) {
      ++report.late;
      return;
    }
    auto sample = decoder_.decode(e);
    auto &history = history_[e.source];
    while (!history.empty() && history.front().event < sealed - 35000)
      history.pop_front();
    if (history.size() == history_capacity)
      throw std::length_error("Source history capacity exceeded");
    history.insert(std::upper_bound(history.begin(), history.end(), sample.event,
                                    [](int64_t time, Sample const &x) { return time < x.event; }),
                   sample);
  }
  Sample const *at(size_t source, int64_t tick) const {
    auto const &h = history_[source];
    auto it = std::upper_bound(h.begin(), h.end(), tick,
                               [](int64_t time, Sample const &x) { return time < x.event; });
    return it == h.begin() ? nullptr : &*std::prev(it);
  }
  static bool fresh(Sample const *x, int64_t tick, int64_t age) {
    return x && x->valid && tick >= x->event && tick - x->event <= age;
  }
};
struct EnergyStep {
  int64_t end;
  double kwh;
  bool valid;
};
class RoomOwner {
  Config const &config_;
  Runtime runtime_;
  Inputs inputs_;
  std::array<std::deque<EnergyStep>, 2> energy_;
  std::array<BoilerOutput, 2> previous_{};

public:
  Report report;
  int64_t sealed = -1000;
  RoomOwner(char const *bundle, Config const &c) : config_(c), runtime_(bundle) {
    report.frames.reserve(c.seconds + 1);
    report.transitions.reserve(128);
    if (has_dispatch(c.scenario))
      report.decisions.reserve(c.seconds + 1);
  }
  void accept(Envelope const &e) { inputs_.accept(e, sealed, report); }
  void frame(int64_t tick) {
    Frame f;
    f.tick = tick;
    auto gas = inputs_.at(6, tick);
    bool gas_fresh = Inputs::fresh(gas, tick, 8000);
    for (size_t b = 0; b < 2; ++b) {
      if (tick > 0)
        energy_[b].push_back({tick, previous_[b].heat / 3600, previous_[b].heat_valid});
      while (!energy_[b].empty() && energy_[b].front().end <= tick - 30000)
        energy_[b].pop_front();
      auto water = inputs_.at(b, tick);
      bool water_fresh = Inputs::fresh(water, tick, 2500);
      auto start_gas = inputs_.at(6, tick - 30000);
      bool energy_ready = gas_fresh && gas->event == tick && tick >= 30000 && start_gas &&
                          start_gas->valid && start_gas->event == tick - 30000 &&
                          start_gas->boot == gas->boot && energy_[b].size() == 30;
      double kwh = 0;
      for (auto const &step : energy_[b]) {
        kwh += step.kwh;
        energy_ready &= step.valid;
      }
      double volume = energy_ready ? gas->values[b] - start_gas->values[b] : 0;
      if (energy_ready && volume < 0)
        throw std::runtime_error("Gas counter regressed within one boot epoch");
      BoilerInput in;
      in.tick = tick;
      in.water_valid = water_fresh;
      in.fuel_valid = gas_fresh;
      in.energy_ready = energy_ready;
      in.water_kwh = kwh;
      in.gas_volume = volume;
      in.fuel_epoch = gas ? gas->boot : 0;
      if (water) {
        in.supply = water->values[0];
        in.return_temp = water->values[1];
        in.flow = water->values[2];
        in.fault = water->values[4] != 0;
      }
      if (gas)
        in.gas_flow = gas->values[b + 2];
      previous_[b] = runtime_.boiler(b, in);
      auto const &out = previous_[b];
      f.heat[b] = out.heat;
      f.efficiency[b] = out.efficiency;
      f.heat_valid[b] = out.heat_valid;
      f.efficiency_valid[b] = out.efficiency_valid;
      runtime_.alert(b, tick, "DATA_STALE", true, !out.fresh, out.fresh);
      runtime_.alert(b, tick, "EFFICIENCY_DEGRADED", out.efficiency_valid, out.efficiency < 83,
                     out.efficiency > 87);
      runtime_.alert(b, tick, "PLC_REPORTED_FAULT", water_fresh, out.fault, !out.fault);
    }
    auto vfd = inputs_.at(2, tick);
    DispatchInput dispatch;
    dispatch.tick = tick;
    for (size_t p = 0; p < 3; ++p) {
      auto vibration = inputs_.at(3 + p, tick);
      PumpInput in;
      in.tick = tick;
      in.fresh = Inputs::fresh(vfd, tick, 2500) && Inputs::fresh(vibration, tick, 3000);
      if (vibration) {
        in.epoch = vibration->boot;
        in.sequence = vibration->sequence;
        in.rms = vibration->values[0];
        in.kurtosis = vibration->values[1];
      }
      if (vfd) {
        in.current = vfd->values[p * 5 + 1];
        in.pressure = vfd->values[p * 5 + 2];
        in.flow = vfd->values[p * 5 + 3];
        in.running = vfd->values[p * 5 + 4] != 0;
      }
      auto out = runtime_.pump(p, in);
      f.risk[p] = out.risk;
      f.window_mean[p] = out.mean;
      f.window_peak[p] = out.peak;
      f.pump_fresh[p] = out.fresh;
      dispatch.pumps[p] = {out.risk,
                           in.flow,
                           in.pressure,
                           in.fresh,
                           in.running,
                           p == 2 && ((config_.scenario == Scenario::dispatch && tick < 100000) ||
                                      (config_.scenario == Scenario::window && tick < 140000)),
                           true,
                           out.window};
      report.max_window_samples = std::max(report.max_window_samples, out.window_samples);
      runtime_.alert(p + 2, tick, "DATA_STALE", true, !out.fresh, out.fresh);
      runtime_.alert(p + 2, tick, "PUMP_COMPOSITE_RISK", out.fresh && (out.valid || out.risk < 40),
                     out.risk >= 80, out.risk < 40);
      runtime_.alert(p + 2, tick, "PUMP_RUN_MISMATCH", out.fresh, out.mismatch, !out.mismatch);
    }
    auto header = inputs_.at(7, tick), ambient = inputs_.at(8, tick);
    bool header_fresh = Inputs::fresh(header, tick, 3000),
         ambient_fresh = Inputs::fresh(ambient, tick, 30000);
    bool heat_valid = f.heat_valid[0] && f.heat_valid[1] && header_fresh;
    bool demand_valid = header_fresh && ambient_fresh;
    runtime_.room(heat_valid, demand_valid, f.heat[0], f.heat[1], header ? header->values[0] : 0,
                  ambient ? ambient->values[0] : 0, f);
    runtime_.alert(5, tick, "DATA_STALE", true, !(heat_valid && demand_valid),
                   heat_valid && demand_valid);
    runtime_.alert(5, tick, "HEAT_BALANCE_MISMATCH", heat_valid, std::abs(f.gap) > 8,
                   std::abs(f.gap) < 4);
    runtime_.alert(5, tick, "HEAT_SUPPLY_DEFICIT", demand_valid, f.deficit > 10, f.deficit < 5);
    auto pump_alarm = runtime_.alarm(2, "PUMP_COMPOSITE_RISK");
    auto balance_alarm = runtime_.alarm(5, "HEAT_BALANCE_MISMATCH");
    auto active = [](Alarm const &alarm) { return alarm.phase == 2 || alarm.phase == 3; };
    bool incident = active(pump_alarm) && active(balance_alarm);
    runtime_.alert(5, tick, "CIRCULATION_INCIDENT", true, incident, !incident);
    if (has_dispatch(config_.scenario)) {
      dispatch.valid = heat_valid && demand_valid && previous_[0].fresh && previous_[1].fresh;
      dispatch.other_heat = f.heat[1];
      dispatch.demand = f.demand;
      auto primary = inputs_.at(0, tick);
      if (primary) {
        dispatch.supply = primary->values[0];
        dispatch.return_temp = primary->values[1];
      }
      report.decisions.push_back(runtime_.dispatch(dispatch));
    }
    if (config_.scenario == Scenario::circulation && tick == 110000) {
      report.acknowledgement_accepted = runtime_.acknowledge(2, tick, pump_alarm);
      runtime_.drain(2, report);
      report.stale_acknowledgement_rejected = !runtime_.acknowledge(2, tick, pump_alarm);
    }
    for (size_t asset = 0; asset < 6; ++asset)
      runtime_.drain(asset, report);
    report.frames.push_back(f);
    sealed = tick;
  }
};
} // namespace
char const *source_name(size_t source) { return names.at(source); }
Report simulate(char const *bundle, Config const &c) {
  if (c.seconds < 0 || c.seconds > 240)
    throw std::invalid_argument("Simulation duration must be 0..240 seconds");
  RoomOwner owner(bundle, c);
  Producers
      producers; // Joined before sessions, schemas and storage disappear, even if start fails.
  producers.start(c);
  std::array<std::optional<Envelope>, source_count> heads;
  for (size_t n = 0; n < source_count; ++n)
    heads[n] = producers.receive(n);
  int64_t next_frame = 0;
  while (true) {
    size_t selected = source_count;
    for (size_t n = 0; n < source_count; ++n)
      if (heads[n] &&
          (selected == source_count ||
           std::tie(heads[n]->delivery, n) < std::tie(heads[selected]->delivery, selected)))
        selected = n;
    if (selected == source_count)
      break;
    auto e = *heads[selected];
    while (next_frame <= c.seconds * second && next_frame + lateness < e.delivery) {
      owner.frame(next_frame);
      next_frame += second;
    }
    owner.accept(e);
    heads[selected] = producers.receive(selected);
  }
  while (next_frame <= c.seconds * second) {
    owner.frame(next_frame);
    next_frame += second;
  }
  return std::move(owner.report);
}
} // namespace boiler_room
