#pragma once
#include "simulation.hpp"
#include <core/vstr.h>
#include <string_view>

namespace boiler_room {
// Returned labels refer to string literals, independently of the input/query.
std::string_view canonical_label(std::string_view value);
struct ScenarioSpec {
  Scenario scenario;
  int seconds;
};
ScenarioSpec parse_scenario(std::string_view value);
// Compares only the supplied bytes, never padding or storage beyond either view.
bool payload_equal(vstr left, vstr right);
} // namespace boiler_room
