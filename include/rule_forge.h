#ifndef __RULE_FORGE_H__
#define __RULE_FORGE_H__

#include "ruleforge_types.h" /* ruleforge_status_t, ruleforge_fact_t */
#include <stdint.h>          /* int64_t */
#include <stddef.h>          /* size_t  */
#include <platform.h>

// --- Version Information ---
#define RULEFORGE_VERSION_MAJOR 0
#define RULEFORGE_VERSION_MINOR 3
#define RULEFORGE_VERSION_PATCH 0
#define RULEFORGE_VERSION_STRING "0.3.0"

// --- Thread Safety ---
// RulesForge thread safety guarantees:
//   - KnowledgeBase: safe to share across threads only after rule loading and
//     native-function registration are complete and no further mutations happen.
//   - StatefulSession: NOT thread-safe. Each session must be used from a single
//     thread at a time. Create separate sessions for concurrent rule execution.

#ifdef __cplusplus
extern "C" {
#endif

// --- Opaque Types (Handles) ---
// These are opaque handles to internal C++ objects.
// Users of the C API must never dereference them directly.
typedef struct ruleforge_knowledge_base_handle_s *ruleforge_knowledge_base_t;
typedef struct ruleforge_stateful_session_handle_s *ruleforge_stateful_session_t;
typedef struct ruleforge_query_result_handle_s *ruleforge_query_result_t;

typedef enum {
  RULES_FORGE_VALIDATION_NONE = 0,
  RULES_FORGE_VALIDATION_WARN = 1,
  RULES_FORGE_VALIDATION_STRICT = 2,
} ruleforge_validation_mode_t;

// --- Global Initialization and Cleanup ---
// Call these once at the start and end of your application.
CXX_C_API ruleforge_status_t ruleforge_init(void);
CXX_C_API ruleforge_status_t ruleforge_cleanup(void);

// Returns the library version string (e.g., "0.3.0").
CXX_C_API const char *ruleforge_get_version(void);

// Returns a pointer to a thread-local error string.
// The returned pointer remains valid until the next RulesForge API call on the
// same thread.
CXX_C_API const char *ruleforge_get_last_error_message(void);

// --- Knowledge Base (Rules) Management ---
// Creates a new, empty Knowledge Base.
// Returns RULES_FORGE_OK on success, and sets 'out_kb' to the handle.
// On failure, 'out_kb' will be NULL.
CXX_C_API ruleforge_status_t ruleforge_kb_create(ruleforge_knowledge_base_t *out_kb);

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

// --- Native Function Registration ---
// Callback signature for native C functions callable from rules.
// ctx: User-provided context.
// argc: Number of arguments.
// argv: Array of argument strings.
// out_result: Optional JSON/text result string. If set, allocate with malloc/
//             strdup-compatible allocation. RulesForge consumes and frees it.
// Returns RULES_FORGE_OK on success.
typedef ruleforge_status_t (*ruleforge_native_function_t)(
    void *ctx, int argc, const char **argv, char **out_result);

// Plugin ABI for DLL function table loading.
#define RULEFORGE_PLUGIN_ABI_V1 1u

typedef struct {
  const char *name;
  ruleforge_native_function_t callback;
  void *user_data;
} ruleforge_plugin_function_entry_t;

typedef struct {
  uint32_t abi_version;
  uint32_t function_count;
  const ruleforge_plugin_function_entry_t *functions;
} ruleforge_plugin_function_table_t;

typedef ruleforge_status_t (*ruleforge_get_function_table_t)(
    ruleforge_plugin_function_table_t *out_table);

// Register a native C function that can be called from rules.
// kb: Knowledge base.
// function_name: Name of the function (e.g., "republish", "webhook").
// callback: C function pointer.
// user_data: User data passed to callback.
// Returns RULES_FORGE_OK on success.
CXX_C_API ruleforge_status_t ruleforge_kb_register_native_function(
    ruleforge_knowledge_base_t kb,
    const char *function_name,
    ruleforge_native_function_t callback,
    void *user_data);

// Load a DLL/.so bundle of RHS native functions and register all entries.
// This is for invoke()-style native callbacks, not the standard source/sink plugin ABI.
// For standard reusable plugin extension, use rule_forge_plugin.h vtables.
// library_path: Path to plugin library.
// symbol_name: Optional exported symbol name. Pass NULL for default "ruleforge_get_function_table".
CXX_C_API ruleforge_status_t ruleforge_kb_load_native_function_table(
    ruleforge_knowledge_base_t kb,
    const char *library_path,
    const char *symbol_name);

// Load a TurboScript/exprtk plugin (ts_plugin_t ABI) and register its functions.
// This allows RHS actions to leverage external mathematical and string modules.
CXX_C_API ruleforge_status_t ruleforge_kb_load_ts_plugin(
    ruleforge_knowledge_base_t kb,
    const char *library_path);

// --- Stateful Session Management ---
// Creates a new Stateful Session from a Knowledge Base.
// Returns RULES_FORGE_OK on success, and sets 'out_session' to the handle.
// On failure, 'out_session' will be NULL.
CXX_C_API ruleforge_status_t ruleforge_session_create(ruleforge_knowledge_base_t kb,
                                                      ruleforge_stateful_session_t *out_session);

// Adds a fact to the Stateful Session.
// fact_type: The type of the fact (e.g., "Customer", "Order").
// fact_json: A JSON string representing the fact's fields.
// Returns RULES_FORGE_OK on success.
CXX_C_API ruleforge_status_t ruleforge_session_add_fact_json(ruleforge_stateful_session_t session,
                                                             const char *fact_type,
                                                             const char *fact_json);

// Adds a fact to the Stateful Session and returns a stable fact handle owned by the session.
// The returned handle remains valid until the session is reset or destroyed.
CXX_C_API ruleforge_status_t ruleforge_session_add_fact_json_ex(ruleforge_stateful_session_t session,
                                                                const char *fact_type,
                                                                const char *fact_json,
                                                                ruleforge_fact_t *out_fact);

// Adds one fact to the Stateful Session from binary payload.
// The fact type must have a matching declaration available to the session codec registry.
CXX_C_API ruleforge_status_t ruleforge_session_add_fact_binary(ruleforge_stateful_session_t session,
                                                               const char *fact_type,
                                                               const uint8_t *fact_data,
                                                               size_t fact_len);

// Adds one fact to the Stateful Session from binary payload and returns a stable fact handle.
// The returned handle remains valid until the session is reset or destroyed.
CXX_C_API ruleforge_status_t
ruleforge_session_add_fact_binary_ex(ruleforge_stateful_session_t session,
                                     const char *fact_type,
                                     const uint8_t *fact_data,
                                     size_t fact_len,
                                     ruleforge_fact_t *out_fact);

// Adds facts to the Stateful Session from CSV content.
// csv_source must include a header row. Each subsequent row is inserted as one fact.
// out_loaded_count is optional and receives number of successfully loaded rows.
CXX_C_API ruleforge_status_t ruleforge_session_add_facts_csv(ruleforge_stateful_session_t session,
                                                             const char *fact_type,
                                                             const char *csv_source,
                                                             int *out_loaded_count);

// Adds facts to the Stateful Session from CSV content and returns inserted fact handles.
// On success, out_facts points to a heap array of out_loaded_count handles.
// Free the returned array with ruleforge_fact_array_free().
CXX_C_API ruleforge_status_t ruleforge_session_add_facts_csv_ex(ruleforge_stateful_session_t session,
                                                                const char *fact_type,
                                                                const char *csv_source,
                                                                ruleforge_fact_t **out_facts,
                                                                int *out_loaded_count);

// Adds facts to the Stateful Session from a CSV file.
// csv_file_path points to a CSV file that includes a header row.
// out_loaded_count is optional and receives number of successfully loaded rows.
CXX_C_API ruleforge_status_t ruleforge_session_add_facts_csv_file(ruleforge_stateful_session_t session,
                                                                  const char *fact_type,
                                                                  const char *csv_file_path,
                                                                  int *out_loaded_count);

// Adds facts to the Stateful Session from a CSV file and returns inserted fact handles.
// On success, out_facts points to a heap array of out_loaded_count handles.
// Free the returned array with ruleforge_fact_array_free().
CXX_C_API ruleforge_status_t ruleforge_session_add_facts_csv_file_ex(ruleforge_stateful_session_t session,
                                                                     const char *fact_type,
                                                                     const char *csv_file_path,
                                                                     ruleforge_fact_t **out_facts,
                                                                     int *out_loaded_count);

// Frees an array returned by ruleforge_session_add_facts_csv_ex() or
// ruleforge_session_add_facts_csv_file_ex().
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
