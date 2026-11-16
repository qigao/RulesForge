#pragma once

#include <toad/script.h>

/* Internal native protocol. All pointers are borrowed for the synchronous call,
 * except session, whose provider owns storage until a successful close. */
typedef struct rf_script_module_source {
  toad_script_string_view_t name;
  toad_script_string_view_t source;
} rf_script_module_source;

typedef struct rf_script_export {
  size_t module_index;
  toad_script_string_view_t name;
  size_t arity;
} rf_script_export;

typedef struct rf_script_limits {
  size_t source_bytes;
  size_t value_bytes;
  size_t value_nodes;
  uint32_t value_depth;
  uint32_t steps;
  uint32_t loops;
  uint32_t recursion;
  size_t retained_bytes;
  size_t stack_bytes;
  toad_script_execution_mode_t mode;
} rf_script_limits;

typedef struct rf_script_diagnostic {
  int status;
  uint32_t phase;
  uint32_t line;
  uint32_t column;
  char message[1024];
} rf_script_diagnostic;

typedef struct rf_script_session rf_script_session;
enum { RF_SCRIPT_NATIVE_MODULE_LIMIT = 16 };
typedef struct rf_script_open {
  size_t struct_size;
  const rf_script_module_source *modules;
  size_t module_count;
  const rf_script_export *exports;
  size_t export_count;
  rf_script_limits limits;
  rf_script_session *session;
  rf_script_diagnostic diagnostic;
  /* Borrowed from the pinned Program generation; Toad owns its acquired leases. */
  cmeta_plugin_registry *native_registry;
  const cmeta_plugin_ref *native_plugins;
  size_t native_plugin_count;
} rf_script_open;

typedef int (*rf_script_result_sink)(void *, const toad_script_value_view_t *);
typedef struct rf_script_call {
  size_t struct_size;
  rf_script_session *session;
  size_t binding;
  const toad_script_value_view_t *arguments;
  size_t argument_count;
  rf_script_result_sink sink;
  void *sink_context;
  rf_script_diagnostic diagnostic;
} rf_script_call;

typedef struct rf_script_close {
  size_t struct_size;
  rf_script_session *session;
  rf_script_diagnostic diagnostic;
} rf_script_close;
