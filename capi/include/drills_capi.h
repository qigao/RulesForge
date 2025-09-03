#ifndef DRILLS_CAPI_H
#define DRILLS_CAPI_H

#include <cstdint> // For int64_t

// Define export/import macros for cross-platform compatibility
#ifndef DRILLS_CAPI_API
    #ifdef _WIN32
        #ifdef DRILLS_CAPI_EXPORTS
            #define DRILLS_CAPI_API __declspec(dllexport)
        #else
            #define DRILLS_CAPI_API __declspec(dllimport)
        #endif
    #else
        #define DRILLS_CAPI_API
    #endif
#endif

#ifndef DRILLS_CAPI_CALL
    #ifdef _WIN32
        #define DRILLS_CAPI_CALL __stdcall
    #else
        #define DRILLS_CAPI_CALL
    #endif
#endif

// Begin extern "C" block for C++ compatibility
#ifdef __cplusplus
extern "C" {
#endif

// --- Opaque Types (Handles) ---
// These are pointers to internal C++ objects.
// Users of the C API should treat them as opaque handles.
typedef void* drills_knowledge_base_t;
typedef void* drills_stateful_session_t;
typedef void* drills_fact_t; // For facts retrieved from queries or created by C API
typedef void* drills_query_result_t;

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
} drills_status_t;

// --- Global Initialization and Cleanup ---
// Call these once at the start and end of your application.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_init(void);
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_cleanup(void);

DRILLS_CAPI_API const char* DRILLS_CAPI_CALL drills_get_last_error_message(void);

// --- Knowledge Base (Rules) Management ---
// Creates a new, empty Knowledge Base.
// Returns DRILLS_OK on success, and sets 'out_kb' to the handle.
// On failure, 'out_kb' will be NULL.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_kb_create(drills_knowledge_base_t* out_kb);

// Loads DRL rules into the Knowledge Base.
// drl_source_json: A JSON string containing the DRL source.
// Returns DRILLS_OK on success.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_kb_load_drl(drills_knowledge_base_t kb, const char* drl_source_json);

// Loads rules from a decision table CSV into the Knowledge Base.
// csv_source: The CSV content as a string.
// Returns DRILLS_OK on success.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_kb_load_decision_table_csv(drills_knowledge_base_t kb, const char* csv_source);

// Destroys a Knowledge Base and frees its associated resources.
// The handle becomes invalid after this call.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_kb_destroy(drills_knowledge_base_t kb);

// --- Stateful Session Management ---
// Creates a new Stateful Session from a Knowledge Base.
// Returns DRILLS_OK on success, and sets 'out_session' to the handle.
// On failure, 'out_session' will be NULL.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_session_create(drills_knowledge_base_t kb, drills_stateful_session_t* out_session);

// Adds a fact to the Stateful Session.
// fact_type: The type of the fact (e.g., "Customer", "Order").
// fact_json: A JSON string representing the fact's fields (e.g., "{\"name\": \"Alice\", \"age\": 30}").
// Returns DRILLS_OK on success.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_session_add_fact_json(drills_stateful_session_t session, const char* fact_type, const char* fact_json);

// Fires all rules in the Stateful Session.
// Returns DRILLS_OK on success.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_session_fire_all_rules(drills_stateful_session_t session);

// Executes a query on the Stateful Session.
// query_name: The name of the query to execute.
// out_query_result: On success, set to a handle for the query results. Must be destroyed with drills_query_result_destroy().
// Returns DRILLS_OK on success.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_session_query(drills_stateful_session_t session, const char* query_name, drills_query_result_t* out_query_result);

// Destroys a Stateful Session and frees its associated resources.
// The handle becomes invalid after this call.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_session_destroy(drills_stateful_session_t session);

// --- Query Result Access ---
// Gets the number of results (rows) in a query result.
// Returns the number of results, or -1 on error.
int DRILLS_CAPI_API DRILLS_CAPI_CALL drills_query_result_get_size(drills_query_result_t query_result);

// Gets a fact from a specific result row and binding name.
// query_result: The query result handle.
// row_index: The 0-based index of the result row.
// binding_name: The name of the binding (e.g., "$customer", "$order").
// out_fact: On success, set to a handle for the fact. This fact handle is owned by the query_result
//           and becomes invalid when query_result is destroyed. Do NOT destroy this fact handle directly.
// Returns DRILLS_OK on success.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_query_result_get_fact_at_index(drills_query_result_t query_result, int row_index, const char* binding_name, drills_fact_t* out_fact);

// Destroys a Query Result and frees its associated resources.
// The handle becomes invalid after this call.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_query_result_destroy(drills_query_result_t query_result);

// --- Fact Field Access ---
// Gets a fact field as a string.
// fact: The fact handle.
// field_name: The name of the field.
// buffer: A pre-allocated buffer to copy the string into.
// buffer_size: The size of the buffer.
// out_actual_length: On success, set to the actual length of the string (excluding null terminator).
// Returns DRILLS_OK on success. If buffer is too small, returns DRILLS_ERROR_INVALID_ARGUMENT
// and sets out_actual_length to the required size.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_fact_get_field_as_string(drills_fact_t fact, const char* field_name, char* buffer, size_t buffer_size, size_t* out_actual_length);

// Gets a fact field as a double.
// Returns DRILLS_OK on success.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_fact_get_field_as_double(drills_fact_t fact, const char* field_name, double* out_value);

// Gets a fact field as an integer (64-bit).
// Returns DRILLS_OK on success.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_fact_get_field_as_int(drills_fact_t fact, const char* field_name, int64_t* out_value);

// Gets a fact field as a boolean.
// Returns DRILLS_OK on success.
drills_status_t DRILLS_CAPI_API DRILLS_CAPI_CALL drills_fact_get_field_as_bool(drills_fact_t fact, const char* field_name, int* out_value); // Use int for bool in C

// End extern "C" block
#ifdef __cplusplus
}
#endif

#endif // DRILLS_CAPI_H
