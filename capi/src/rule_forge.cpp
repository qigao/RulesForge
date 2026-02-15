// Include the header file to get type definitions
#include "rule_forge.h"

// Include C string handling
#include <cstring>

// Include C++ backend
#include "errors.hpp"
#include "fact_builder.hpp"
#include "knowledge_base.hpp"
#include "query_result.hpp"
#include "rfl_parser.hpp"
#include "stateful_session.hpp"

// Include JSON parsing
#include <jsoncons/json.hpp>

using namespace ruleforge;

// Simple implementation for core C API functions
extern "C" {

// Thread-safe error handling
thread_local char last_error[1024] = "";

// Safe error setter - prevents buffer overflow
static void set_error(const char *msg) { snprintf(last_error, sizeof(last_error), "%s", msg); }

static void set_error_fmt(const char *prefix, const char *detail) {
  snprintf(last_error, sizeof(last_error), "%s%s", prefix, detail);
}

RULEFORGE_API ruleforge_status_t ruleforge_init() {
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

RULEFORGE_API ruleforge_status_t ruleforge_cleanup() {
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

RULEFORGE_API const char *ruleforge_get_last_error_message() { return last_error; }

RULEFORGE_API const char *ruleforge_get_version() { return RULEFORGE_VERSION_STRING; }

// Knowledge Base functions
// Helper function for crossing C++/pure C boundaries safely
struct KnowledgeBaseWrapper {
  std::shared_ptr<KnowledgeBase> kb;
};

RULEFORGE_API ruleforge_status_t ruleforge_kb_create(ruleforge_knowledge_base_t *out_kb) {
  if (!out_kb) {
    set_error("Output Knowledge Base pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    // Create empty knowledge base
    parser_state empty_state;
    auto kb_wrapper = new KnowledgeBaseWrapper();
    kb_wrapper->kb = KnowledgeBase::create(std::move(empty_state));
    *out_kb = static_cast<ruleforge_knowledge_base_t>(kb_wrapper);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to create Knowledge Base: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

RULEFORGE_API ruleforge_status_t ruleforge_kb_load_drl(ruleforge_knowledge_base_t kb, const char *drl_source) {
  if (!kb) {
    set_error("Knowledge Base handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (!drl_source) {
    set_error("RFL source is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    ParsingResult result;
    auto kb_wrapper = static_cast<KnowledgeBaseWrapper *>(kb);

    // Save the native functions before rebuilding
    auto saved_native_functions = kb_wrapper->kb->get_native_functions();

    // Use the real RFL parser
    kb_wrapper->kb = build_knowledge_base(drl_source, result, "C_API_Source");

    if (!result.success) {
      std::string error_msg = "RFL compilation failed: ";
      for (const auto &err : result.errors) {
        error_msg += err.to_string() + "; ";
      }
      snprintf(last_error, sizeof(last_error), "%s", error_msg.c_str());
      return RULES_FORGE_ERROR_COMPILATION_FAILED;
    }

    // Restore the native functions
    for (auto const& [name, func] : saved_native_functions) {
      kb_wrapper->kb->register_native_function(name, func.callback, func.user_data);
    }

    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to load RFL: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

RULEFORGE_API ruleforge_status_t ruleforge_kb_load_decision_table_csv(ruleforge_knowledge_base_t kb,
                                                        const char *csv_source) {
  if (!kb) {
    set_error("Knowledge Base handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (!csv_source) {
    set_error("CSV source is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    ParsingResult result;
    auto kb_wrapper = static_cast<KnowledgeBaseWrapper *>(kb);

    // Save the native functions before rebuilding
    auto saved_native_functions = kb_wrapper->kb->get_native_functions();

    kb_wrapper->kb = build_knowledge_base_from_csv_string(csv_source, result, "C_API_CSV_Source");

    if (!result.success) {
      std::string error_msg = "Decision table compilation failed: ";
      for (const auto &err : result.errors) {
        error_msg += err.to_string() + "; ";
      }
      snprintf(last_error, sizeof(last_error), "%s", error_msg.c_str());
      return RULES_FORGE_ERROR_COMPILATION_FAILED;
    }

    // Restore the native functions
    for (auto const& [name, func] : saved_native_functions) {
      kb_wrapper->kb->register_native_function(name, func.callback, func.user_data);
    }

    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to load CSV: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

RULEFORGE_API ruleforge_status_t ruleforge_kb_destroy(ruleforge_knowledge_base_t kb) {
  if (!kb) {
    set_error("Knowledge Base handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto kb_wrapper = static_cast<KnowledgeBaseWrapper *>(kb);
    delete kb_wrapper;
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to destroy Knowledge Base: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

RULEFORGE_API ruleforge_status_t ruleforge_kb_register_native_function(
    ruleforge_knowledge_base_t kb,
    const char *function_name,
    ruleforge_native_function_t callback,
    void *user_data) {
  if (!kb) {
    set_error("Knowledge Base handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (!function_name) {
    set_error("Function name is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (!callback) {
    set_error("Callback function is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto kb_wrapper = static_cast<KnowledgeBaseWrapper *>(kb);
    kb_wrapper->kb->register_native_function(
        function_name,
        reinterpret_cast<NativeFunctionCallback>(callback),
        user_data);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to register native function: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

// Session wrapper for safe C++/C interop
struct StatefulSessionWrapper {
  std::unique_ptr<StatefulSession> session;
};

RULEFORGE_API ruleforge_status_t ruleforge_session_create(ruleforge_knowledge_base_t kb,
                                           ruleforge_stateful_session_t *out_session) {
  if (!kb) {
    set_error("Knowledge Base handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (!out_session) {
    set_error("Output Session pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto kb_wrapper = static_cast<KnowledgeBaseWrapper *>(kb);
    auto session_wrapper = new StatefulSessionWrapper();
    session_wrapper->session = kb_wrapper->kb->create_session();
    *out_session = static_cast<ruleforge_stateful_session_t>(session_wrapper);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to create session: ", e.what());
    return RULES_FORGE_ERROR_SESSION_CREATION_FAILED;
  }
}

// QueryResult wrapper for safe C++/C interop
struct QueryResultWrapper {
  std::unique_ptr<QueryResult> query_result;
};

RULEFORGE_API ruleforge_status_t ruleforge_session_add_fact_json(ruleforge_stateful_session_t session,
                                                   const char *fact_type, const char *fact_json) {
  if (!session || !fact_type || !fact_json) {
    set_error("Session handle, fact type, or fact JSON is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = static_cast<StatefulSessionWrapper *>(session);

    // Parse JSON and create fact using FactBuilder
    auto builder = FactBuilder::create(fact_type);
    jsoncons::json j = jsoncons::json::parse(fact_json);

    for (auto const &member : j.object_range()) {
      const std::string &key = member.key();
      const jsoncons::json &value = member.value();

      if (value.is_string()) {
        builder.set(key, value.as<std::string>());
      } else if (value.is_int64()) {
        builder.set(key, value.as<int64_t>());
      } else if (value.is_double()) {
        builder.set(key, value.as<double>());
      } else if (value.is_bool()) {
        builder.set(key, value.as<bool>());
      }
    }

    // Add the fact to the session
    session_wrapper->session->add_fact(std::move(builder).build());
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const jsoncons::json_exception &e) {
    set_error_fmt("JSON parsing failed: ", e.what());
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to add fact: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t ruleforge_session_fire_all_rules(ruleforge_stateful_session_t session,
                                                    int max_rules, int *out_fired_count) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = static_cast<StatefulSessionWrapper *>(session);
    int fired = session_wrapper->session->fire_all_rules(max_rules);
    if (out_fired_count) {
      *out_fired_count = fired;
    }
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to fire rules: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

RULEFORGE_API int ruleforge_session_get_fact_count(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return -1;
  }
  try {
    auto session_wrapper = static_cast<StatefulSessionWrapper *>(session);
    last_error[0] = '\0';
    return static_cast<int>(session_wrapper->session->get_fact_count());
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get fact count: ", e.what());
    return -1;
  }
}

RULEFORGE_API ruleforge_status_t ruleforge_session_query(ruleforge_stateful_session_t session,
                                           const char *query_name,
                                           ruleforge_query_result_t *out_query_result) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (!query_name) {
    set_error("Query name is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (!out_query_result) {
    set_error("Output Query Result pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = static_cast<StatefulSessionWrapper *>(session);

    // Execute query
    QueryResult query_result = session_wrapper->session->execute_query(query_name);

    // Wrap in QueryResultWrapper for safe C interop
    auto result_wrapper = new QueryResultWrapper();
    result_wrapper->query_result = std::make_unique<QueryResult>(query_result);

    *out_query_result = static_cast<ruleforge_query_result_t>(result_wrapper);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to execute query: ", e.what());
    return RULES_FORGE_ERROR_QUERY_FAILED;
  }
}

RULEFORGE_API ruleforge_status_t ruleforge_session_destroy(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = static_cast<StatefulSessionWrapper *>(session);
    delete session_wrapper;
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to destroy session: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

// Query result functions
RULEFORGE_API int ruleforge_query_result_get_size(ruleforge_query_result_t query_result) {
  if (!query_result) {
    set_error("Query Result handle is NULL");
    return -1;
  }
  try {
    auto result_wrapper = static_cast<QueryResultWrapper *>(query_result);
    last_error[0] = '\0';
    return static_cast<int>(result_wrapper->query_result->size());
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get query result size: ", e.what());
    return -1;
  }
}

RULEFORGE_API ruleforge_status_t ruleforge_query_result_get_fact_at_index(ruleforge_query_result_t query_result,
                                                            int row_index, const char *binding_name,
                                                            ruleforge_fact_t *out_fact) {
  if (!query_result || !binding_name || !out_fact) {
    set_error("Query Result handle, binding name, or output Fact pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (row_index < 0) {
    set_error("Row index is negative");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto result_wrapper = static_cast<QueryResultWrapper *>(query_result);

    if (row_index >= static_cast<int>(result_wrapper->query_result->size())) {
      set_error("Row index out of bounds");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    // Use iterator to access row at index
    auto it = result_wrapper->query_result->begin();
    std::advance(it, row_index);

    // Get the fact from the query result row
    auto fact_opt = (*it).get(std::string(binding_name));
    if (!fact_opt) {
      set_error("Binding name not found");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    // Return the raw pointer - ownership remains with QueryResult
    *out_fact = static_cast<ruleforge_fact_t>(fact_opt->get());
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get fact: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

RULEFORGE_API ruleforge_status_t ruleforge_query_result_destroy(ruleforge_query_result_t query_result) {
  if (!query_result) {
    set_error("Query Result handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto result_wrapper = static_cast<QueryResultWrapper *>(query_result);
    delete result_wrapper;
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to destroy query result: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

// Fact functions
RULEFORGE_API ruleforge_status_t ruleforge_fact_get_field_as_string(ruleforge_fact_t fact, const char *field_name,
                                                      char *buffer, size_t buffer_size,
                                                      size_t *out_actual_length) {
  if (!fact || !field_name || !buffer || !out_actual_length) {
    set_error("Fact handle, field name, buffer, or actual length pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto f = static_cast<const Fact *>(fact);
    auto field_opt = f->get_field(field_name);
    if (!field_opt) {
      set_error("Field not found");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto *field_ptr = std::get_if<std::string>(&(*field_opt));
    if (!field_ptr) {
      set_error("Field is not a string");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    const auto &field = *field_ptr;

    *out_actual_length = field.length();
    if (buffer_size <= field.length()) {
      set_error("Buffer too small");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    memcpy(buffer, field.c_str(), field.length() + 1);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get string field: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

RULEFORGE_API ruleforge_status_t ruleforge_fact_get_field_as_double(ruleforge_fact_t fact, const char *field_name,
                                                      double *out_value) {
  if (!fact || !field_name || !out_value) {
    set_error("Fact handle, field name, or output value pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto f = static_cast<const Fact *>(fact);
    auto field_opt = f->get_field(field_name);
    if (!field_opt) {
      set_error("Field not found");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto *double_ptr = std::get_if<double>(&(*field_opt));
    if (double_ptr) {
      *out_value = *double_ptr;
    } else {
      auto *int_ptr = std::get_if<int64_t>(&(*field_opt));
      if (int_ptr) {
        *out_value = static_cast<double>(*int_ptr);
      } else {
        set_error("Field is not a numeric type");
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
      }
    }
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get double field: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

RULEFORGE_API ruleforge_status_t ruleforge_fact_get_field_as_int(ruleforge_fact_t fact, const char *field_name,
                                                   int64_t *out_value) {
  if (!fact || !field_name || !out_value) {
    set_error("Fact handle, field name, or output value pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto f = static_cast<const Fact *>(fact);
    auto field_opt = f->get_field(field_name);
    if (!field_opt) {
      set_error("Field not found");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto *field_ptr = std::get_if<int64_t>(&(*field_opt));
    if (!field_ptr) {
      set_error("Field is not an int");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    *out_value = *field_ptr;
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get int field: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

RULEFORGE_API ruleforge_status_t ruleforge_fact_get_field_as_bool(ruleforge_fact_t fact, const char *field_name,
                                                    int *out_value) {
  if (!fact || !field_name || !out_value) {
    set_error("Fact handle, field name, or output value pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto f = static_cast<const Fact *>(fact);
    auto field_opt = f->get_field(field_name);
    if (!field_opt) {
      set_error("Field not found");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto *field_ptr = std::get_if<int64_t>(&(*field_opt));
    if (!field_ptr) {
      set_error("Field is not a bool");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    *out_value = (*field_ptr != 0) ? 1 : 0;
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get bool field: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

// --- Session Observability Functions ---

RULEFORGE_API ruleforge_status_t ruleforge_session_enable_tracing(ruleforge_stateful_session_t session,
                                                    int enabled) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = static_cast<StatefulSessionWrapper *>(session);
    session_wrapper->session->enable_tracing(enabled != 0);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to enable tracing: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

RULEFORGE_API ruleforge_status_t ruleforge_session_get_execution_trace(ruleforge_stateful_session_t session,
                                                         int include_network, char *buffer,
                                                         size_t buffer_size,
                                                         size_t *out_actual_length) {
  if (!session || !buffer || !out_actual_length) {
    set_error("Session handle, buffer, or actual length pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = static_cast<StatefulSessionWrapper *>(session);
    std::string trace = session_wrapper->session->get_execution_trace(include_network != 0);

    *out_actual_length = trace.length();
    if (buffer_size <= trace.length()) {
      set_error("Buffer too small for trace");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    memcpy(buffer, trace.c_str(), trace.length() + 1);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get execution trace: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

RULEFORGE_API ruleforge_status_t
ruleforge_session_get_rule_performance_summary(ruleforge_stateful_session_t session, char *buffer,
                                               size_t buffer_size, size_t *out_actual_length) {
  if (!session || !buffer || !out_actual_length) {
    set_error("Session handle, buffer, or actual length pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = static_cast<StatefulSessionWrapper *>(session);
    std::string summary = session_wrapper->session->get_rule_performance_summary();

    *out_actual_length = summary.length();
    if (buffer_size <= summary.length()) {
      set_error("Buffer too small for summary");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    memcpy(buffer, summary.c_str(), summary.length() + 1);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get rule performance summary: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

RULEFORGE_API ruleforge_status_t ruleforge_session_clear_trace(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = static_cast<StatefulSessionWrapper *>(session);
    session_wrapper->session->get_tracer().clear_trace();
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to clear trace: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

// --- Session Memory Statistics Functions ---

RULEFORGE_API int64_t ruleforge_session_get_memory_used(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return -1;
  }
  try {
    auto session_wrapper = static_cast<StatefulSessionWrapper *>(session);
    last_error[0] = '\0';
    return static_cast<int64_t>(session_wrapper->session->get_arena().memory_used());
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get memory used: ", e.what());
    return -1;
  }
}

RULEFORGE_API int64_t ruleforge_session_get_memory_peak(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return -1;
  }
  try {
    auto session_wrapper = static_cast<StatefulSessionWrapper *>(session);
    last_error[0] = '\0';
    return static_cast<int64_t>(session_wrapper->session->get_arena().memory_peak());
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get memory peak: ", e.what());
    return -1;
  }
}

RULEFORGE_API ruleforge_status_t ruleforge_session_get_memory_stats(ruleforge_stateful_session_t session,
                                                      char *buffer, size_t buffer_size,
                                                      size_t *out_actual_length) {
  if (!session || !buffer || !out_actual_length) {
    set_error("Session handle, buffer, or actual length pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = static_cast<StatefulSessionWrapper *>(session);
    std::string stats = session_wrapper->session->get_memory_stats();

    *out_actual_length = stats.length();
    if (buffer_size <= stats.length()) {
      set_error("Buffer too small for stats");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    memcpy(buffer, stats.c_str(), stats.length() + 1);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get memory stats: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

} // extern "C"
