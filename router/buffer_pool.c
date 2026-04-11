/**
 * @file buffer_pool.c
 * @brief Memory buffer pool — wraps TurboNet object_pool_t with thread-safe acquire/release
 */

#include "buffer_pool.h"
#include "object_pool.h"
#include "turbo_thread.h"
#include <stdlib.h>

struct buffer_pool {
    object_pool_t* pool;
    turbo_mutex_t  lock;
    turbo_cond_t   cond;
    uint64_t       total_acquires;
    uint64_t       total_releases;
    uint64_t       acquire_failures;
    size_t         buffer_size;
};

buffer_pool_t* buffer_pool_create(size_t buffer_size, size_t pool_size) {
    buffer_pool_t* bp = calloc(1, sizeof(buffer_pool_t));
    if (!bp) return NULL;

    /* Each slot holds buffer_t header + trailing data bytes in one allocation */
    object_pool_config_t cfg = {
        .object_size      = sizeof(buffer_t) + buffer_size,
        .initial_capacity = pool_size,
        .max_capacity     = pool_size,
        .zero_on_alloc    = false,
    };
    bp->pool = object_pool_create(&cfg);
    if (!bp->pool) { free(bp); return NULL; }

    bp->buffer_size = buffer_size;

    turbo_mutex_init(&bp->lock);
    turbo_cond_init(&bp->cond);
    return bp;
}

buffer_t* buffer_pool_acquire(buffer_pool_t* pool, int timeout_ms) {
    if (!pool) return NULL;

    turbo_mutex_lock(&pool->lock);

    buffer_t* b = (buffer_t*)object_pool_alloc(pool->pool);

    while (!b) {
        if (timeout_ms == 0) {
            pool->acquire_failures++;
            turbo_mutex_unlock(&pool->lock);
            return NULL;
        } else if (timeout_ms > 0) {
            int ret = turbo_cond_timedwait(&pool->cond, &pool->lock,
                                           (uint64_t)timeout_ms * 1000000);
            if (ret != 0) {
                pool->acquire_failures++;
                turbo_mutex_unlock(&pool->lock);
                return NULL;
            }
        } else {
            turbo_cond_wait(&pool->cond, &pool->lock);
        }
        b = (buffer_t*)object_pool_alloc(pool->pool);
    }

    b->data     = (uint8_t*)(b + 1);
    b->capacity = pool->buffer_size;
    b->length   = 0;
    pool->total_acquires++;
    turbo_mutex_unlock(&pool->lock);
    return b;
}

void buffer_pool_release(buffer_pool_t* pool, buffer_t* buffer) {
    if (!pool || !buffer) return;

    turbo_mutex_lock(&pool->lock);
    object_pool_free(pool->pool, buffer);
    pool->total_releases++;
    turbo_cond_signal(&pool->cond);
    turbo_mutex_unlock(&pool->lock);
}

void buffer_pool_get_stats(buffer_pool_t* pool, pool_stats_t* out_stats) {
    if (!pool || !out_stats) return;

    turbo_mutex_lock(&pool->lock);
    out_stats->total_size       = object_pool_capacity(pool->pool);
    out_stats->in_use           = object_pool_allocated_count(pool->pool);
    out_stats->available        = object_pool_free_count(pool->pool);
    out_stats->total_acquires   = pool->total_acquires;
    out_stats->total_releases   = pool->total_releases;
    out_stats->acquire_failures = pool->acquire_failures;
    turbo_mutex_unlock(&pool->lock);
}

void buffer_pool_destroy(buffer_pool_t* pool) {
    if (!pool) return;

    object_pool_destroy(pool->pool);
    turbo_mutex_destroy(&pool->lock);
    turbo_cond_destroy(&pool->cond);
    free(pool);
}
