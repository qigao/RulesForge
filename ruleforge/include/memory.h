#ifndef MEMORY_H
#define MEMORY_H

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>
extern "C" {
#include "memory_pool.h"
}

// ============================================================================
// Modern C++ Memory Pool Wrapper
// ============================================================================

/**
 * @brief RAII wrapper for C memory pool
 *
 * Features:
 * - Zero-copy allocations
 * - Cache-friendly contiguous memory
 * - Stack-style mark/rewind for temporary allocations
 * - Statistics tracking
 */
class MemoryArena {
public:
  explicit MemoryArena(size_t size = 1024 * 1024) // 1MB default
      : pool_(pool_create(size), pool_destroy) {
    if (!pool_) {
      throw std::bad_alloc();
    }
  }

  // Non-copyable, movable
  MemoryArena(const MemoryArena &) = delete;
  MemoryArena &operator=(const MemoryArena &) = delete;
  MemoryArena(MemoryArena &&) = default;
  MemoryArena &operator=(MemoryArena &&) = default;

  /**
   * @brief Allocate object from pool (placement new)
   */
  template <typename T, typename... Args> T *allocate(Args &&...args) {
    void *ptr = pool_alloc(pool_.get(), sizeof(T));
    if (!ptr) {
      throw std::bad_alloc();
    }
    return new (ptr) T(std::forward<Args>(args)...);
  }

  /**
   * @brief Allocate array from pool
   */
  template <typename T> T *allocate_array(size_t count) {
    void *ptr = pool_alloc(pool_.get(), sizeof(T) * count);
    if (!ptr) {
      throw std::bad_alloc();
    }
    // Default construct each element
    T *array = static_cast<T *>(ptr);
    for (size_t i = 0; i < count; ++i) {
      new (&array[i]) T();
    }
    return array;
  }

  /**
   * @brief Reset pool (reuse all memory)
   * WARNING: Does NOT call destructors!
   */
  void reset() { pool_reset(pool_.get()); }

  /**
   * @brief Mark current position for stack-style allocation
   */
  size_t mark() const { return pool_mark(pool_.get()); }

  /**
   * @brief Rewind to previous mark
   */
  void rewind(size_t mark) { pool_rewind(pool_.get(), mark); }

  // Statistics
  size_t used() const { return pool_get_used(pool_.get()); }
  size_t available() const { return pool_get_available(pool_.get()); }
  size_t peak() const { return pool_get_peak(pool_.get()); }

  MemoryPool *raw() { return pool_.get(); }

private:
  std::unique_ptr<MemoryPool, decltype(&pool_destroy)> pool_;
};

// ============================================================================
// RAII Scope Guard for Mark/Rewind
// ============================================================================

/**
 * @brief Automatic rewind on scope exit
 *
 * Usage:
 *   {
 *       MemoryScope scope(arena);
 *       auto* temp = arena.allocate<MyObject>();
 *       // ... use temp ...
 *   } // Automatically rewinds here
 */
class MemoryScope {
public:
  explicit MemoryScope(MemoryArena &arena) : arena_(arena), mark_(arena.mark()) {}

  ~MemoryScope() { arena_.rewind(mark_); }

  // Non-copyable, non-movable
  MemoryScope(const MemoryScope &) = delete;
  MemoryScope &operator=(const MemoryScope &) = delete;
  MemoryScope(MemoryScope &&) = delete;
  MemoryScope &operator=(MemoryScope &&) = delete;

private:
  MemoryArena &arena_;
  size_t mark_;
};

// ============================================================================
// Custom Allocator for STL Containers (C++17 std::pmr style)
// ============================================================================

/**
 * @brief STL-compatible allocator using MemoryArena
 *
 * Usage:
 *   MemoryArena arena;
 *   std::vector<int, ArenaAllocator<int>> vec(ArenaAllocator<int>(arena));
 */
template <typename T> class ArenaAllocator {
public:
  using value_type = T;

  explicit ArenaAllocator(MemoryArena &arena) : arena_(&arena) {}

  template <typename U> ArenaAllocator(const ArenaAllocator<U> &other) : arena_(other.arena_) {}

  T *allocate(size_t n) { return arena_->allocate_array<T>(n); }

  void deallocate(T *, size_t) {
    // No-op: memory is freed when arena is reset
  }

  template <typename U> bool operator==(const ArenaAllocator<U> &other) const {
    return arena_ == other.arena_;
  }

  template <typename U> bool operator!=(const ArenaAllocator<U> &other) const {
    return !(*this == other);
  }

  template <typename U> friend class ArenaAllocator;

private:
  MemoryArena *arena_;
};

// ============================================================================
// Object Pool for Fixed-Size Objects
// ============================================================================

/**
 * @brief Type-safe object pool with free list
 *
 * Features:
 * - O(1) allocation and deallocation
 * - Automatic constructor/destructor calls
 * - Memory reuse without fragmentation
 */
template <typename T, size_t ChunkSize = 64> class ObjectPool {
public:
  explicit ObjectPool(MemoryArena &arena) : arena_(arena) { allocate_chunk(); }

  /**
   * @brief Allocate object from pool
   */
  template <typename... Args> T *allocate(Args &&...args) {
    if (!free_list_) {
      allocate_chunk();
    }

    Node *node = free_list_;
    free_list_ = node->next;

    return new (&node->storage) T(std::forward<Args>(args)...);
  }

  /**
   * @brief Return object to pool
   */
  void deallocate(T *ptr) {
    if (!ptr)
      return;

    // Call destructor
    ptr->~T();

    // Add to free list
    Node *node = reinterpret_cast<Node *>(ptr);
    node->next = free_list_;
    free_list_ = node;
  }

private:
  struct Node {
    union {
      alignas(T) unsigned char storage[sizeof(T)];
      Node *next;
    };
  };

  void allocate_chunk() {
    Node *chunk = arena_.allocate_array<Node>(ChunkSize);

    // Link nodes into free list
    for (size_t i = 0; i < ChunkSize - 1; ++i) {
      chunk[i].next = &chunk[i + 1];
    }
    chunk[ChunkSize - 1].next = free_list_;
    free_list_ = chunk;
  }

  MemoryArena &arena_;
  Node *free_list_ = nullptr;
};

// ============================================================================
// Smart Pointer for Pool-Allocated Objects
// ============================================================================

/**
 * @brief Custom deleter for ObjectPool
 */
template <typename T, size_t ChunkSize = 64> class PoolDeleter {
public:
  explicit PoolDeleter(ObjectPool<T, ChunkSize> *pool = nullptr) : pool_(pool) {}

  void operator()(T *ptr) const {
    if (pool_) {
      pool_->deallocate(ptr);
    }
  }

private:
  ObjectPool<T, ChunkSize> *pool_;
};

/**
 * @brief Unique pointer for pool-allocated objects
 *
 * Usage:
 *   auto ptr = make_pooled<MyType>(pool, args...);
 */
template <typename T, size_t ChunkSize = 64>
using PoolPtr = std::unique_ptr<T, PoolDeleter<T, ChunkSize>>;

template <typename T, size_t ChunkSize = 64, typename... Args>
PoolPtr<T, ChunkSize> make_pooled(ObjectPool<T, ChunkSize> &pool, Args &&...args) {
  T *ptr = pool.allocate(std::forward<Args>(args)...);
  return PoolPtr<T, ChunkSize>(ptr, PoolDeleter<T, ChunkSize>(&pool));
}

#endif // MEMORY_H
