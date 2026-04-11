/**
 * @file session_pool.h
 * @brief RulesForge session pool for high-performance session reuse
 */

#ifndef SESSION_POOL_H
#define SESSION_POOL_H

#include "rule_forge.h"
#include "buffer_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct session_pool session_pool_t;

/**
 * @brief Create session pool
 * @param kb Knowledge base handle
 * @param pool_size Number of sessions in pool
 * @return Pool handle, or NULL on failure
 */
session_pool_t* session_pool_create(
    ruleforge_knowledge_base_t kb,
    size_t pool_size);

/**
 * @brief Acquire session from pool
 * @param pool Pool handle
 * @param timeout_ms Timeout in milliseconds (-1=block, 0=non-blocking)
 * @return Session handle, or NULL if pool exhausted/timeout
 */
ruleforge_stateful_session_t session_pool_acquire(
    session_pool_t* pool,
    int timeout_ms);

/**
 * @brief Release session back to pool
 * @param pool Pool handle
 * @param session Session handle (must be from this pool)
 */
void session_pool_release(
    session_pool_t* pool,
    ruleforge_stateful_session_t session);

/**
 * @brief Get pool statistics
 * @param pool Pool handle
 * @param out_stats Output statistics
 */
void session_pool_get_stats(
    session_pool_t* pool,
    pool_stats_t* out_stats);

/**
 * @brief Destroy session pool
 * @param pool Pool handle
 */
void session_pool_destroy(session_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif // SESSION_POOL_H
