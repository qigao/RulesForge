/**
 * @file session_pool.c
 * @brief RulesForge session pool — fixed-size, thread-safe, O(1) acquire/release
 *
 * Acquire: pop from free-list stack           — O(1)
 * Release: hash-map lookup (session → slot)   — O(1) amortized
 */

#include "session_pool.h"
#include "turbo_thread.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ──────────────────────────────────────────────────────────────
 * slot_map — open-addressing hash map: session_ptr → slot index
 *
 * Populated once at pool creation (fixed set, stable pointers).
 * Load factor kept ≤ 0.5, so worst-case probe count is bounded.
 * ────────────────────────────────────────────────────────────── */

typedef struct {
    ruleforge_stateful_session_t key; /* NULL = empty bucket */
    int                          slot;
} slot_map_entry_t;

static size_t slot_map_size_for(size_t n) {
    /* next power-of-2 >= n*2, so load ≤ 0.5 */
    size_t s = 2;
    while (s < n * 2) s <<= 1;
    return s;
}

static int slot_map_lookup(const slot_map_entry_t* map, size_t mask,
                           ruleforge_stateful_session_t session) {
    size_t h = ((uintptr_t)session >> 4) & mask;
    while (map[h].key) {
        if (map[h].key == session) return map[h].slot;
        h = (h + 1) & mask; /* linear probe */
    }
    return -1; /* not found */
}

static void slot_map_insert(slot_map_entry_t* map, size_t mask,
                            ruleforge_stateful_session_t session, int slot) {
    size_t h = ((uintptr_t)session >> 4) & mask;
    while (map[h].key) h = (h + 1) & mask;
    map[h].key  = session;
    map[h].slot = slot;
}

/* ──────────────────────────────────────────────────────────────
 * session_pool definition
 * ────────────────────────────────────────────────────────────── */

struct session_pool {
    ruleforge_stateful_session_t* sessions;  /* flat array — session handle per slot */
    int*                          index_of;  /* slot → free_list position; -1 = in-use */
    size_t                        pool_size;
    int*                          free_list; /* stack of free slot indices */
    size_t                        free_count;
    ruleforge_knowledge_base_t    kb;

    /* O(1) release: map session handle → slot index */
    slot_map_entry_t*             slot_map;
    size_t                        slot_map_mask; /* = slot_map_size - 1 */

    turbo_mutex_t  lock;
    turbo_cond_t   cond;
    uint64_t       total_acquires;
    uint64_t       total_releases;
    uint64_t       acquire_failures;
};

/* ──────────────────────────────────────────────────────────────
 * Lifecycle
 * ────────────────────────────────────────────────────────────── */

session_pool_t* session_pool_create(ruleforge_knowledge_base_t kb, size_t pool_size) {
    session_pool_t* sp = calloc(1, sizeof(session_pool_t));
    if (!sp) return NULL;

    sp->sessions  = (ruleforge_stateful_session_t*)calloc(pool_size, sizeof(ruleforge_stateful_session_t));
    sp->free_list = (int*)malloc(pool_size * sizeof(int));
    sp->index_of  = (int*)malloc(pool_size * sizeof(int));

    size_t map_size   = slot_map_size_for(pool_size);
    sp->slot_map      = (slot_map_entry_t*)calloc(map_size, sizeof(slot_map_entry_t));
    sp->slot_map_mask = map_size - 1;

    if (!sp->sessions || !sp->free_list || !sp->index_of || !sp->slot_map) {
        free(sp->sessions); free(sp->free_list);
        free(sp->index_of); free(sp->slot_map);
        free(sp);
        return NULL;
    }

    sp->kb        = kb;
    sp->pool_size = pool_size;
    sp->free_count = 0;

    for (size_t i = 0; i < pool_size; i++) {
        if (ruleforge_session_create(kb, &sp->sessions[i]) == RULES_FORGE_OK) {
            sp->index_of[i]               = (int)sp->free_count;
            sp->free_list[sp->free_count++] = (int)i;
            slot_map_insert(sp->slot_map, sp->slot_map_mask, sp->sessions[i], (int)i);
        }
    }

    turbo_mutex_init(&sp->lock);
    turbo_cond_init(&sp->cond);
    return sp;
}

/* ──────────────────────────────────────────────────────────────
 * Acquire — O(1): pop from free-list stack
 * ────────────────────────────────────────────────────────────── */

ruleforge_stateful_session_t session_pool_acquire(session_pool_t* pool, int timeout_ms) {
    if (!pool) return NULL;

    turbo_mutex_lock(&pool->lock);

    while (pool->free_count == 0) {
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
    }

    int slot = pool->free_list[--pool->free_count];
    pool->index_of[slot] = -1; /* mark in-use */
    ruleforge_stateful_session_t session = pool->sessions[slot];

    pool->total_acquires++;
    ruleforge_session_reset(session);
    turbo_mutex_unlock(&pool->lock);
    return session;
}

/* ──────────────────────────────────────────────────────────────
 * Release — O(1): hash-map lookup gives slot, then stack push
 * ────────────────────────────────────────────────────────────── */

void session_pool_release(session_pool_t* pool, ruleforge_stateful_session_t session) {
    if (!pool || !session) return;

    turbo_mutex_lock(&pool->lock);

    int slot = slot_map_lookup(pool->slot_map, pool->slot_map_mask, session);

    /* guard: slot must exist and must currently be in-use */
    if (slot >= 0 && pool->index_of[slot] == -1) {
        pool->index_of[slot]                  = (int)pool->free_count;
        pool->free_list[pool->free_count++] = slot;
        pool->total_releases++;
        turbo_cond_signal(&pool->cond);
    }

    turbo_mutex_unlock(&pool->lock);
}

/* ──────────────────────────────────────────────────────────────
 * Stats / Destroy
 * ────────────────────────────────────────────────────────────── */

void session_pool_get_stats(session_pool_t* pool, pool_stats_t* out_stats) {
    if (!pool || !out_stats) return;

    turbo_mutex_lock(&pool->lock);
    out_stats->total_size       = pool->pool_size;
    out_stats->available        = pool->free_count;
    out_stats->in_use           = pool->pool_size - pool->free_count;
    out_stats->total_acquires   = pool->total_acquires;
    out_stats->total_releases   = pool->total_releases;
    out_stats->acquire_failures = pool->acquire_failures;
    turbo_mutex_unlock(&pool->lock);
}

void session_pool_destroy(session_pool_t* pool) {
    if (!pool) return;

    for (size_t i = 0; i < pool->pool_size; i++) {
        if (pool->sessions[i]) ruleforge_session_destroy(pool->sessions[i]);
    }

    turbo_mutex_destroy(&pool->lock);
    turbo_cond_destroy(&pool->cond);
    free(pool->sessions);
    free(pool->free_list);
    free(pool->index_of);
    free(pool->slot_map);
    free(pool);
}
