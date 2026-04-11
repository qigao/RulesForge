/**
 * @file buffer_pool.h
 * @brief Memory buffer pool for zero-copy payload handling
 */

#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct buffer_pool buffer_pool_t;

typedef struct {
    uint8_t* data;
    size_t capacity;
    size_t length;
} buffer_t;

typedef struct {
    size_t total_size;
    size_t in_use;
    size_t available;
    uint64_t total_acquires;
    uint64_t total_releases;
    uint64_t acquire_failures;
} pool_stats_t;

/**
 * @brief Create buffer pool
 * @param buffer_size Size of each buffer in bytes
 * @param pool_size Number of buffers in pool
 * @return Pool handle, or NULL on failure
 */
buffer_pool_t* buffer_pool_create(
    size_t buffer_size,
    size_t pool_size);

/**
 * @brief Acquire buffer from pool
 * @param pool Pool handle
 * @param timeout_ms Timeout in milliseconds (-1=block, 0=non-blocking)
 * @return Buffer pointer, or NULL if pool exhausted/timeout
 */
buffer_t* buffer_pool_acquire(
    buffer_pool_t* pool,
    int timeout_ms);

/**
 * @brief Release buffer back to pool
 * @param pool Pool handle
 * @param buffer Buffer pointer (must be from this pool)
 */
void buffer_pool_release(
    buffer_pool_t* pool,
    buffer_t* buffer);

/**
 * @brief Get pool statistics
 * @param pool Pool handle
 * @param out_stats Output statistics
 */
void buffer_pool_get_stats(
    buffer_pool_t* pool,
    pool_stats_t* out_stats);

/**
 * @brief Destroy buffer pool
 * @param pool Pool handle
 */
void buffer_pool_destroy(buffer_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif // BUFFER_POOL_H
