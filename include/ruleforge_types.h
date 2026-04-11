/**
 * @file ruleforge_types.h
 * @brief Shared status codes and opaque handle types.
 *
 * Both rule_forge.h (main C API) and rule_forge_plugin.h (plugin vtables) need
 * ruleforge_status_t and ruleforge_fact_t.
 */

#ifndef RULEFORGE_TYPES_H
#define RULEFORGE_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Standard status codes returned by all RulesForge C API functions.
 */
typedef enum {
    RULES_FORGE_OK                        = 0,
    RULES_FORGE_ERROR_GENERIC             = 1,
    RULES_FORGE_ERROR_INVALID_ARGUMENT    = 2,
    RULES_FORGE_ERROR_COMPILATION_FAILED  = 3,
    RULES_FORGE_ERROR_SESSION_CREATION_FAILED = 4,
    RULES_FORGE_ERROR_FACT_INSERTION_FAILED   = 5,
    RULES_FORGE_ERROR_QUERY_FAILED        = 6,
    RULES_FORGE_ERROR_MEMORY_ALLOCATION   = 7,
    RULES_FORGE_ERROR_SESSION_INCONSISTENT = 8,
    RULES_FORGE_STATUS_END_OF_STREAM      = 9,
} ruleforge_status_t;

/**
 * @brief Opaque handle for a fact.
 *
 * The handle is owned by the session or query result that produced it.
 * Do not free it directly.
 */
typedef struct ruleforge_fact_handle_s *ruleforge_fact_t;

#ifdef __cplusplus
}
#endif

#endif /* RULEFORGE_TYPES_H */