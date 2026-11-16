#pragma once
#include <atomic>
#include <cstdint>

// One definition is shared by all node translation units.
namespace rulesforge::rete_detail {
extern std::atomic<uint64_t> g_alpha_checks;
extern std::atomic<uint64_t> g_join_checks;
extern std::atomic<uint64_t> g_compare_calls;
extern std::atomic<uint64_t> g_field_lookups;
}
