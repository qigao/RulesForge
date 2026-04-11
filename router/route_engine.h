/**
 * @file route_engine.h
 * @brief Source-to-sink routing engine with RulesForge integration
 */

#ifndef ROUTE_ENGINE_H
#define ROUTE_ENGINE_H

#include "data_bind.h"
#include "rule_forge.h"
#include "rule_forge_plugin.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Route Engine
// ============================================================

typedef struct route_engine route_engine_t;

/**
 * @brief Plugin entry for startup loading
 */
typedef struct {
    const char* name;        ///< Logical plugin name used for routing/lookup
    const char* dll_path;    ///< Path to plugin DLL
    const char* type;        ///< "source" or "sink"
    const char* config_json; ///< Plugin configuration JSON (optional)
} route_engine_plugin_entry_t;

/**
 * @brief Route engine configuration
 */
typedef struct {
    const char* rules_file;       ///< Single rules file path (mutually exclusive with rules_files)
    const char** rules_files;     ///< Multiple rules file paths (NULL-terminated array)
    int rules_file_count;         ///< Number of rules files (0 = use rules_file)
    const char* schema_file;      ///< Schema file path

    // Performance parameters
    int session_pool_size;        ///< Session pool size
    int max_rule_fires;           ///< Max rule fires per fetched payload

    // Default behavior
    int drop_no_route;            ///< Drop payloads with no route (0=keep, 1=drop)
    const char* default_target;   ///< Default sink for no-route payloads (optional)

    // Plugins to load at startup
    const route_engine_plugin_entry_t* plugins; ///< Plugin list
    int plugin_count;                           ///< Number of plugins

} route_engine_config_t;

/**
 * @brief Route engine statistics
 */
typedef struct {
    uint64_t total_messages;     ///< Total fetched payloads processed
    uint64_t total_routed;       ///< Successful sink pushes
    uint64_t no_route;           ///< Payloads with no route
    uint64_t route_errors;       ///< Route failures
    uint64_t parse_errors;       ///< Parse failures

    // Performance statistics
    uint64_t avg_route_time_us;  ///< Average route time (microseconds)
    uint64_t avg_decisions_per_msg; ///< Average decisions per fetched payload

    // Object pool statistics
    size_t session_pool_in_use;      ///< Session pool in use
    size_t session_pool_available;   ///< Session pool available
    size_t buffer_pool_in_use;       ///< Buffer pool in use
    size_t buffer_pool_available;    ///< Buffer pool available
} route_engine_stats_t;

/**
 * @brief Create empty route engine (for testing only - no KB/pools)
 * @return Engine handle, or NULL on failure
 */
route_engine_t* route_engine_create_empty(void);

/**
 * @brief Get number of registered sources
 */
int route_engine_source_count(const route_engine_t* engine);

/**
 * @brief Get number of registered sinks
 */
int route_engine_sink_count(const route_engine_t* engine);

/**
 * @brief Create route engine
 * @param config Engine configuration
 * @return Engine handle, or NULL on failure
 */
route_engine_t* route_engine_create(const route_engine_config_t* config);

/**
 * @brief Register data source plugin
 * @param engine Engine handle
 * @param source_name Source name (e.g., "mqtt_source")
 * @param vtable DataSource vtable
 * @param config_json Plugin configuration JSON
 * @return RULES_FORGE_OK on success
 */
ruleforge_status_t route_engine_register_source(
    route_engine_t* engine,
    const char* source_name,
    const ruleforge_datasource_vtable_t* vtable,
    const char* config_json);

/**
 * @brief Register data sink plugin using the route-envelope API
 * @param engine Engine handle
 * @param sink_name Sink name (e.g., "http_sink")
 * @param vtable Route-envelope sink vtable
 * @param config_json Plugin configuration JSON
 * @return RULES_FORGE_OK on success
 */
ruleforge_status_t route_engine_register_sink(
    route_engine_t* engine,
    const char* sink_name,
    const ruleforge_datasink_vtable_t* vtable,
    const char* config_json);

/**
 * @brief Load plugin from DLL
 * @param engine Engine handle
 * @param dll_path Plugin DLL path
 * @param plugin_type "source" or "sink"
 * @return RULES_FORGE_OK on success
 */
ruleforge_status_t route_engine_load_plugin(
    route_engine_t* engine,
    const char* dll_path,
    const char* plugin_type);

/**
 * @brief Load plugin from DLL with explicit logical name and config
 * @param engine Engine handle
 * @param plugin_name Logical source/sink name (NULL = derive from DLL filename)
 * @param dll_path Plugin DLL path
 * @param plugin_type "source" or "sink"
 * @param config_json Plugin configuration JSON
 * @return RULES_FORGE_OK on success
 */
ruleforge_status_t route_engine_load_plugin_ex(
    route_engine_t* engine,
    const char* plugin_name,
    const char* dll_path,
    const char* plugin_type,
    const char* config_json);

/**
 * @brief Main routing loop for source plugins (blocking)
 * @param engine Engine handle
 * @param source_name Data source name
 * @param query Query parameter (e.g., MQTT topic, SQL query)
 * @param fact_type Declared fact type for fetched payloads
 * @param max_messages Max fetched payloads to process (-1=unlimited)
 * @return RULES_FORGE_OK on success
 */
ruleforge_status_t route_engine_run(
    route_engine_t* engine,
    const char* source_name,
    const char* query,
    const char* fact_type,
    int max_messages);

/**
 * @brief Stop routing loop (from another thread)
 * @param engine Engine handle
 * @return RULES_FORGE_OK on success
 */
ruleforge_status_t route_engine_stop(route_engine_t* engine);

/**
 * @brief Get engine statistics
 * @param engine Engine handle
 * @param out_stats Output statistics
 */
void route_engine_get_stats(
    route_engine_t* engine,
    route_engine_stats_t* out_stats);

/**
 * @brief Get the plugin context pointer for a registered source.
 *
 * Allows in-process callers (e.g. mod_rulesforge) to retrieve the ctx
 * allocated by the source's init() so they can call plugin-specific APIs
 * such as freeswitch_source_push().
 *
 * @param engine      Engine handle
 * @param source_name Registered source name
 * @return ctx pointer on success, NULL if not found
 */
void* route_engine_get_source_ctx(
    route_engine_t* engine,
    const char* source_name);

/**
 * @brief Destroy route engine
 * @param engine Engine handle
 */
void route_engine_destroy(route_engine_t* engine);

#ifdef __cplusplus
}
#endif

#endif // ROUTE_ENGINE_H

