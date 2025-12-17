#ifndef OBJECT_POOL_HPP
#define OBJECT_POOL_HPP

#include "memory.h"
#include "memory_optimized_types.hpp"

// Global memory arena and pools for shared allocations
class GlobalPools {
public:
  // Get the global arena (1MB default)
  static MemoryArena& arena() {
    static MemoryArena instance(1024 * 1024);
    return instance;
  }

  static ObjectPool<Fact>& fact_pool() {
    static ObjectPool<Fact> pool(arena());
    return pool;
  }

  static ObjectPool<TokenWME>& token_wme_pool() {
    static ObjectPool<TokenWME> pool(arena());
    return pool;
  }

  static ObjectPool<Token>& token_pool() {
    static ObjectPool<Token> pool(arena());
    return pool;
  }

  // Fast allocation helpers - returns PoolPtr with automatic cleanup
  template <typename... Args>
  static auto make_pooled_fact(Args&&... args) {
    return make_pooled<Fact>(fact_pool(), std::forward<Args>(args)...);
  }

  template <typename... Args>
  static auto make_pooled_token_wme(Args&&... args) {
    return make_pooled<TokenWME>(token_wme_pool(), std::forward<Args>(args)...);
  }

  template <typename... Args>
  static auto make_pooled_token(Args&&... args) {
    return make_pooled<Token>(token_pool(), std::forward<Args>(args)...);
  }

  static void clear_all_pools() {
    // Reset arena - all pool memory is freed
    arena().reset();
  }

  static void reserve_all(size_t /*count*/) {
    // No-op: ObjectPool allocates chunks on demand
  }
};

// RAII helper for pool statistics
class PoolStatsCollector {
public:
  struct Stats {
    size_t arena_used;
    size_t arena_available;
    size_t arena_peak;
    size_t string_interner_size;
  };

  static Stats collect() {
    Stats stats{};
    stats.arena_used = GlobalPools::arena().used();
    stats.arena_available = GlobalPools::arena().available();
    stats.arena_peak = GlobalPools::arena().peak();
    stats.string_interner_size = StringInterner::instance().size();
    return stats;
  }

  static std::string format_stats(Stats const& stats) {
    std::ostringstream oss;
    oss << "=== Memory Pool Statistics ===\n";
    oss << "Arena Used:     " << stats.arena_used << " bytes\n";
    oss << "Arena Available: " << stats.arena_available << " bytes\n";
    oss << "Arena Peak:     " << stats.arena_peak << " bytes\n";
    oss << "Interned Strings: " << stats.string_interner_size << "\n";
    return oss.str();
  }
};

#endif // OBJECT_POOL_HPP
