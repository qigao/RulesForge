/**
 * @file route_engine.c
 * @brief Message routing engine implementation with object pools
 */

#include "route_engine.h"
#include "session_pool.h"
#include "buffer_pool.h"
#include "plugin_loader.h"
#include "turbo_thread.h"
#include <stdatomic.h>  /* _Atomic, C11 — guarantees cross-thread visibility for engine->running */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef _WIN32
#include <time.h>
#endif

#define MAX_SOURCES 16
#define MAX_SINKS 64

// Data source entry
typedef struct {
    char name[64];
    ruleforge_datasource_vtable_t vtable;
    void* ctx;
    plugin_handle_t dll_handle;
} datasource_entry_t;

typedef struct {
    ruleforge_fact_t primary_fact;
    ruleforge_fact_t inline_fact;
    ruleforge_fact_t* facts;
    int fact_count;
    int owns_array;
} original_fact_batch_t;

// Data sink entry
typedef struct {
    char name[64];
    ruleforge_datasink_vtable_t vtable;
    void* ctx;
    plugin_handle_t dll_handle;
} datasink_entry_t;

// Route engine structure
struct route_engine {
    ruleforge_knowledge_base_t kb;
    int runtime_acquired;

    // Plugin registry
    datasource_entry_t sources[MAX_SOURCES];
    int source_count;

    datasink_entry_t sinks[MAX_SINKS];
    int sink_count;

    // Object pools
    session_pool_t* session_pool;
    buffer_pool_t* buffer_pool;
    turbo_mutex_t stats_lock;

    // Configuration
    route_engine_config_t config;

    // Runtime state — written by route_engine_stop() on a different thread,
    // read by route_engine_run() loop.  _Atomic guarantees visibility without
    // needing an explicit mutex on this flag.
    _Atomic int running;

    // Statistics
    route_engine_stats_t stats;
    uint64_t total_decisions;
    uint64_t successful_payloads;
};

static _Atomic int g_ruleforge_runtime_users = 0;

static ruleforge_status_t process_source_payload(
    route_engine_t* engine,
    const char* source_name,
    const char* source_query,
    const char* fact_type,
    DataBindFormat format,
    const uint8_t* data,
    size_t len);

static void record_route_time(route_engine_t* engine, uint64_t elapsed_us) {
    if (!engine) {
        return;
    }

    turbo_mutex_lock(&engine->stats_lock);
    engine->stats.avg_route_time_us =
        (engine->stats.avg_route_time_us * engine->successful_payloads + elapsed_us) /
        (engine->successful_payloads + 1);
    engine->successful_payloads++;
    turbo_mutex_unlock(&engine->stats_lock);
}

static uint64_t stats_note_payload_begin(route_engine_t* engine) {
    uint64_t message_index;

    turbo_mutex_lock(&engine->stats_lock);
    engine->stats.total_messages++;
    message_index = engine->stats.total_messages;
    turbo_mutex_unlock(&engine->stats_lock);
    return message_index;
}

static void stats_note_parse_error(route_engine_t* engine) {
    turbo_mutex_lock(&engine->stats_lock);
    engine->stats.parse_errors++;
    turbo_mutex_unlock(&engine->stats_lock);
}

static void stats_note_route_error(route_engine_t* engine) {
    turbo_mutex_lock(&engine->stats_lock);
    engine->stats.route_errors++;
    turbo_mutex_unlock(&engine->stats_lock);
}

static void stats_note_total_routed(route_engine_t* engine) {
    turbo_mutex_lock(&engine->stats_lock);
    engine->stats.total_routed++;
    turbo_mutex_unlock(&engine->stats_lock);
}

static void stats_note_no_route(route_engine_t* engine) {
    turbo_mutex_lock(&engine->stats_lock);
    engine->stats.no_route++;
    turbo_mutex_unlock(&engine->stats_lock);
}

static void stats_note_decisions(route_engine_t* engine, int decision_count) {
    turbo_mutex_lock(&engine->stats_lock);
    engine->total_decisions += (uint64_t)decision_count;
    engine->stats.avg_decisions_per_msg =
        (engine->stats.total_messages == 0)
            ? 0
            : (engine->total_decisions / engine->stats.total_messages);
    turbo_mutex_unlock(&engine->stats_lock);
}

static ruleforge_status_t route_engine_runtime_acquire(void) {
    if (atomic_fetch_add(&g_ruleforge_runtime_users, 1) == 0) {
        ruleforge_status_t status = ruleforge_init();
        if (status != RULES_FORGE_OK) {
            atomic_fetch_sub(&g_ruleforge_runtime_users, 1);
            return status;
        }
    }
    return RULES_FORGE_OK;
}

static void route_engine_runtime_release(void) {
    int previous = atomic_fetch_sub(&g_ruleforge_runtime_users, 1);
    if (previous == 1) {
        ruleforge_cleanup();
    }
}

static bool is_valid_plugin_type(const char* plugin_type) {
    return plugin_type &&
           (strcmp(plugin_type, "source") == 0 ||
            strcmp(plugin_type, "sink") == 0);
}

static bool copy_name_or_fail(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0 || !src || !src[0]) {
        return false;
    }

    size_t len = strlen(src);
    if (len >= dst_size) {
        return false;
    }

    memcpy(dst, src, len + 1);
    return true;
}

static const char* path_basename(const char* path) {
    if (!path) return NULL;

    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    const char* base = path;

    if (slash && slash + 1 > base) base = slash + 1;
    if (backslash && backslash + 1 > base) base = backslash + 1;
    return base;
}

static bool derive_plugin_name(const char* explicit_name,
                               const char* dll_path,
                               char* out_name,
                               size_t out_name_size) {
    if (explicit_name && explicit_name[0]) {
        return copy_name_or_fail(out_name, out_name_size, explicit_name);
    }

    const char* base = path_basename(dll_path);
    if (!base || !base[0]) {
        return false;
    }

    size_t len = strlen(base);
    if (len >= 3 && base[0] == 'l' && base[1] == 'i' && base[2] == 'b') {
        base += 3;
        len -= 3;
    }

    const char* dot = strrchr(base, '.');
    if (dot && dot > base) {
        len = (size_t)(dot - base);
    }

    if (len == 0 || len >= out_name_size) {
        return false;
    }

    memcpy(out_name, base, len);
    out_name[len] = '\0';
    return true;
}

static datasource_entry_t* find_source(route_engine_t* engine, const char* name) {
    for (int i = 0; i < engine->source_count; i++) {
        if (strcmp(engine->sources[i].name, name) == 0) {
            return &engine->sources[i];
        }
    }
    return NULL;
}

static datasink_entry_t* find_sink(route_engine_t* engine, const char* name) {
    for (int i = 0; i < engine->sink_count; i++) {
        if (strcmp(engine->sinks[i].name, name) == 0) {
            return &engine->sinks[i];
        }
    }
    return NULL;
}

static ruleforge_status_t initialize_sink(datasink_entry_t* entry,
                                             const ruleforge_datasink_vtable_t* vtable,
                                             const char* config_json) {
    ruleforge_status_t status = vtable->init(config_json, &entry->ctx);
    if (status != RULES_FORGE_OK) {
        return status;
    }

    entry->vtable = *vtable;
    return RULES_FORGE_OK;
}

route_engine_t* route_engine_create_empty(void) {
    route_engine_t* engine = (route_engine_t*)calloc(1, sizeof(route_engine_t));
    if (!engine) {
        return NULL;
    }

    turbo_mutex_init(&engine->stats_lock);
    return engine;
}

// Create route engine
route_engine_t* route_engine_create(const route_engine_config_t* config) {
    if (!config || config->session_pool_size <= 0 || config->max_rule_fires <= 0) return NULL;
    if (config->plugin_count > 0 && !config->plugins) return NULL;

    route_engine_t* engine = route_engine_create_empty();
    if (!engine) return NULL;

    // Save configuration
    engine->config = *config;

    // Initialize RulesForge runtime
    if (route_engine_runtime_acquire() != RULES_FORGE_OK) {
        free(engine);
        return NULL;
    }
    engine->runtime_acquired = 1;

    // Create knowledge base
    if (ruleforge_kb_create(&engine->kb) != RULES_FORGE_OK) {
        route_engine_runtime_release();
        free(engine);
        return NULL;
    }

    // Load rules — multi-file or single-file
    ruleforge_status_t load_status;
    if (config->rules_file_count > 0 && config->rules_files) {
        load_status = ruleforge_kb_load_drl_files(
            engine->kb,
            config->rules_files,
            config->rules_file_count,
            NULL, 0);
    } else if (config->rules_file) {
        load_status = ruleforge_kb_load_drl_file(
            engine->kb,
            config->rules_file,
            NULL, 0);
    } else {
        ruleforge_kb_destroy(engine->kb);
        route_engine_runtime_release();
        free(engine);
        return NULL;
    }

    if (load_status != RULES_FORGE_OK) {
        ruleforge_kb_destroy(engine->kb);
        route_engine_runtime_release();
        free(engine);
        return NULL;
    }

    // Create session pool
    engine->session_pool = session_pool_create(engine->kb, config->session_pool_size);
    if (!engine->session_pool) {
        ruleforge_kb_destroy(engine->kb);
        route_engine_runtime_release();
        free(engine);
        return NULL;
    }

    // Create buffer pool (2x session pool size)
    engine->buffer_pool = buffer_pool_create(
        1024 * 1024,  // 1MB per buffer
        config->session_pool_size * 2
    );
    if (!engine->buffer_pool) {
        session_pool_destroy(engine->session_pool);
        ruleforge_kb_destroy(engine->kb);
        route_engine_runtime_release();
        free(engine);
        return NULL;
    }

    // Load plugins at startup
    for (int i = 0; i < config->plugin_count; i++) {
        const route_engine_plugin_entry_t* p = &config->plugins[i];
        if (route_engine_load_plugin_ex(engine, p->name, p->dll_path, p->type, p->config_json) != RULES_FORGE_OK) {
            route_engine_destroy(engine);
            return NULL;
        }
    }

    engine->running = 0;

    return engine;
}

// Register data source
ruleforge_status_t route_engine_register_source(
    route_engine_t* engine,
    const char* source_name,
    const ruleforge_datasource_vtable_t* vtable,
    const char* config_json) {

    if (!engine || !source_name || !vtable) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    if (!vtable->init || !vtable->fetch || !vtable->free_data || !vtable->cleanup) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    if (engine->source_count >= MAX_SOURCES) {
        return RULES_FORGE_ERROR_GENERIC;
    }
    if (find_source(engine, source_name)) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    datasource_entry_t* entry = &engine->sources[engine->source_count];
    if (!copy_name_or_fail(entry->name, sizeof(entry->name), source_name)) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    entry->vtable = *vtable;

    // Initialize plugin
    ruleforge_status_t status = vtable->init(config_json, &entry->ctx);
    if (status != RULES_FORGE_OK) {
        return status;
    }

    engine->source_count++;
    return RULES_FORGE_OK;
}

// Register data sink
ruleforge_status_t route_engine_register_sink(
    route_engine_t* engine,
    const char* sink_name,
    const ruleforge_datasink_vtable_t* vtable,
    const char* config_json) {

    if (!engine || !sink_name || !vtable) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    if (!vtable->init || !vtable->push_route || !vtable->cleanup) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    if (engine->sink_count >= MAX_SINKS) {
        return RULES_FORGE_ERROR_GENERIC;
    }
    if (find_sink(engine, sink_name)) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    datasink_entry_t* entry = &engine->sinks[engine->sink_count];
    if (!copy_name_or_fail(entry->name, sizeof(entry->name), sink_name)) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    ruleforge_status_t status = initialize_sink(entry, vtable, config_json);
    if (status != RULES_FORGE_OK) {
        return status;
    }

    engine->sink_count++;
    return RULES_FORGE_OK;
}

// Load plugin from DLL
ruleforge_status_t route_engine_load_plugin(
    route_engine_t* engine,
    const char* dll_path,
    const char* plugin_type) {
    return route_engine_load_plugin_ex(engine, NULL, dll_path, plugin_type, NULL);
}

ruleforge_status_t route_engine_load_plugin_ex(
    route_engine_t* engine,
    const char* plugin_name,
    const char* dll_path,
    const char* plugin_type,
    const char* config_json) {

    if (!engine || !dll_path || !plugin_type || !is_valid_plugin_type(plugin_type)) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    char resolved_name[64];
    plugin_handle_t handle;
    void* fn;
    ruleforge_status_t status = RULES_FORGE_ERROR_INVALID_ARGUMENT;

    if (strcmp(plugin_type, "source") == 0) {
        if (!derive_plugin_name(plugin_name, dll_path, resolved_name, sizeof(resolved_name))) {
            return RULES_FORGE_ERROR_INVALID_ARGUMENT;
        }
        status = plugin_load(dll_path, "ruleforge_get_source_vtable", &handle, &fn);
        if (status != RULES_FORGE_OK) {
            return status;
        }

        ruleforge_get_source_vtable_fn get_vtable = (ruleforge_get_source_vtable_fn)fn;
        status = route_engine_register_source(engine, resolved_name, get_vtable(), config_json);
        if (status == RULES_FORGE_OK) {
            engine->sources[engine->source_count - 1].dll_handle = handle;
        } else {
            plugin_unload(handle);
        }
        return status;
    }

    if (!derive_plugin_name(plugin_name, dll_path, resolved_name, sizeof(resolved_name))) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    status = plugin_load(dll_path, "ruleforge_get_sink_vtable", &handle, &fn);
    if (status != RULES_FORGE_OK) {
        return status;
    }

    ruleforge_get_sink_vtable_fn get_vtable = (ruleforge_get_sink_vtable_fn)fn;
    status = route_engine_register_sink(engine, resolved_name, get_vtable(), config_json);
    if (status == RULES_FORGE_OK) {
        engine->sinks[engine->sink_count - 1].dll_handle = handle;
    } else {
        plugin_unload(handle);
    }

    return status;
}

// Inject fact into session based on format
static ruleforge_status_t add_fact_by_format(
    ruleforge_stateful_session_t session,
    const char* fact_type,
    DataBindFormat format,
    const buffer_t* buffer,
    original_fact_batch_t* out_batch)
{
    if (out_batch) {
        memset(out_batch, 0, sizeof(*out_batch));
    }

    switch (format) {
        case DATA_BIND_FORMAT_JSON: {
            ruleforge_fact_t fact = NULL;
            ruleforge_status_t status = ruleforge_session_add_fact_json_ex(
                session, fact_type, (const char*)buffer->data, &fact);
            if (status == RULES_FORGE_OK && out_batch && fact) {
                out_batch->primary_fact = fact;
                out_batch->inline_fact = fact;
                out_batch->facts = &out_batch->inline_fact;
                out_batch->fact_count = 1;
            }
            return status;
        }
        case DATA_BIND_FORMAT_CSV: {
            int loaded = 0;
            ruleforge_fact_t* facts = NULL;
            ruleforge_status_t status = ruleforge_session_add_facts_csv_ex(
                session, fact_type, (const char*)buffer->data, &facts, &loaded);
            if (status == RULES_FORGE_OK && out_batch) {
                out_batch->facts = facts;
                out_batch->fact_count = loaded;
                out_batch->owns_array = facts != NULL;
                if (loaded == 1 && facts) {
                    out_batch->primary_fact = facts[0];
                }
            } else {
                ruleforge_fact_array_free(facts);
            }
            return status;
        }
        case DATA_BIND_FORMAT_BINARY:
        {
            ruleforge_fact_t fact = NULL;
            ruleforge_status_t status = ruleforge_session_add_fact_binary_ex(
                session, fact_type, buffer->data, buffer->length, &fact);
            if (status == RULES_FORGE_OK && out_batch && fact) {
                out_batch->primary_fact = fact;
                out_batch->inline_fact = fact;
                out_batch->facts = &out_batch->inline_fact;
                out_batch->fact_count = 1;
            }
            return status;
        }
        default:
            return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
}

static void free_original_fact_batch(original_fact_batch_t* batch) {
    if (!batch) {
        return;
    }
    if (batch->owns_array) {
        ruleforge_fact_array_free(batch->facts);
    }
    memset(batch, 0, sizeof(*batch));
}

static const char* choose_metadata_payload(const char* payload, const char* metadata) {
    return (payload && payload[0]) ? payload : ((metadata && metadata[0]) ? metadata : NULL);
}

static int is_end_of_stream(ruleforge_status_t status) {
    return status == RULES_FORGE_STATUS_END_OF_STREAM;
}

static bool sink_accepts_route(const datasink_entry_t* sink,
                               const original_fact_batch_t* original_batch) {
    (void)original_batch;
    return sink != NULL;
}

static ruleforge_route_envelope_t build_route_envelope(
    const char* source_name,
    const char* source_query,
    const char* fact_type,
    const char* target_name,
    const char* payload,
    const char* metadata,
    const original_fact_batch_t* original_batch,
    ruleforge_fact_t decision_fact,
    uint64_t message_index,
    int decision_index,
    int decision_count)
{
    ruleforge_route_envelope_t route = {
        .source_name = source_name,
        .source_query = source_query,
        .fact_type = fact_type,
        .target_name = target_name,
        .payload = payload,
        .metadata = metadata,
        .effective_metadata = choose_metadata_payload(payload, metadata),
        .original_fact = original_batch ? original_batch->primary_fact : NULL,
        .decision_fact = decision_fact,
        .message_index = message_index,
        .decision_index = decision_index,
        .decision_count = decision_count,
        .original_facts = original_batch ? original_batch->facts : NULL,
        .original_fact_count = original_batch ? original_batch->fact_count : 0
    };
    return route;
}

static ruleforge_status_t push_route_to_sink(datasink_entry_t* sink,
                                             const ruleforge_route_envelope_t* route) {
    if (!sink || !route) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    return sink->vtable.push_route(sink->ctx, route);
}

static ruleforge_status_t dup_fact_string(ruleforge_fact_t fact,
                                          const char* field_name,
                                          char** out_value) {
    char small[1] = {0};
    size_t actual_len = 0;
    ruleforge_status_t status;

    if (!out_value) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    *out_value = NULL;
    status = ruleforge_fact_get_field_as_string(
        fact, field_name, small, sizeof(small), &actual_len);
    if (status == RULES_FORGE_OK) {
        *out_value = (char*)malloc(actual_len + 1);
        if (!*out_value) {
            return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
        }
        memcpy(*out_value, small, actual_len + 1);
        return RULES_FORGE_OK;
    }

    if (status != RULES_FORGE_ERROR_INVALID_ARGUMENT || actual_len == 0) {
        return status;
    }

    *out_value = (char*)malloc(actual_len + 1);
    if (!*out_value) {
        return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
    }

    status = ruleforge_fact_get_field_as_string(
        fact, field_name, *out_value, actual_len + 1, &actual_len);
    if (status != RULES_FORGE_OK) {
        free(*out_value);
        *out_value = NULL;
    }
    return status;
}

// Execute route decisions from query result.
// The original source payload has already been inserted into `session`.
// Only forward it when there is exactly one unambiguous original fact in working memory.
// Otherwise fall back to the RouteDecision fact, which avoids brittle index-based correlation.
static void execute_routes(
    route_engine_t* engine,
    ruleforge_stateful_session_t session,
    ruleforge_query_result_t query_result,
    const char* source_name,
    const char* source_query,
    const char* fact_type,
    const original_fact_batch_t* original_batch,
    uint64_t message_index)
{
    const int is_batch_route = (original_batch && original_batch->fact_count > 1);
    datasink_entry_t* batch_seen_sinks[MAX_SINKS];
    int batch_seen_count = 0;
    int decision_count = ruleforge_query_result_get_size(query_result);
    int route_decision_count;
    if (decision_count < 0) {
        stats_note_route_error(engine);
        return;
    }

    if (is_batch_route) {
        for (int i = 0; i < decision_count; ++i) {
            ruleforge_fact_t decision_fact = NULL;
            char* sink_name = NULL;
            char* actual_target = NULL;
            datasink_entry_t* sink = NULL;

            if (ruleforge_query_result_get_fact_at_index(query_result, i, "$decision", &decision_fact) != RULES_FORGE_OK ||
                decision_fact == NULL) {
                continue;
            }
            if (dup_fact_string(decision_fact, "target", &sink_name) != RULES_FORGE_OK || sink_name == NULL) {
                continue;
            }

            actual_target = sink_name;
            size_t name_len = strlen(sink_name);
            if (name_len >= 2 && sink_name[0] == '"' && sink_name[name_len - 1] == '"') {
                sink_name[name_len - 1] = '\0';
                actual_target = sink_name + 1;
            }

            sink = actual_target[0] ? find_sink(engine, actual_target) : NULL;
            if (sink) {
                bool seen = false;
                for (int seen_idx = 0; seen_idx < batch_seen_count; ++seen_idx) {
                    if (batch_seen_sinks[seen_idx] == sink) {
                        seen = true;
                        break;
                    }
                }
                if (!seen && batch_seen_count < MAX_SINKS) {
                    batch_seen_sinks[batch_seen_count++] = sink;
                }
            }
            free(sink_name);
        }
        route_decision_count = batch_seen_count;
        batch_seen_count = 0;
    } else {
        route_decision_count = decision_count;
    }

    stats_note_decisions(engine, route_decision_count);

    if (decision_count == 0) {
        stats_note_no_route(engine);
        if (!engine->config.drop_no_route && engine->config.default_target) {
            datasink_entry_t* sink = find_sink(engine, engine->config.default_target);
            if (!sink) {
                stats_note_route_error(engine);
            } else if (!sink_accepts_route(sink, original_batch)) {
                stats_note_route_error(engine);
            } else {
                ruleforge_route_envelope_t route = build_route_envelope(
                    source_name,
                    source_query,
                    fact_type,
                    engine->config.default_target,
                    NULL,
                    NULL,
                    original_batch,
                    NULL,
                    message_index,
                    0,
                    0);
                if (push_route_to_sink(sink, &route) == RULES_FORGE_OK) {
                    stats_note_total_routed(engine);
                } else {
                    stats_note_route_error(engine);
                }
            }
        }
        return;
    }

    int dispatched_count = 0;
    for (int i = 0; i < decision_count; i++) {
        ruleforge_fact_t decision_fact = NULL;
        ruleforge_status_t fetch_status = ruleforge_query_result_get_fact_at_index(
            query_result, i, "$decision", &decision_fact);
            
        if (fetch_status != RULES_FORGE_OK || decision_fact == NULL) {
            stats_note_route_error(engine);
            continue;
        }

        char* sink_name = NULL;
        ruleforge_status_t target_status = dup_fact_string(
            decision_fact, "target", &sink_name);

        if (target_status != RULES_FORGE_OK) {
            stats_note_route_error(engine);
            continue;
        }

        /* Strip surrounding quotes if present (RFL parser may preserve them) */
        char* actual_target = sink_name;
        size_t name_len = strlen(sink_name);
        if (name_len >= 2 && sink_name[0] == '"' && sink_name[name_len - 1] == '"') {
            sink_name[name_len - 1] = '\0';
            actual_target = sink_name + 1;
        }
        if (!actual_target[0]) {
            free(sink_name);
            stats_note_route_error(engine);
            continue;
        }

        datasink_entry_t* sink = find_sink(engine, actual_target);
        if (!sink) {
            free(sink_name);
            stats_note_route_error(engine);
            continue;
        }

        if (is_batch_route) {
            bool already_pushed = false;
            for (int seen_idx = 0; seen_idx < batch_seen_count; ++seen_idx) {
                if (batch_seen_sinks[seen_idx] == sink) {
                    already_pushed = true;
                    break;
                }
            }
            if (already_pushed) {
                free(sink_name);
                continue;
            }
        }

        char* payload = NULL;
        char* metadata = NULL;
        ruleforge_status_t payload_status = dup_fact_string(decision_fact, "payload", &payload);
        ruleforge_status_t metadata_status = dup_fact_string(decision_fact, "metadata", &metadata);

        if (payload_status != RULES_FORGE_OK &&
            payload_status != RULES_FORGE_ERROR_INVALID_ARGUMENT) {
            free(metadata);
            free(payload);
            free(sink_name);
            stats_note_route_error(engine);
            continue;
        }

        if (metadata_status != RULES_FORGE_OK &&
            metadata_status != RULES_FORGE_ERROR_INVALID_ARGUMENT) {
            free(metadata);
            free(payload);
            free(sink_name);
            stats_note_route_error(engine);
            continue;
        }

        ruleforge_route_envelope_t route = build_route_envelope(
            source_name,
            source_query,
            fact_type,
            actual_target,
            (payload && payload[0]) ? payload : NULL,
            (metadata && metadata[0]) ? metadata : NULL,
            original_batch,
            decision_fact,
            message_index,
            dispatched_count,
            route_decision_count);

        if (push_route_to_sink(sink, &route) == RULES_FORGE_OK) {
            stats_note_total_routed(engine);
            dispatched_count++;
            if (is_batch_route && batch_seen_count < MAX_SINKS) {
                batch_seen_sinks[batch_seen_count++] = sink;
            }
        } else {
            stats_note_route_error(engine);
        }
        free(metadata);
        free(payload);
        free(sink_name);
    }

}

// Process one source payload: add_fact → fire → route
static ruleforge_status_t process_source_payload(
    route_engine_t* engine,
    const char* source_name,
    const char* source_query,
    const char* fact_type,
    DataBindFormat format,
    const uint8_t* data,
    size_t len)
{
    ruleforge_status_t status;

    if (!engine || !engine->buffer_pool || !engine->session_pool || !fact_type || !data || len == 0) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    buffer_t* buffer = buffer_pool_acquire(engine->buffer_pool, 100);
    if (!buffer) { stats_note_route_error(engine); return RULES_FORGE_ERROR_GENERIC; }

    uint64_t message_index = stats_note_payload_begin(engine);

    if (len + 1 > buffer->capacity) {
        buffer_pool_release(engine->buffer_pool, buffer);
        stats_note_parse_error(engine);
        return RULES_FORGE_ERROR_GENERIC;
    }

    memcpy(buffer->data, data, len);
    buffer->data[len] = '\0'; /* Ensure null-termination for string parsers (JSON/CSV) */
    buffer->length = len;

    ruleforge_stateful_session_t session = session_pool_acquire(engine->session_pool, 100);
    if (!session) {
        stats_note_route_error(engine);
        buffer_pool_release(engine->buffer_pool, buffer);
        return RULES_FORGE_ERROR_GENERIC;
    }

    original_fact_batch_t original_batch = {0};
    status = add_fact_by_format(session, fact_type, format, buffer, &original_batch);
    if (status != RULES_FORGE_OK) {
        stats_note_parse_error(engine);
        session_pool_release(engine->session_pool, session);
        buffer_pool_release(engine->buffer_pool, buffer);
        return status;
    }

    int fired;
    status = ruleforge_session_fire_all_rules(session, engine->config.max_rule_fires, &fired);
    if (status != RULES_FORGE_OK) {
        stats_note_route_error(engine);
        free_original_fact_batch(&original_batch);
        session_pool_release(engine->session_pool, session);
        buffer_pool_release(engine->buffer_pool, buffer);
        return status;
    }

    ruleforge_query_result_t query_result;
    if (ruleforge_session_query(session, "get_route_decisions", &query_result) == RULES_FORGE_OK) {
        execute_routes(
            engine,
            session,
            query_result,
            source_name,
            source_query,
            fact_type,
            &original_batch,
            message_index);
        ruleforge_query_result_destroy(query_result);
    }

    free_original_fact_batch(&original_batch);
    session_pool_release(engine->session_pool, session);
    buffer_pool_release(engine->buffer_pool, buffer);
    return RULES_FORGE_OK;
}

    // Start routing loop
ruleforge_status_t route_engine_run(
    route_engine_t* engine,
    const char* source_name,
    const char* query,
    const char* fact_type,
    int max_messages) {

    if (!engine || !source_name || !fact_type)
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;

    datasource_entry_t* source = find_source(engine, source_name);
    if (!source)
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;

    engine->running = 1;
    int processed = 0;

    while (engine->running && (max_messages < 0 || processed < max_messages)) {
        DataBindFormat format;
        const uint8_t* data;
        size_t len;
        uint64_t start_time;
        ruleforge_status_t status = source->vtable.fetch(
            source->ctx, query, fact_type, &format, &data, &len);

        if (is_end_of_stream(status)) {
            engine->running = 0;
            return RULES_FORGE_OK;
        }
        if (status != RULES_FORGE_OK) {
            engine->running = 0;
            return status;
        }

        start_time = get_time_us();
        status = process_source_payload(
            engine,
            source->name,
            query,
            fact_type,
            format,
            data,
            len);
        source->vtable.free_data(source->ctx, data);
        if (status != RULES_FORGE_OK) {
            engine->running = 0;
            return status;
        }

        record_route_time(engine, get_time_us() - start_time);
        processed++;
    }

    engine->running = 0;
    return RULES_FORGE_OK;
}

// Accessor functions
int route_engine_source_count(const route_engine_t* engine) {
    return engine ? engine->source_count : 0;
}

int route_engine_sink_count(const route_engine_t* engine) {
    return engine ? engine->sink_count : 0;
}

// Stop routing loop
ruleforge_status_t route_engine_stop(route_engine_t* engine) {
    if (!engine) return RULES_FORGE_ERROR_INVALID_ARGUMENT;

    engine->running = 0;
    return RULES_FORGE_OK;
}

// Get source plugin context by name
void* route_engine_get_source_ctx(route_engine_t* engine, const char* source_name) {
    if (!engine || !source_name) return NULL;
    datasource_entry_t* entry = find_source(engine, source_name);
    return entry ? entry->ctx : NULL;
}

// Get engine statistics
void route_engine_get_stats(route_engine_t* engine, route_engine_stats_t* out_stats) {
    if (!engine || !out_stats) return;

    turbo_mutex_lock(&engine->stats_lock);
    *out_stats = engine->stats;
    turbo_mutex_unlock(&engine->stats_lock);

    if (engine->session_pool) {
        pool_stats_t s;
        session_pool_get_stats(engine->session_pool, &s);
        out_stats->session_pool_in_use   = s.in_use;
        out_stats->session_pool_available = s.available;
    }

    if (engine->buffer_pool) {
        pool_stats_t s;
        buffer_pool_get_stats(engine->buffer_pool, &s);
        out_stats->buffer_pool_in_use   = s.in_use;
        out_stats->buffer_pool_available = s.available;
    }
}

// Destroy route engine
void route_engine_destroy(route_engine_t* engine) {
    if (!engine) return;

    // Destroy buffer pool
    buffer_pool_destroy(engine->buffer_pool);

    // Destroy session pool
    session_pool_destroy(engine->session_pool);

    // Cleanup data sources
    for (int i = 0; i < engine->source_count; i++) {
        engine->sources[i].vtable.cleanup(engine->sources[i].ctx);
        plugin_unload(engine->sources[i].dll_handle);
    }

    // Cleanup data sinks
    for (int i = 0; i < engine->sink_count; i++) {
        engine->sinks[i].vtable.cleanup(engine->sinks[i].ctx);
        plugin_unload(engine->sinks[i].dll_handle);
    }

    // Destroy knowledge base
    ruleforge_kb_destroy(engine->kb);

    // Cleanup RulesForge runtime
    if (engine->runtime_acquired) {
        route_engine_runtime_release();
    }

    turbo_mutex_destroy(&engine->stats_lock);
    free(engine);
}
