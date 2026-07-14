#ifndef __RULE_FORGE_H__
#define __RULE_FORGE_H__

#include <stdint.h>          /* int64_t */
#include <stddef.h>          /* size_t  */
#include <platform.h>

// --- Version Information ---
#define RULEFORGE_VERSION_MAJOR 0
#define RULEFORGE_VERSION_MINOR 5
#define RULEFORGE_VERSION_PATCH 0
#define RULEFORGE_VERSION_STRING "0.5.0"

// --- Thread Safety ---
// RulesForge thread safety guarantees:
//   - KnowledgeBase: safe to share across threads only after rule loading and
//     native-predicate registration are complete and no further mutations happen.
//   - StatefulSession: NOT thread-safe. Each session must be used from a single
//     thread at a time. Create separate sessions for concurrent rule execution.
//   - ContinuousSession: NOT thread-safe. Its DataBind streams and session must
//     be used from the same thread.

#ifdef __cplusplus
extern "C" {
#endif

// --- Status Codes ---
// Standard status codes returned by RulesForge C API functions.
typedef enum {
  RULES_FORGE_OK = 0,
  RULES_FORGE_ERROR_GENERIC = 1,
  RULES_FORGE_ERROR_INVALID_ARGUMENT = 2,
  RULES_FORGE_ERROR_COMPILATION_FAILED = 3,
  RULES_FORGE_ERROR_SESSION_CREATION_FAILED = 4,
  RULES_FORGE_ERROR_FACT_INSERTION_FAILED = 5,
  RULES_FORGE_ERROR_QUERY_FAILED = 6,
  RULES_FORGE_ERROR_MEMORY_ALLOCATION = 7,
  RULES_FORGE_ERROR_SESSION_INCONSISTENT = 8,
  RULES_FORGE_STATUS_END_OF_STREAM = 9,
  RULES_FORGE_ERROR_RESOURCE_LIMIT = 10,
} ruleforge_status_t;

// --- Opaque Types (Handles) ---
// These are opaque handles to internal C++ objects.
// Users of the C API must never dereference them directly.
// Fact handles are owned by the session or query result that produced them.
typedef struct ruleforge_fact_handle_s *ruleforge_fact_t;
typedef struct ruleforge_knowledge_base_handle_s *ruleforge_knowledge_base_t;
typedef struct ruleforge_stateful_session_handle_s *ruleforge_stateful_session_t;
typedef struct ruleforge_query_result_handle_s *ruleforge_query_result_t;
typedef struct ruleforge_data_bind_object_handle_s *ruleforge_data_bind_object_t;
typedef struct ruleforge_data_bind_stream_handle_s *ruleforge_data_bind_stream_t;
typedef struct ruleforge_continuous_session_handle_s *ruleforge_continuous_session_t;
typedef struct ruleforge_continuous_result_handle_s *ruleforge_continuous_result_t;
typedef struct ruleforge_continuous_data_bind_stream_handle_s
    *ruleforge_continuous_data_bind_stream_t;

// Byte sink used by DataBindObject writers. Each callback supplies an arbitrary
// byte chunk; callback count and chunk boundaries have no business semantics.
// Return zero to continue, or non-zero to abort the write.
typedef int (*ruleforge_write_fn)(const void *data, size_t len, void *user);

#define RULEFORGE_CONTINUOUS_CONFIG_ABI_V1 1u

typedef struct {
  uint32_t abi_version;
  size_t struct_size;
  size_t max_active_events;
  size_t max_dedup_entries;
  size_t max_pending_result_batches;
  size_t max_pending_results;
  size_t max_input_batch_size;
  size_t max_replay_steps;
  int max_rules_per_step;
  int64_t allowed_lateness_ms;
  int64_t event_retention_ms;
  int64_t dedup_retention_ms;
  int64_t max_event_time_lead_ms;
  const char *const *output_fact_types;
  size_t output_fact_type_count;
} ruleforge_continuous_config_t;

typedef enum {
  RULES_FORGE_CONTINUOUS_COMMITTED = 0,
  RULES_FORGE_CONTINUOUS_DRAIN_REQUIRED = 1,
} ruleforge_continuous_step_status_t;

typedef struct {
  uint64_t accepted_events;
  uint64_t expired_events;
  uint64_t rejected_duplicates;
  uint64_t rejected_late_events;
  uint64_t rejected_resource_limits;
  uint64_t replay_recoveries;
  size_t active_events;
  size_t dedup_entries;
  size_t pending_result_batches;
  size_t pending_results;
} ruleforge_continuous_metrics_t;

typedef enum {
  RULES_FORGE_VALIDATION_NONE = 0,
  RULES_FORGE_VALIDATION_WARN = 1,
  RULES_FORGE_VALIDATION_STRICT = 2,
} ruleforge_validation_mode_t;

typedef enum {
  RULES_FORGE_EXECUTION_MODE_DEFAULT = 0,
  RULES_FORGE_EXECUTION_MODE_V1_STANDARD = 1,
  RULES_FORGE_EXECUTION_MODE_V2_HIGH_PERFORMANCE = 2,
} ruleforge_execution_mode_t;

// --- Global Initialization and Cleanup ---
// Call these once at the start and end of your application.
CXX_C_API ruleforge_status_t ruleforge_init(void);
CXX_C_API ruleforge_status_t ruleforge_cleanup(void);

// Returns the library version string (e.g., "0.5.0").
CXX_C_API const char *ruleforge_get_version(void);

// Returns a pointer to a thread-local error string.
// The returned pointer remains valid until the next RulesForge API call on the
// same thread.
CXX_C_API const char *ruleforge_get_last_error_message(void);

// --- Schema-bound DataBind Objects ---
// Each constructor loads schema_path and returns an independently owned object.
// The object remains valid after the internal DataBind codec is released and
// must be destroyed with ruleforge_data_bind_object_destroy(). Text inputs are
// length-delimited UTF-8 and need not be NUL-terminated.
//
// Common parameters:
//   schema_path: trusted DataBind schema file used to bind the object.
//   fact_type: schema message type copied into the returned object.
//   data/text + len: length-delimited source payload.
//   out_object: receives NULL on failure and a caller-owned handle on success.
// Returns RULES_FORGE_OK, RULES_FORGE_ERROR_INVALID_ARGUMENT for schema/input
// errors, RULES_FORGE_ERROR_MEMORY_ALLOCATION for allocation failure, or
// RULES_FORGE_ERROR_GENERIC for runtime/I/O failure. Detailed diagnostics are
// available through ruleforge_get_last_error_message().
//
// Example:
//   ruleforge_data_bind_object_t object = NULL;
//   const char json[] = "{\"id\":7}";
//   if (ruleforge_data_bind_object_from_json(
//           "item.schema", "Item", json, sizeof(json) - 1, &object)
//       == RULES_FORGE_OK) {
//     ruleforge_data_bind_object_destroy(object);
//   }
CXX_C_API ruleforge_status_t ruleforge_data_bind_object_from_binary(
    const char *schema_path, const char *fact_type, const uint8_t *data,
    size_t len, ruleforge_data_bind_object_t *out_object);
CXX_C_API ruleforge_status_t ruleforge_data_bind_object_from_json(
    const char *schema_path, const char *fact_type, const char *json,
    size_t len, ruleforge_data_bind_object_t *out_object);
CXX_C_API ruleforge_status_t ruleforge_data_bind_object_from_yaml(
    const char *schema_path, const char *fact_type, const char *yaml,
    size_t len, ruleforge_data_bind_object_t *out_object);
CXX_C_API ruleforge_status_t ruleforge_data_bind_object_from_xml(
    const char *schema_path, const char *fact_type, const char *xml,
    size_t len, ruleforge_data_bind_object_t *out_object);
// csv must include a header row; row is the zero-based data-row index.
CXX_C_API ruleforge_status_t ruleforge_data_bind_object_from_csv(
    const char *schema_path, const char *fact_type, const char *csv,
    size_t len, size_t row, ruleforge_data_bind_object_t *out_object);
CXX_C_API ruleforge_status_t ruleforge_data_bind_object_clone(
    ruleforge_data_bind_object_t object,
    ruleforge_data_bind_object_t *out_object);

// Returns a borrowed type-name view valid until object is destroyed.
CXX_C_API const char *ruleforge_data_bind_object_get_type_name(
    ruleforge_data_bind_object_t object);

// Serialized JSON/YAML/XML buffers are owned by the caller and must be released
// with ruleforge_data_bind_serialized_free(). CSV and binary object serialization
// are not provided by DataBind 1.10.0. out_len is optional; output buffers are
// set to NULL on failure. Writers return RULES_FORGE_ERROR_GENERIC when the sink
// rejects a chunk and otherwise use the same status mapping as serialization.
CXX_C_API ruleforge_status_t ruleforge_data_bind_object_serialize_json(
    ruleforge_data_bind_object_t object, char **out_json, size_t *out_len);
CXX_C_API ruleforge_status_t ruleforge_data_bind_object_serialize_yaml(
    ruleforge_data_bind_object_t object, char **out_yaml, size_t *out_len);
CXX_C_API ruleforge_status_t ruleforge_data_bind_object_serialize_xml(
    ruleforge_data_bind_object_t object, char **out_xml, size_t *out_len);
CXX_C_API ruleforge_status_t ruleforge_data_bind_object_write_json(
    ruleforge_data_bind_object_t object, ruleforge_write_fn write, void *user);
CXX_C_API ruleforge_status_t ruleforge_data_bind_object_write_yaml(
    ruleforge_data_bind_object_t object, ruleforge_write_fn write, void *user);
CXX_C_API ruleforge_status_t ruleforge_data_bind_object_write_xml(
    ruleforge_data_bind_object_t object, ruleforge_write_fn write, void *user);
CXX_C_API void ruleforge_data_bind_serialized_free(char *data);
CXX_C_API ruleforge_status_t ruleforge_data_bind_object_destroy(
    ruleforge_data_bind_object_t object);

// --- Knowledge Base (Rules) Management ---
// Creates a new, empty Knowledge Base.
// Returns RULES_FORGE_OK on success, and sets 'out_kb' to the handle.
// On failure, 'out_kb' will be NULL.
CXX_C_API ruleforge_status_t ruleforge_kb_create(ruleforge_knowledge_base_t *out_kb);

// Selects the rule execution mode used by sessions created from this KB.
// Set this before ruleforge_session_create(). DEFAULT uses the build default.
// v1_standard keeps exact salience ordering. v2_high_performance is usually
// 20%-30% faster, with coarser salience buckets.
CXX_C_API ruleforge_status_t ruleforge_kb_set_execution_mode(ruleforge_knowledge_base_t kb,
                                                             ruleforge_execution_mode_t mode);

// Returns the selected execution mode: "v1_standard" or "v2_high_performance".
CXX_C_API const char *ruleforge_kb_get_execution_mode(ruleforge_knowledge_base_t kb);

// Loads RFL rules into the Knowledge Base from an in-memory source string.
// Returns RULES_FORGE_OK on success.
CXX_C_API ruleforge_status_t ruleforge_kb_load_drl(ruleforge_knowledge_base_t kb,
                                                   const char *drl_source);

// Loads a single RFL file into the Knowledge Base, resolving import statements.
// file_path: Path to the root RFL file.
// base_dirs: Array of directories to search for imported files (NULL = no import resolution).
// base_dir_count: Number of entries in base_dirs.
// Returns RULES_FORGE_OK on success.
CXX_C_API ruleforge_status_t ruleforge_kb_load_drl_file(ruleforge_knowledge_base_t kb,
                                                        const char *file_path,
                                                        const char **base_dirs,
                                                        int base_dir_count);

// Loads multiple RFL files and merges them into the Knowledge Base.
// file_paths: Array of RFL file paths.
// file_count: Number of files.
// base_dirs: Array of directories to search for imported files (NULL = no import resolution).
// base_dir_count: Number of entries in base_dirs.
// Returns RULES_FORGE_OK on success.
CXX_C_API ruleforge_status_t ruleforge_kb_load_drl_files(ruleforge_knowledge_base_t kb,
                                                         const char **file_paths,
                                                         int file_count,
                                                         const char **base_dirs,
                                                         int base_dir_count);

// Loads rules from a decision table CSV into the Knowledge Base.
// csv_source: The CSV content as a string.
// Returns RULES_FORGE_OK on success.
CXX_C_API ruleforge_status_t ruleforge_kb_load_decision_table_csv(ruleforge_knowledge_base_t kb,
                                                                  const char *csv_source);

// Destroys a Knowledge Base and frees its associated resources.
// The handle becomes invalid after this call.
CXX_C_API ruleforge_status_t ruleforge_kb_destroy(ruleforge_knowledge_base_t kb);

// --- Stateful Session Management ---
// Creates a new Stateful Session from a Knowledge Base.
// Returns RULES_FORGE_OK on success, and sets 'out_session' to the handle.
// On failure, 'out_session' will be NULL.
CXX_C_API ruleforge_status_t ruleforge_session_create(ruleforge_knowledge_base_t kb,
                                                      ruleforge_stateful_session_t *out_session);

// --- Continuous Session Management ---
// Continuous sessions are single-threaded and own all event/runtime state.
// Initialize config with this function before overriding bounded values.
CXX_C_API ruleforge_status_t
ruleforge_continuous_config_init(ruleforge_continuous_config_t *config);
CXX_C_API ruleforge_status_t ruleforge_continuous_session_create(
    ruleforge_knowledge_base_t kb, const ruleforge_continuous_config_t *config,
    ruleforge_continuous_session_t *out_session);
CXX_C_API ruleforge_status_t ruleforge_continuous_session_destroy(
    ruleforge_continuous_session_t session);

// Commits an existing DataBindObject as one event. The session copies the
// object's value; ownership remains with the caller. event_id and entry_point
// must be non-NULL and out_result receives NULL on failure. The object's schema
// type must already be imported by the knowledge base.
CXX_C_API ruleforge_status_t ruleforge_continuous_push_data_bind_object(
    ruleforge_continuous_session_t session,
    ruleforge_data_bind_object_t object, const char *event_id,
    const char *entry_point, int64_t event_time_ms,
    ruleforge_continuous_result_t *out_result);

// Parses one schema-bound JSON object and commits it as one event step.
CXX_C_API ruleforge_status_t ruleforge_continuous_push_json_schema(
    ruleforge_continuous_session_t session, const char *schema_path,
    const char *fact_type, const char *event_id, const char *entry_point,
    int64_t event_time_ms, const char *fact_json,
    ruleforge_continuous_result_t *out_result);

// Parses one schema-bound YAML mapping and commits it as one event step.
CXX_C_API ruleforge_status_t ruleforge_continuous_push_yaml_schema(
    ruleforge_continuous_session_t session, const char *schema_path,
    const char *fact_type, const char *event_id, const char *entry_point,
    int64_t event_time_ms, const char *fact_yaml,
    ruleforge_continuous_result_t *out_result);

// Selects all matching records, reads per-event metadata from bound fields,
// and commits the selected records as one atomic continuous batch. YAML paths
// use YPATH syntax (for example, /events/*).
CXX_C_API ruleforge_status_t ruleforge_continuous_push_json_path_schema(
    ruleforge_continuous_session_t session, const char *schema_path,
    const char *fact_type, const char *json_source, const char *json_path,
    const char *event_id_field, const char *event_time_field,
    const char *entry_point, ruleforge_continuous_result_t *out_result);
CXX_C_API ruleforge_status_t ruleforge_continuous_push_yaml_path_schema(
    ruleforge_continuous_session_t session, const char *schema_path,
    const char *fact_type, const char *yaml_source, const char *yaml_path,
    const char *event_id_field, const char *event_time_field,
    const char *entry_point, ruleforge_continuous_result_t *out_result);
CXX_C_API ruleforge_status_t ruleforge_continuous_push_csv_path_schema(
    ruleforge_continuous_session_t session, const char *schema_path,
    const char *fact_type, const char *csv_source, const char *csv_path,
    const char *event_id_field, const char *event_time_field,
    const char *entry_point, ruleforge_continuous_result_t *out_result);
CXX_C_API ruleforge_status_t ruleforge_continuous_push_xml_path_schema(
    ruleforge_continuous_session_t session, const char *schema_path,
    const char *fact_type, const char *xml_source, const char *xml_path,
    const char *event_id_field, const char *event_time_field,
    const char *entry_point, ruleforge_continuous_result_t *out_result);

CXX_C_API ruleforge_status_t ruleforge_continuous_advance_watermark(
    ruleforge_continuous_session_t session, int64_t watermark_ms,
    ruleforge_continuous_result_t *out_result);
CXX_C_API ruleforge_status_t ruleforge_continuous_drain(
    ruleforge_continuous_session_t session,
    ruleforge_continuous_result_t *out_result);
CXX_C_API ruleforge_status_t ruleforge_continuous_acknowledge(
    ruleforge_continuous_session_t session, uint64_t batch_id);
CXX_C_API ruleforge_status_t ruleforge_continuous_get_metrics(
    ruleforge_continuous_session_t session,
    ruleforge_continuous_metrics_t *out_metrics);

// Incremental JSON/YAML DataBind adapters. Event metadata is fixed at creation;
// finish parses the complete object and commits exactly one event step.
CXX_C_API ruleforge_status_t ruleforge_continuous_data_bind_stream_json_create(
    ruleforge_continuous_session_t session, const char *schema_path,
    const char *fact_type, const char *event_id, const char *entry_point,
    int64_t event_time_ms, ruleforge_continuous_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_continuous_data_bind_stream_yaml_create(
    ruleforge_continuous_session_t session, const char *schema_path,
    const char *fact_type, const char *event_id, const char *entry_point,
    int64_t event_time_ms, ruleforge_continuous_data_bind_stream_t *out_stream);

// Incremental path-selected batch adapters. Metadata field names and entry
// point are fixed at creation; finish atomically commits all selected events.
// YAML paths use YPATH syntax.
CXX_C_API ruleforge_status_t ruleforge_continuous_data_bind_stream_json_path_create(
    ruleforge_continuous_session_t session, const char *schema_path,
    const char *fact_type, const char *json_path, const char *event_id_field,
    const char *event_time_field, const char *entry_point,
    ruleforge_continuous_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_continuous_data_bind_stream_yaml_path_create(
    ruleforge_continuous_session_t session, const char *schema_path,
    const char *fact_type, const char *yaml_path, const char *event_id_field,
    const char *event_time_field, const char *entry_point,
    ruleforge_continuous_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_continuous_data_bind_stream_csv_path_create(
    ruleforge_continuous_session_t session, const char *schema_path,
    const char *fact_type, const char *csv_path, const char *event_id_field,
    const char *event_time_field, const char *entry_point,
    ruleforge_continuous_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_continuous_data_bind_stream_xml_path_create(
    ruleforge_continuous_session_t session, const char *schema_path,
    const char *fact_type, const char *xml_path, const char *event_id_field,
    const char *event_time_field, const char *entry_point,
    ruleforge_continuous_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_continuous_data_bind_stream_feed(
    ruleforge_continuous_data_bind_stream_t stream, const void *data, size_t len);
CXX_C_API ruleforge_status_t ruleforge_continuous_data_bind_stream_feed_file(
    ruleforge_continuous_data_bind_stream_t stream, const char *file_path);
CXX_C_API ruleforge_status_t ruleforge_continuous_data_bind_stream_finish(
    ruleforge_continuous_data_bind_stream_t stream,
    ruleforge_continuous_result_t *out_result);
CXX_C_API ruleforge_status_t ruleforge_continuous_data_bind_stream_destroy(
    ruleforge_continuous_data_bind_stream_t stream);

// Result handles own immutable output fact snapshots. Borrowed fact handles
// returned by get_output become invalid when the result is destroyed.
CXX_C_API ruleforge_continuous_step_status_t
ruleforge_continuous_result_get_status(ruleforge_continuous_result_t result);
CXX_C_API uint64_t
ruleforge_continuous_result_get_batch_id(ruleforge_continuous_result_t result);
CXX_C_API int
ruleforge_continuous_result_get_rules_fired(ruleforge_continuous_result_t result);
CXX_C_API size_t
ruleforge_continuous_result_get_events_expired(ruleforge_continuous_result_t result);
CXX_C_API ruleforge_status_t ruleforge_continuous_result_get_watermark(
    ruleforge_continuous_result_t result, int64_t *out_watermark_ms,
    int *out_has_watermark);
CXX_C_API int
ruleforge_continuous_result_get_output_count(ruleforge_continuous_result_t result);
CXX_C_API ruleforge_status_t ruleforge_continuous_result_get_output(
    ruleforge_continuous_result_t result, int index, ruleforge_fact_t *out_fact);
CXX_C_API ruleforge_status_t ruleforge_continuous_result_destroy(
    ruleforge_continuous_result_t result);

// Adds an existing DataBindObject as one fact. The session copies the object's
// value; ownership remains with the caller and the object may be reused. out_fact
// is optional. The object's schema type must already be imported by the session's
// knowledge base; insertion and consistency errors use the existing session
// status codes and ruleforge_get_last_error_message().
CXX_C_API ruleforge_status_t ruleforge_session_add_data_bind_object(
    ruleforge_stateful_session_t session, ruleforge_data_bind_object_t object,
    ruleforge_fact_t *out_fact);

// Adds one schema-bound JSON fact.
CXX_C_API ruleforge_status_t
ruleforge_session_add_fact_json_schema(ruleforge_stateful_session_t session,
                                       const char *schema_path,
                                       const char *fact_type,
                                       const char *fact_json,
                                       ruleforge_fact_t *out_fact);

// Adds the first schema-bound fact selected by a non-empty JSONPath expression.
CXX_C_API ruleforge_status_t
ruleforge_session_add_fact_json_path_schema(ruleforge_stateful_session_t session,
                                            const char *schema_path,
                                            const char *fact_type,
                                            const char *fact_json,
                                            const char *json_path,
                                            ruleforge_fact_t *out_fact);

// Adds all schema-bound facts selected by a non-empty JSONPath expression.
CXX_C_API ruleforge_status_t
ruleforge_session_add_facts_json_path_schema(ruleforge_stateful_session_t session,
                                             const char *schema_path,
                                             const char *fact_type,
                                             const char *fact_json,
                                             const char *json_path,
                                             ruleforge_fact_t **out_facts,
                                             int *out_loaded_count);

// Adds one schema-bound YAML fact.
CXX_C_API ruleforge_status_t
ruleforge_session_add_fact_yaml_schema(ruleforge_stateful_session_t session,
                                       const char *schema_path,
                                       const char *fact_type,
                                       const char *fact_yaml,
                                       ruleforge_fact_t *out_fact);

// Adds the first schema-bound fact selected by a non-empty YPATH expression.
CXX_C_API ruleforge_status_t
ruleforge_session_add_fact_yaml_path_schema(ruleforge_stateful_session_t session,
                                            const char *schema_path,
                                            const char *fact_type,
                                            const char *fact_yaml,
                                            const char *yaml_path,
                                            ruleforge_fact_t *out_fact);

// Adds all schema-bound facts selected by a non-empty YPATH expression.
CXX_C_API ruleforge_status_t
ruleforge_session_add_facts_yaml_path_schema(ruleforge_stateful_session_t session,
                                             const char *schema_path,
                                             const char *fact_type,
                                             const char *fact_yaml,
                                             const char *yaml_path,
                                             ruleforge_fact_t **out_facts,
                                             int *out_loaded_count);

// Adds one schema-bound binary fact.
CXX_C_API ruleforge_status_t
ruleforge_session_add_fact_binary_schema(ruleforge_stateful_session_t session,
                                         const char *schema_path,
                                         const char *fact_type,
                                         const uint8_t *fact_data,
                                         size_t fact_len,
                                         ruleforge_fact_t *out_fact);

// Adds schema-bound CSV facts. csv_source must include a header row.
CXX_C_API ruleforge_status_t
ruleforge_session_add_facts_csv_schema(ruleforge_stateful_session_t session,
                                       const char *schema_path,
                                       const char *fact_type,
                                       const char *csv_source,
                                       ruleforge_fact_t **out_facts,
                                       int *out_loaded_count);

// Adds schema-bound CSV rows selected by a non-empty CSVPath expression.
CXX_C_API ruleforge_status_t
ruleforge_session_add_facts_csv_path_schema(ruleforge_stateful_session_t session,
                                            const char *schema_path,
                                            const char *fact_type,
                                            const char *csv_source,
                                            const char *csv_path,
                                            ruleforge_fact_t **out_facts,
                                            int *out_loaded_count);

// Adds schema-bound XML facts.
// xpath may be NULL or empty to bind the document root.
CXX_C_API ruleforge_status_t
ruleforge_session_add_facts_xml_schema(ruleforge_stateful_session_t session,
                                       const char *schema_path,
                                       const char *fact_type,
                                       const char *xml_source,
                                       const char *xpath,
                                       ruleforge_fact_t **out_facts,
                                       int *out_loaded_count);

// Creates incremental schema-bound input streams. YAML path parameters use
// YPATH syntax. Feed chunks as they arrive,
// then call finish to validate the complete result and batch-insert its facts.
// A stream and its session must be used from the same thread. Destroying a
// session with an active stream is rejected.
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_json_create(
    ruleforge_stateful_session_t session, const char *schema_path,
    const char *fact_type, ruleforge_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_json_all_create(
    ruleforge_stateful_session_t session, const char *schema_path,
    const char *fact_type, ruleforge_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_json_path_create(
    ruleforge_stateful_session_t session, const char *schema_path,
    const char *fact_type, const char *json_path,
    ruleforge_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_json_path_all_create(
    ruleforge_stateful_session_t session, const char *schema_path,
    const char *fact_type, const char *json_path,
    ruleforge_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_yaml_create(
    ruleforge_stateful_session_t session, const char *schema_path,
    const char *fact_type, ruleforge_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_yaml_all_create(
    ruleforge_stateful_session_t session, const char *schema_path,
    const char *fact_type, ruleforge_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_yaml_path_create(
    ruleforge_stateful_session_t session, const char *schema_path,
    const char *fact_type, const char *yaml_path,
    ruleforge_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_yaml_path_all_create(
    ruleforge_stateful_session_t session, const char *schema_path,
    const char *fact_type, const char *yaml_path,
    ruleforge_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_csv_all_create(
    ruleforge_stateful_session_t session, const char *schema_path,
    const char *fact_type, ruleforge_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_csv_path_create(
    ruleforge_stateful_session_t session, const char *schema_path,
    const char *fact_type, const char *csv_path,
    ruleforge_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_xml_create(
    ruleforge_stateful_session_t session, const char *schema_path,
    const char *fact_type, ruleforge_data_bind_stream_t *out_stream);
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_xml_path_all_create(
    ruleforge_stateful_session_t session, const char *schema_path,
    const char *fact_type, const char *xml_path,
    ruleforge_data_bind_stream_t *out_stream);

// Feeds memory or a file into an incremental stream. A failed feed makes the
// stream unusable for further feed/finish calls; destroy it to release resources.
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_feed(
    ruleforge_data_bind_stream_t stream, const void *data, size_t len);
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_feed_file(
    ruleforge_data_bind_stream_t stream, const char *file_path);

// Finishes parsing and inserts all results. out_facts may be NULL; otherwise
// release the returned array with ruleforge_fact_array_free().
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_finish(
    ruleforge_data_bind_stream_t stream, ruleforge_fact_t **out_facts,
    int *out_loaded_count);
CXX_C_API ruleforge_status_t ruleforge_data_bind_stream_destroy(
    ruleforge_data_bind_stream_t stream);

// Frees an array returned by schema-bound multi-fact input APIs.
CXX_C_API void ruleforge_fact_array_free(ruleforge_fact_t *facts);

// Fires all rules in the Stateful Session.
// max_rules: Maximum number of rules to fire (-1 for unlimited).
//            Use this to prevent infinite loops in rules.
// out_fired_count: If not NULL, set to the number of rules fired.
// Returns RULES_FORGE_OK on success.
CXX_C_API ruleforge_status_t ruleforge_session_fire_all_rules(ruleforge_stateful_session_t session,
                                                              int max_rules, int *out_fired_count);

// Resets the Stateful Session, clearing all facts and rules execution state.
// This is necessary if the session becomes inconsistent after an error.
// Returns RULES_FORGE_OK on success.
CXX_C_API ruleforge_status_t ruleforge_session_reset(ruleforge_stateful_session_t session);

// Gets the number of facts currently in the session's working memory.
// Returns the fact count, or -1 on error.
CXX_C_API int ruleforge_session_get_fact_count(ruleforge_stateful_session_t session);

// Executes a query on the Stateful Session.
// query_name: The name of the query to execute.
// out_query_result: On success, set to a handle for the query results. Must be destroyed with
// ruleforge_query_result_destroy(). Returns RULES_FORGE_OK on success.
CXX_C_API ruleforge_status_t ruleforge_session_query(ruleforge_stateful_session_t session,
                                                     const char *query_name,
                                                     ruleforge_query_result_t *out_query_result);

// Destroys a Stateful Session and frees its associated resources.
// The handle becomes invalid after this call.
CXX_C_API ruleforge_status_t ruleforge_session_destroy(ruleforge_stateful_session_t session);

// Sets schema validation mode for fact insertion in this session.
// Returns RULES_FORGE_OK on success.
CXX_C_API ruleforge_status_t
ruleforge_session_set_validation_mode(ruleforge_stateful_session_t session,
                                      ruleforge_validation_mode_t mode);

// --- Session Observability ---
// Enables or disables execution tracing for the session.
// When enabled, rule firings, fact operations, and timing data are recorded.
// enabled: 1 to enable, 0 to disable.
CXX_C_API ruleforge_status_t ruleforge_session_enable_tracing(ruleforge_stateful_session_t session,
                                                              int enabled);

// Gets the execution trace as a formatted string.
// include_network: If non-zero, includes RETE network propagation events.
// buffer: Pre-allocated buffer to copy the trace into.
// buffer_size: The size of the buffer.
// out_actual_length: On success, set to the actual length of the trace (excluding null terminator).
// Returns RULES_FORGE_OK on success. If buffer is too small, returns RULES_FORGE_ERROR_INVALID_ARGUMENT.
CXX_C_API ruleforge_status_t
ruleforge_session_get_execution_trace(ruleforge_stateful_session_t session, int include_network,
                                      char *buffer, size_t buffer_size, size_t *out_actual_length);

// Gets a summary of rule performance (fire counts, timing).
// buffer: Pre-allocated buffer to copy the summary into.
// buffer_size: The size of the buffer.
// out_actual_length: On success, set to the actual length of the summary.
CXX_C_API ruleforge_status_t
ruleforge_session_get_rule_performance_summary(ruleforge_stateful_session_t session, char *buffer,
                                               size_t buffer_size, size_t *out_actual_length);

// Clears all recorded trace events.
CXX_C_API ruleforge_status_t ruleforge_session_clear_trace(ruleforge_stateful_session_t session);

// --- Session Memory Statistics ---
// Gets the current memory used by the session's arena allocator (in bytes).
// Returns the memory used, or -1 on error.
CXX_C_API int64_t ruleforge_session_get_memory_used(ruleforge_stateful_session_t session);

// Gets the peak memory usage of the session's arena allocator (in bytes).
// Returns the peak memory, or -1 on error.
CXX_C_API int64_t ruleforge_session_get_memory_peak(ruleforge_stateful_session_t session);

// Gets formatted memory statistics string.
// buffer: Pre-allocated buffer to copy the stats into.
// buffer_size: The size of the buffer.
// out_actual_length: On success, set to the actual length of the stats string.
CXX_C_API ruleforge_status_t
ruleforge_session_get_memory_stats(ruleforge_stateful_session_t session, char *buffer,
                                   size_t buffer_size, size_t *out_actual_length);

// --- Query Result Access ---
// Gets the number of results (rows) in a query result.
// Returns the number of results, or -1 on error.
CXX_C_API int ruleforge_query_result_get_size(ruleforge_query_result_t query_result);

// Gets a fact from a specific result row and binding name.
// query_result: The query result handle.
// row_index: The 0-based index of the result row.
// binding_name: The name of the binding (e.g., "$customer", "$order").
// out_fact: On success, set to a handle for the fact. This fact handle is owned by the query_result
//           and becomes invalid when query_result is destroyed. Do NOT destroy this fact handle
//           directly.
// Returns RULES_FORGE_OK on success.
CXX_C_API ruleforge_status_t
ruleforge_query_result_get_fact_at_index(ruleforge_query_result_t query_result, int row_index,
                                         const char *binding_name, ruleforge_fact_t *out_fact);

// Destroys a Query Result and frees its associated resources.
// The handle becomes invalid after this call.
CXX_C_API ruleforge_status_t ruleforge_query_result_destroy(ruleforge_query_result_t query_result);

// --- Fact Field Access ---
// Gets a fact field as a string.
// fact: The fact handle.
// field_name: The name of the field.
// buffer: A pre-allocated buffer to copy the string into.
// buffer_size: The size of the buffer.
// out_actual_length: On success, set to the actual length of the string (excluding null
// terminator). Returns RULES_FORGE_OK on success. If buffer is too small, returns
// RULES_FORGE_ERROR_INVALID_ARGUMENT and sets out_actual_length to the required size.
CXX_C_API ruleforge_status_t ruleforge_fact_get_field_as_string(ruleforge_fact_t fact,
                                                                const char *field_name,
                                                                char *buffer, size_t buffer_size,
                                                                size_t *out_actual_length);

// Gets a fact field as a double.
// Returns RULES_FORGE_OK on success.
CXX_C_API ruleforge_status_t ruleforge_fact_get_field_as_double(ruleforge_fact_t fact,
                                                                const char *field_name,
                                                                double *out_value);

// Gets a fact field as an integer (64-bit).
// Returns RULES_FORGE_OK on success.
CXX_C_API ruleforge_status_t ruleforge_fact_get_field_as_int(ruleforge_fact_t fact,
                                                             const char *field_name,
                                                             int64_t *out_value);

// Gets a fact field as a boolean.
// Returns RULES_FORGE_OK on success.
CXX_C_API ruleforge_status_t ruleforge_fact_get_field_as_bool(
    ruleforge_fact_t fact, const char *field_name, int *out_value);

#ifdef __cplusplus
}
#endif

#endif // __RULE_FORGE_H__
