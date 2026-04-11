/**
 * @file rule_forge_plugin.h
 * @brief Standard RulesForge plugin vtable ABI for external extension.
 *
 * This is the public extension contract for source/sink style plugins.
 * Older private include paths under plugins/ are kept only as compatibility
 * wrappers and should not be treated as the canonical ABI.
 */

#ifndef RULE_FORGE_PLUGIN_H
#define RULE_FORGE_PLUGIN_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "ruleforge_types.h"

#ifndef DATA_BIND_H
typedef enum {
    DATA_BIND_FORMAT_BINARY,
    DATA_BIND_FORMAT_JSON,
    DATA_BIND_FORMAT_CSV
} DataBindFormat;
#endif

// ============================================================
// DataSource Plugin (vtable)
// ============================================================

typedef struct {
    ruleforge_status_t (*init)(const char* config_json, void** out_ctx);

    ruleforge_status_t (*fetch)(
        void* ctx,
        const char* query,
        const char* fact_type,
        DataBindFormat* out_format,
        const uint8_t** out_data,
        size_t* out_len);

    void (*free_data)(void* ctx, const uint8_t* data);
    void (*cleanup)(void* ctx);
} ruleforge_datasource_vtable_t;

// ============================================================
// DataSink Plugin (vtable)
// ============================================================

typedef struct {
    const char* source_name;
    const char* source_query;
    const char* fact_type;
    const char* target_name;
    const char* payload;
    const char* metadata;
    const char* effective_metadata;
    ruleforge_fact_t original_fact;
    ruleforge_fact_t decision_fact;
    uint64_t message_index;
    int decision_index;
    int decision_count;
    const ruleforge_fact_t* original_facts;
    int original_fact_count;
} ruleforge_route_envelope_t;

typedef ruleforge_status_t (*ruleforge_datasink_push_route_fn)(
    void* ctx,
    const ruleforge_route_envelope_t* route);

typedef struct {
    ruleforge_status_t (*init)(const char* config_json, void** out_ctx);
    ruleforge_datasink_push_route_fn push_route;
    void (*cleanup)(void* ctx);
} ruleforge_datasink_vtable_t;

// ============================================================
// Plugin DLL Export
// ============================================================

typedef const ruleforge_datasource_vtable_t* (*ruleforge_get_source_vtable_fn)(void);
typedef const ruleforge_datasink_vtable_t*   (*ruleforge_get_sink_vtable_fn)(void);

#ifdef __cplusplus
}
#endif

#endif /* RULE_FORGE_PLUGIN_H */