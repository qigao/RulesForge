#include "text.hpp"
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

namespace boiler_room {
namespace {
// re2c's sentinel API needs a byte at YYLIMIT. Copy bounded external views so
// even a non-NUL-terminated slice is safe; an embedded NUL cannot match a prefix.
std::array<char, 96> padded(std::string_view value) {
  if (value.size() >= 96)
    throw std::invalid_argument("Boiler-room token exceeds 95 bytes");
  std::array<char, 96> result{};
  if (!value.empty())
    std::memcpy(result.data(), value.data(), value.size());
  return result;
}
} // namespace
std::string_view canonical_label(std::string_view value) {
  auto buffer = padded(value);
  char const *YYCURSOR = buffer.data(), *YYLIMIT = buffer.data() + value.size();
  [[maybe_unused]] char const *YYMARKER = YYCURSOR;
  auto accept = [&](std::string_view label) {
    if (YYCURSOR != YYLIMIT)
      throw std::invalid_argument("Unknown boiler-room label: " + std::string(value));
    return label;
  };
  /*!re2c
        re2c:define:YYCTYPE = char;
        re2c:yyfill:enable = 0;
        re2c:eof = 0;
        "B1" { return accept("B1"); }
        "B2" { return accept("B2"); }
        "P1" { return accept("P1"); }
        "P2" { return accept("P2"); }
        "P3" { return accept("P3"); }
        "ROOM" { return accept("ROOM"); }
        "DATA_STALE" { return accept("DATA_STALE"); }
        "EFFICIENCY_DEGRADED" { return accept("EFFICIENCY_DEGRADED"); }
        "PLC_REPORTED_FAULT" { return accept("PLC_REPORTED_FAULT"); }
        "PUMP_COMPOSITE_RISK" { return accept("PUMP_COMPOSITE_RISK"); }
        "PUMP_RUN_MISMATCH" { return accept("PUMP_RUN_MISMATCH"); }
        "HEAT_BALANCE_MISMATCH" { return accept("HEAT_BALANCE_MISMATCH"); }
        "HEAT_SUPPLY_DEFICIT" { return accept("HEAT_SUPPLY_DEFICIT"); }
        "CIRCULATION_INCIDENT" { return accept("CIRCULATION_INCIDENT"); }
        "KEEP" { return accept("KEEP"); }
        "DERATE" { return accept("DERATE"); }
        "SWITCH" { return accept("SWITCH"); }
        "STOP" { return accept("STOP"); }
        "OBSERVE" { return accept("OBSERVE"); }
        "REQUEST_SUPPORT" { return accept("REQUEST_SUPPORT"); }
        "ACCEPTED" { return accept("ACCEPTED"); }
        "FEASIBLE" { return accept("FEASIBLE"); }
        "NEW" { return accept("NEW"); }
        "PENDING" { return accept("PENDING"); }
        "SCORED" { return accept("SCORED"); }
        "REJECTED" { return accept("REJECTED"); }
        "CONFIRMED" { return accept("CONFIRMED"); }
        "DEGRADING" { return accept("DEGRADING"); }
        "NORMAL" { return accept("NORMAL"); }
        "TRANSIENT" { return accept("TRANSIENT"); }
        "UNKNOWN" { return accept("UNKNOWN"); }
        "INPUT_INCOMPLETE" { return accept("INPUT_INCOMPLETE"); }
        "INSUFFICIENT_HEAT" { return accept("INSUFFICIENT_HEAT"); }
        "ISOLATED_OR_RECOVERING_PEAK" { return accept("ISOLATED_OR_RECOVERING_PEAK"); }
        "MAINTENANCE_LOCK" { return accept("MAINTENANCE_LOCK"); }
        "MINIMUM_FEASIBLE_COST" { return accept("MINIMUM_FEASIBLE_COST"); }
        "NO_SAFE_PLAN" { return accept("NO_SAFE_PLAN"); }
        "PRIMARY_LOCKED" { return accept("PRIMARY_LOCKED"); }
        "PRIMARY_UNAVAILABLE" { return accept("PRIMARY_UNAVAILABLE"); }
        "PRIMARY_WINDOW_INCOMPLETE" { return accept("PRIMARY_WINDOW_INCOMPLETE"); }
        "RISK_LIMIT" { return accept("RISK_LIMIT"); }
        "SECONDARY_UNAVAILABLE" { return accept("SECONDARY_UNAVAILABLE"); }
        "SECONDARY_WINDOW_UNSAFE" { return accept("SECONDARY_WINDOW_UNSAFE"); }
        "STANDBY_STALE" { return accept("STANDBY_STALE"); }
        "STANDBY_UNAVAILABLE" { return accept("STANDBY_UNAVAILABLE"); }
        "STANDBY_WINDOW_INCOMPLETE" { return accept("STANDBY_WINDOW_INCOMPLETE"); }
        "TOPOLOGY_MISMATCH" { return accept("TOPOLOGY_MISMATCH"); }
        "WINDOW_NOT_CONFIRMED" { return accept("WINDOW_NOT_CONFIRMED"); }
        * { throw std::invalid_argument("Unknown boiler-room label: " + std::string(value)); }
        $ { throw std::invalid_argument("Empty boiler-room label"); }
  */
}
ScenarioSpec parse_scenario(std::string_view value) {
  auto buffer = padded(value);
  char const *YYCURSOR = buffer.data(), *YYLIMIT = buffer.data() + value.size();
  [[maybe_unused]] char const *YYMARKER = YYCURSOR;
  auto accept = [&](ScenarioSpec spec) {
    if (YYCURSOR != YYLIMIT)
      throw std::invalid_argument("Unknown simulation scenario");
    return spec;
  };
  /*!re2c
        re2c:define:YYCTYPE = char;
        re2c:yyfill:enable = 0;
        re2c:eof = 0;
        "normal" { return accept({Scenario::normal, 185}); }
        "circulation" { return accept({Scenario::circulation, 185}); }
        "inefficient" { return accept({Scenario::inefficient, 185}); }
        "cold" { return accept({Scenario::cold, 185}); }
        "dispatch" { return accept({Scenario::dispatch, 185}); }
        "window" { return accept({Scenario::window, 240}); }
        * { throw std::invalid_argument("Unknown simulation scenario"); }
        $ { throw std::invalid_argument("Empty simulation scenario"); }
  */
}
} // namespace boiler_room
