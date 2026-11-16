#pragma once
#include <cstdint>

namespace rulesforge::rete_prof {
struct Stats {
  uint64_t alpha_checks = 0;
  uint64_t join_checks = 0;
  uint64_t compare_calls = 0;
  uint64_t field_lookups = 0;
};

void reset_stats();
Stats get_stats();
} // namespace rulesforge::rete_prof
