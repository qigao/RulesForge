#include "diagnostics/rete_profiler.hpp"
#include "diagnostics/rete_counters.hpp"

namespace rulesforge::rete_detail {
std::atomic<uint64_t> g_alpha_checks{0};
std::atomic<uint64_t> g_join_checks{0};
std::atomic<uint64_t> g_compare_calls{0};
std::atomic<uint64_t> g_field_lookups{0};
}

namespace rulesforge::rete_prof {
using namespace rulesforge::rete_detail;
void reset_stats() {
    g_alpha_checks.store(0, std::memory_order_relaxed);
    g_join_checks.store(0, std::memory_order_relaxed);
    g_compare_calls.store(0, std::memory_order_relaxed);
    g_field_lookups.store(0, std::memory_order_relaxed);
}

Stats get_stats() {
    Stats s;
    s.alpha_checks = g_alpha_checks.load(std::memory_order_relaxed);
    s.join_checks = g_join_checks.load(std::memory_order_relaxed);
    s.compare_calls = g_compare_calls.load(std::memory_order_relaxed);
    s.field_lookups = g_field_lookups.load(std::memory_order_relaxed);
    return s;
}
}  // namespace rulesforge::rete_prof
