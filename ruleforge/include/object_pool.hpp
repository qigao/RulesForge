#ifndef OBJECT_POOL_HPP
#define OBJECT_POOL_HPP

#include <cstdlib>
#include <memory>
#include <mutex>
#include <sstream>
#include <stack>
#include <type_traits>
#include <vector>

#include "memory_optimized_types.hpp"
// Include mimalloc if available, fallback to standard allocator
#ifdef HAS_MIMALLOC
  #include <mimalloc.h>
  #define POOL_MALLOC(size) mi_malloc(size)
  #define POOL_FREE(ptr) mi_free(ptr)
  #define POOL_ALIGNED_ALLOC(size, align) mi_aligned_alloc(align, size)
  #define POOL_ALIGNED_FREE(ptr) mi_free(ptr)
#else
  // Cross-platform aligned allocation
  #ifdef _MSC_VER
    #include <malloc.h>
    #define POOL_MALLOC(size) std::malloc(size)
    #define POOL_FREE(ptr) std::free(ptr)
    #define POOL_ALIGNED_ALLOC(size, align) _aligned_malloc(size, align)
    #define POOL_ALIGNED_FREE(ptr) _aligned_free(ptr)
  #else
    #include <cstdlib>  // For std::aligned_alloc
    #define POOL_MALLOC(size) std::malloc(size)
    #define POOL_FREE(ptr) std::free(ptr)
    #define POOL_ALIGNED_ALLOC(size, align) std::aligned_alloc(align, size)
    #define POOL_ALIGNED_FREE(ptr) std::free(ptr)
  #endif
#endif

// Thread-safe object pool for frequent allocations/deallocations
template<typename T>
class ObjectPool
{
public:
  static_assert(std::is_destructible_v<T>, "T must be destructible");

  explicit ObjectPool(size_t initial_size = 64, size_t max_size = 1024)
      : max_size_(max_size)
  {
    reserve(initial_size);
  }

  ~ObjectPool() { clear(); }

  // Non-copyable, non-movable for thread safety
  ObjectPool(ObjectPool const&) = delete;
  ObjectPool& operator=(ObjectPool const&) = delete;
  ObjectPool(ObjectPool&&) = delete;
  ObjectPool& operator=(ObjectPool&&) = delete;

  template<typename... Args>
  std::unique_ptr<T, void (*)(T*)> acquire(Args&&... args)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    T* ptr = nullptr;
    if (!available_.empty()) {
      ptr = available_.top();
      available_.pop();
    } else {
      ptr = static_cast<T*>(POOL_ALIGNED_ALLOC(sizeof(T), alignof(T)));
      if (!ptr) {
        throw std::bad_alloc();
      }
      allocated_.push_back(ptr);
    }

    // Construct object in place
    new (ptr) T(std::forward<Args>(args)...);

    // Return unique_ptr with function pointer deleter
    return std::unique_ptr<T, void (*)(T*)>(ptr,
                                            [](T* p)
                                            {
                                              // We need to access the pool
                                              // instance, but we can't capture
                                              // 'this' in a function pointer So
                                              // we'll use a static function
                                              // approach
                                              if (p) {
                                                p->~T();
#ifdef HAS_MIMALLOC
                                                POOL_FREE(p);
#elif defined(_MSC_VER)
        POOL_ALIGNED_FREE(p);
#else
        POOL_ALIGNED_FREE(p);
#endif
                                              }
                                            });
  }

  void reserve(size_t count)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    while (available_.size() < count && allocated_.size() < max_size_) {
      T* ptr = static_cast<T*>(POOL_ALIGNED_ALLOC(sizeof(T), alignof(T)));
      if (!ptr) {
        break;
      }

      allocated_.push_back(ptr);
      available_.push(ptr);
    }
  }

  void clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);

    // Clear available stack
    while (!available_.empty()) {
      available_.pop();
    }

    // Free all allocated memory using appropriate deallocator
    for (T* ptr : allocated_) {
#ifdef HAS_MIMALLOC
      POOL_FREE(ptr);
#elif defined(_MSC_VER)
      POOL_ALIGNED_FREE(ptr);
#else
      POOL_ALIGNED_FREE(ptr);
#endif
    }
    allocated_.clear();
  }

  size_t capacity() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return allocated_.size();
  }

  size_t available_count() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return available_.size();
  }

private:
  void release(T* ptr)
  {
    if (!ptr) {
      return;
    }

    // Explicitly destroy the object
    ptr->~T();

    std::lock_guard<std::mutex> lock(mutex_);
    if (available_.size() < max_size_) {
      available_.push(ptr);
    }
    // If pool is full, we just let the object be reclaimed
    // (it's still in allocated_ list for cleanup)
  }

  mutable std::mutex mutex_;
  std::vector<T*> allocated_;  // All allocated memory blocks
  std::stack<T*> available_;  // Available for reuse
  size_t max_size_;
};

// Global pools for common types
class GlobalPools
{
public:
  static ObjectPool<Fact>& fact_pool()
  {
    static ObjectPool<Fact> pool(128, 2048);
    return pool;
  }

  static ObjectPool<TokenWME>& token_wme_pool()
  {
    static ObjectPool<TokenWME> pool(256, 4096);
    return pool;
  }

  static ObjectPool<Token>& token_pool()
  {
    static ObjectPool<Token> pool(256, 4096);
    return pool;
  }

  // Fast allocation helpers
  template<typename... Args>
  static auto make_pooled_fact(Args&&... args)
  {
    return fact_pool().acquire(std::forward<Args>(args)...);
  }

  template<typename... Args>
  static auto make_pooled_token_wme(Args&&... args)
  {
    return token_wme_pool().acquire(std::forward<Args>(args)...);
  }

  template<typename... Args>
  static auto make_pooled_token(Args&&... args)
  {
    return token_pool().acquire(std::forward<Args>(args)...);
  }

  static void clear_all_pools()
  {
    fact_pool().clear();
    token_wme_pool().clear();
    token_pool().clear();
  }

  static void reserve_all(size_t count)
  {
    fact_pool().reserve(count);
    token_wme_pool().reserve(count * 2);  // Usually more tokens than facts
    token_pool().reserve(count * 2);
  }
};

// RAII helper for pool statistics
class PoolStatsCollector
{
public:
  struct Stats
  {
    size_t fact_pool_capacity;
    size_t fact_pool_available;
    size_t token_wme_pool_capacity;
    size_t token_wme_pool_available;
    size_t token_pool_capacity;
    size_t token_pool_available;
    size_t string_interner_size;
  };

  static Stats collect()
  {
    Stats stats {};
    stats.fact_pool_capacity = GlobalPools::fact_pool().capacity();
    stats.fact_pool_available = GlobalPools::fact_pool().available_count();
    stats.token_wme_pool_capacity = GlobalPools::token_wme_pool().capacity();
    stats.token_wme_pool_available =
        GlobalPools::token_wme_pool().available_count();
    stats.token_pool_capacity = GlobalPools::token_pool().capacity();
    stats.token_pool_available = GlobalPools::token_pool().available_count();
    stats.string_interner_size = StringInterner::instance().size();
    return stats;
  }

  static std::string format_stats(Stats const& stats)
  {
    std::ostringstream oss;
    oss << "=== Memory Pool Statistics ===\n";
    oss << "Fact Pool:     " << stats.fact_pool_available << "/"
        << stats.fact_pool_capacity << " available\n";
    oss << "TokenWME Pool: " << stats.token_wme_pool_available << "/"
        << stats.token_wme_pool_capacity << " available\n";
    oss << "Token Pool:    " << stats.token_pool_available << "/"
        << stats.token_pool_capacity << " available\n";
    oss << "Interned Strings: " << stats.string_interner_size << "\n";
    return oss.str();
  }
};

#endif  // OBJECT_POOL_HPP
