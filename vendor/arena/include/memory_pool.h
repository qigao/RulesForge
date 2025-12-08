#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <stddef.h>
#include <stdint.h>

// Memory pool for zero-allocation parsing
typedef struct {
  uint8_t *pool;
  size_t size;
  size_t used;
  size_t peak_used;     // High water mark
  uint64_t alloc_count; // Statistics
} MemoryPool;

// Create memory pool
MemoryPool *pool_create(size_t size);

// Allocate from pool
void *pool_alloc(MemoryPool *pool, size_t size);

// Reset pool (reuse memory)
void pool_reset(MemoryPool *pool);

// Get statistics
size_t pool_get_used(MemoryPool *pool);
size_t pool_get_available(MemoryPool *pool);
size_t pool_get_peak(MemoryPool *pool);

/* Mark / rewind support for stack-style allocations */
size_t pool_mark(MemoryPool *pool);
void pool_rewind(MemoryPool *pool, size_t mark);

// Destroy pool
void pool_destroy(MemoryPool *pool);

#endif // MEMORY_POOL_H
