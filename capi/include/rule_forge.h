#ifndef __RULE_FORGE_H__
#define __RULE_FORGE_H__

#include <stdint.h> // For int64_t (C-compatible)
#include <stddef.h> // For size_t (C-compatible)
#include <platform.h>
// --- Version Information ---
#define RULEFORGE_VERSION_MAJOR 0
#define RULEFORGE_VERSION_MINOR 1
#define RULEFORGE_VERSION_PATCH 0
#define RULEFORGE_VERSION_STRING "0.1.0"

// --- Thread Safety ---
// RuleForge thread safety guarantees:
//   - KnowledgeBase: Thread-safe after compilation. Can be shared across threads.
//   - StatefulSession: NOT thread-safe. Each session must be used from a single
//     thread at a time. Create separate sessions for concurrent rule execution.

// Begin extern "C" block for C++ compatibility
#ifdef __cplusplus
extern "C" {
#endif

// --- Opaque Types (Handles) ---
// These are pointers to internal C++ objects.
// Users of the C API should treat them as opaque handles.
typedef void *ruleforge_knowledge_base_t;
typedef void *ruleforge_stateful_session_t;
typedef void *ruleforge_fact_t; // For facts retrieved from queries or created by C API
typedef void *ruleforge_query_result_t;

// --- Status Codes ---
// All C API functions will return one of these status codes.
typedef enum {
  DRILLS_OK = 0,
  DRILLS_ERROR_GENERIC = 1,
  DRILLS_ERROR_INVALID_ARGUMENT = 2,
  DRILLS_ERROR_COMPILATION_FAILED = 3,
  DRILLS_ERROR_SESSION_CREATION_FAILED = 4,
  DRILLS_ERROR_FACT_INSERTION_FAILED = 5,
  DRILLS_ERROR_QUERY_FAILED = 6,
  DRILLS_ERROR_MEMORY_ALLOCATION = 7,
  // Add more specific error codes as needed
} ruleforge_status_t;

// --- Global Initialization and Cleanup ---
// Call these once at the start and end of your application.
CXX_C_API ruleforge_status_t ruleforge_init(void);
CXX_C_API ruleforge_status_t ruleforge_cleanup(void);

// Returns the library version string (e.g., "0.1.0").
CXX_C_API const char *ruleforge_get_version(void);

CXX_C_API const char *ruleforge_get_last_error_message(void);

// --- Knowledge Base (Rules) Management ---
// Creates a new, empty Knowledge Base.
// Returns DRILLS_OK on success, and sets 'out_kb' to the handle.
// On failure, 'out_kb' will be NULL.
CXX_C_API ruleforge_status_t ruleforge_kb_create(ruleforge_knowledge_base_t *out_kb);

// Loads RFL rules into the Knowledge Base.
// drl_source_json: A JSON string containing the RFL source.
// Returns DRILLS_OK on success.
CXX_C_API ruleforge_status_t ruleforge_kb_load_drl(ruleforge_knowledge_base_t kb,
                                                   const char *drl_source_json);

// Loads rules from a decision table CSV into the Knowledge Base.
// csv_source: The CSV content as a string.
// Returns DRILLS_OK on success.
CXX_C_API ruleforge_status_t ruleforge_kb_load_decision_table_csv(ruleforge_knowledge_base_t kb,
                                                                  const char *csv_source);

// Destroys a Knowledge Base and frees its associated resources.
// The handle becomes invalid after this call.
CXX_C_API ruleforge_status_t ruleforge_kb_destroy(ruleforge_knowledge_base_t kb);


// --- Native Function Registration ---
// Callback signature for native C functions callable from rules
// ctx: User-provided context
// argc: Number of arguments
// argv: Array of argument strings (JSON-encoded)
// out_result: Output result (JSON-encoded string, caller must free with free())
// Returns DRILLS_OK on success
typedef ruleforge_status_t (*ruleforge_native_function_t)(
    void *ctx, int argc, const char **argv, char **out_result);

// Register a native C function that can be called from rules
// kb: Knowledge base
// function_name: Name of the function (e.g., "republish", "webhook")
// callback: C function pointer
// user_data: User data passed to callback
// Returns DRILLS_OK on success
CXX_C_API ruleforge_status_t ruleforge_kb_register_native_function(
    ruleforge_knowledge_base_t kb,
    const char *function_name,
    ruleforge_native_function_t callback,
    void *user_data);
// --- Stateful Session Management ---
// Creates a new Stateful Session from a Knowledge Base.
// Returns DRILLS_OK on success, and sets 'out_session' to the handle.
// On failure, 'out_session' will be NULL.
CXX_C_API ruleforge_status_t ruleforge_session_create(ruleforge_knowledge_base_t kb,
                                                      ruleforge_stateful_session_t *out_session);

// Adds a fact to the Stateful Session.
// fact_type: The type of the fact (e.g., "Customer", "Order").
// fact_json: A JSON string representing the fact's fields (e.g., "{\"name\": \"Alice\", \"age\":
// 30}"). Returns DRILLS_OK on success.
CXX_C_API ruleforge_status_t ruleforge_session_add_fact_json(ruleforge_stateful_session_t session,
                                                             const char *fact_type,
                                                             const char *fact_json);

// Fires all rules in the Stateful Session.
// max_rules: Maximum number of rules to fire (-1 for unlimited).
//            Use this to prevent infinite loops in rules.
// out_fired_count: If not NULL, set to the number of rules fired.
// Returns DRILLS_OK on success.
CXX_C_API ruleforge_status_t ruleforge_session_fire_all_rules(ruleforge_stateful_session_t session,
                                                              int max_rules, int *out_fired_count);

// Gets the number of facts currently in the session's working memory.
// Returns the fact count, or -1 on error.
CXX_C_API int ruleforge_session_get_fact_count(ruleforge_stateful_session_t session);

// Executes a query on the Stateful Session.
// query_name: The name of the query to execute.
// out_query_result: On success, set to a handle for the query results. Must be destroyed with
// ruleforge_query_result_destroy(). Returns DRILLS_OK on success.
CXX_C_API ruleforge_status_t ruleforge_session_query(ruleforge_stateful_session_t session,
                                                     const char *query_name,
                                                     ruleforge_query_result_t *out_query_result);

// Destroys a Stateful Session and frees its associated resources.
// The handle becomes invalid after this call.
CXX_C_API ruleforge_status_t ruleforge_session_destroy(ruleforge_stateful_session_t session);

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
// Returns DRILLS_OK on success. If buffer is too small, returns DRILLS_ERROR_INVALID_ARGUMENT.
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
// Returns DRILLS_OK on success.
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
// terminator). Returns DRILLS_OK on success. If buffer is too small, returns
// DRILLS_ERROR_INVALID_ARGUMENT and sets out_actual_length to the required size.
CXX_C_API ruleforge_status_t ruleforge_fact_get_field_as_string(ruleforge_fact_t fact,
                                                                const char *field_name,
                                                                char *buffer, size_t buffer_size,
                                                                size_t *out_actual_length);

// Gets a fact field as a double.
// Returns DRILLS_OK on success.
CXX_C_API ruleforge_status_t ruleforge_fact_get_field_as_double(ruleforge_fact_t fact,
                                                                const char *field_name,
                                                                double *out_value);

// Gets a fact field as an integer (64-bit).
// Returns DRILLS_OK on success.
CXX_C_API ruleforge_status_t ruleforge_fact_get_field_as_int(ruleforge_fact_t fact,
                                                             const char *field_name,
                                                             int64_t *out_value);

// Gets a fact field as a boolean.
// Returns DRILLS_OK on success.
CXX_C_API ruleforge_status_t ruleforge_fact_get_field_as_bool(
    ruleforge_fact_t fact, const char *field_name, int *out_value); // Use int for bool in C

// End extern "C" block
#ifdef __cplusplus
}
#endif

#endif // __RULE_FORGE_H__

