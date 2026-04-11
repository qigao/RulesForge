// Include the header file to get type definitions
#include "rule_forge.h"

// Include C string handling
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <fmt.h>
#include <fstream>
#include <string>
#include <vector>

// Include C++ backend
#include "core/errors.hpp"
#include "core/exceptions.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/query_result.hpp"
#include "engine/stateful_session.hpp"
#include "rfl_parser.hpp"

// Include JSON parsing
#include <jsoncons/json.hpp>
#include <turbo_parser.h>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

// Forward declarations for TurboScript/exprtk interoperability
#include "exprtk_module.h"
#include "ts_plugin.h"
#include <jsoncons/json.hpp>

using namespace rulesforge;

namespace {
thread_local char last_error[1024] = "";

#if defined(_WIN32)
static void *open_library(const char *path) {
  HMODULE h = LoadLibraryA(path);
  return reinterpret_cast<void *>(h);
}

static void *load_symbol(void *handle, const char *symbol_name) {
  return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol_name));
}

static void close_library(void *handle) {
  if (handle) {
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
  }
}
#else
static void *open_library(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }

static void *load_symbol(void *handle, const char *symbol_name) {
  return dlsym(handle, symbol_name);
}

static void close_library(void *handle) {
  if (handle) {
    dlclose(handle);
  }
}
#endif

static void set_error(const char *msg) { fmt(last_error, sizeof(last_error), "{}", msg); }

static void set_error_fmt(const char *prefix, const char *detail) {
  fmt(last_error, sizeof(last_error), "{}{}", prefix, detail);
}

static ruleforge_status_t map_session_inconsistent(SessionInconsistentException const &e) {
  set_error(e.what());
  return RULES_FORGE_ERROR_SESSION_INCONSISTENT;
}

static std::string trim_ascii(std::string const &s) {
  size_t begin = 0;
  while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
    ++begin;
  }
  size_t end = s.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(begin, end - begin);
}

static bool try_parse_int64_str(std::string const &text, int64_t &out) {
  if (text.empty())
    return false;
  auto *begin = text.data();
  auto *end = begin + text.size();
  auto [ptr, ec] = std::from_chars(begin, end, out);
  return ec == std::errc() && ptr == end;
}

static bool try_parse_double_str(std::string const &text, double &out) {
  if (text.empty())
    return false;
  char *end_ptr = nullptr;
  out = std::strtod(text.c_str(), &end_ptr);
  return end_ptr == text.c_str() + text.size();
}

static void set_fact_field_from_csv(Fact &fact, std::string const &field_name,
                                    std::string const &raw_value) {
  std::string value = trim_ascii(raw_value);
  if (value.empty()) {
    return;
  }
  if (value == "true" || value == "TRUE") {
    fact.fields[field_name] = static_cast<int64_t>(1);
    return;
  }
  if (value == "false" || value == "FALSE") {
    fact.fields[field_name] = static_cast<int64_t>(0);
    return;
  }
  if (value == "null" || value == "NULL") {
    fact.fields[field_name] = NilValue{};
    return;
  }

  int64_t int_value = 0;
  if (try_parse_int64_str(value, int_value)) {
    fact.fields[field_name] = int_value;
    return;
  }

  double double_value = 0.0;
  if (try_parse_double_str(value, double_value)) {
    fact.fields[field_name] = double_value;
    return;
  }

  fact.fields[field_name] = value;
}

static ruleforge_status_t load_csv_facts_into_session(StatefulSession *session_ptr,
                                                      const char *fact_type, const char *csv_source,
                                                      int *out_loaded_count,
                                                      ruleforge_fact_t **out_facts) {
  if (!session_ptr || !fact_type || !csv_source) {
    set_error("Session handle, fact type, or CSV source is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  if (out_facts) {
    *out_facts = nullptr;
  }
  if (out_loaded_count)
    *out_loaded_count = 0;

  turbo_csv_doc_t *doc = nullptr;
  int rc =
      turbo_parse_csv(reinterpret_cast<const uint8_t *>(csv_source), std::strlen(csv_source), &doc);
  if (rc != 0 || !doc) {
    if (doc)
      turbo_free_csv(&doc);
    set_error("CSV parsing failed");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  size_t row_count = turbo_csv_row_count(doc);
  if (row_count == 0) {
    turbo_free_csv(&doc);
    return RULES_FORGE_OK;
  }

  std::vector<std::string> headers;
  for (size_t c = 0;; ++c) {
    char const *cell = turbo_csv_get(doc, 0, c);
    if (!cell)
      break;
    headers.push_back(trim_ascii(cell));
  }

  if (headers.empty()) {
    turbo_free_csv(&doc);
    set_error("CSV header row is empty");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  int loaded = 0;
  std::vector<ruleforge_fact_t> inserted_facts;
  for (size_t r = 1; r < row_count; ++r) {
    Fact *fact = session_ptr->create_fact(fact_type);
    bool has_field = false;

    for (size_t c = 0; c < headers.size(); ++c) {
      std::string const &field_name = headers[c];
      if (field_name.empty())
        continue;

      char const *cell = turbo_csv_get(doc, r, c);
      if (!cell)
        continue;

      set_fact_field_from_csv(*fact, field_name, cell);
      has_field = true;
    }

    if (!has_field) {
      continue;
    }

    session_ptr->add_fact(fact);
    if (out_facts) {
      inserted_facts.push_back(reinterpret_cast<ruleforge_fact_t>(fact));
    }
    ++loaded;
  }

  if (out_facts && !inserted_facts.empty()) {
    auto *fact_array = static_cast<ruleforge_fact_t *>(
        std::calloc(inserted_facts.size(), sizeof(ruleforge_fact_t)));
    if (!fact_array) {
      turbo_free_csv(&doc);
      set_error("Failed to allocate fact handle array");
      return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
    }
    std::memcpy(fact_array, inserted_facts.data(),
                inserted_facts.size() * sizeof(ruleforge_fact_t));
    *out_facts = fact_array;
  }

  if (out_loaded_count)
    *out_loaded_count = loaded;
  turbo_free_csv(&doc);
  return RULES_FORGE_OK;
}
} // namespace

ruleforge_status_t ruleforge_init() {
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

ruleforge_status_t ruleforge_cleanup() {
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

const char *ruleforge_get_last_error_message() { return last_error; }

const char *ruleforge_get_version() { return RULEFORGE_VERSION_STRING; }

// Knowledge Base functions
// Helper function for crossing C++/pure C boundaries safely
struct KnowledgeBaseWrapper {
  std::shared_ptr<KnowledgeBase> kb;
  std::vector<void *> plugin_handles;
};

ruleforge_status_t ruleforge_kb_create(ruleforge_knowledge_base_t *out_kb) {
  if (!out_kb) {
    set_error("Output Knowledge Base pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    // Create empty knowledge base
    parser_state empty_state;
    auto kb_wrapper = new KnowledgeBaseWrapper();
    kb_wrapper->kb = KnowledgeBase::create(std::move(empty_state));
    *out_kb = reinterpret_cast<ruleforge_knowledge_base_t>(kb_wrapper);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to create Knowledge Base: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_kb_load_drl(ruleforge_knowledge_base_t kb, const char *drl_source) {
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
    auto kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);

    // Save the native functions before rebuilding
    auto saved_native_functions = kb_wrapper->kb->get_native_functions();

    // Build first; do not overwrite old KB until success.
    auto compiled_kb = build_knowledge_base(drl_source, result, "C_API_Source");

    if (!result.success || !compiled_kb) {
      std::string error_msg = "RFL compilation failed: ";
      for (const auto &err : result.errors) {
        error_msg += err.to_string() + "; ";
      }
      if (result.errors.empty()) {
        error_msg += "Unknown compilation error.";
      }
      fmt(last_error, sizeof(last_error), "{}", error_msg);
      return RULES_FORGE_ERROR_COMPILATION_FAILED;
    }

    // Restore the native functions
    for (auto const &[name, func] : saved_native_functions) {
      compiled_kb->register_native_function(name, func.callback, func.user_data);
    }
    kb_wrapper->kb = std::move(compiled_kb);

    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to load RFL: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_kb_load_drl_file(ruleforge_knowledge_base_t kb, const char *file_path,
                                              const char **base_dirs, int base_dir_count) {
  if (!kb || !file_path) {
    set_error("Knowledge Base handle or file_path is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);
    auto saved_native_functions = kb_wrapper->kb->get_native_functions();

    std::vector<std::string> dirs;
    for (int i = 0; i < base_dir_count; i++) {
      if (base_dirs[i])
        dirs.emplace_back(base_dirs[i]);
    }

    ParsingResult result;
    std::shared_ptr<KnowledgeBase> compiled_kb;
    if (dirs.empty()) {
      compiled_kb = build_knowledge_base(result, std::string(file_path));
    } else {
      compiled_kb = build_knowledge_base(std::string(file_path), dirs, result);
    }

    if (!result.success || !compiled_kb) {
      std::string error_msg = "RFL compilation failed: ";
      for (const auto &err : result.errors)
        error_msg += err.to_string() + "; ";
      if (result.errors.empty())
        error_msg += "Unknown compilation error.";
      fmt(last_error, sizeof(last_error), "{}", error_msg);
      return RULES_FORGE_ERROR_COMPILATION_FAILED;
    }

    for (auto const &[name, func] : saved_native_functions)
      compiled_kb->register_native_function(name, func.callback, func.user_data);
    kb_wrapper->kb = std::move(compiled_kb);

    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to load RFL file: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_kb_load_drl_files(ruleforge_knowledge_base_t kb,
                                               const char **file_paths, int file_count,
                                               const char **base_dirs, int base_dir_count) {
  if (!kb || !file_paths || file_count <= 0) {
    set_error("Invalid arguments");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);
    auto saved_native_functions = kb_wrapper->kb->get_native_functions();

    std::vector<std::string> files;
    for (int i = 0; i < file_count; i++) {
      if (file_paths[i])
        files.emplace_back(file_paths[i]);
    }

    std::vector<std::string> dirs;
    for (int i = 0; i < base_dir_count; i++) {
      if (base_dirs[i])
        dirs.emplace_back(base_dirs[i]);
    }

    ParsingResult result;
    auto compiled_kb = build_knowledge_base(files, dirs, result);

    if (!result.success || !compiled_kb) {
      std::string error_msg = "RFL compilation failed: ";
      for (const auto &err : result.errors)
        error_msg += err.to_string() + "; ";
      if (result.errors.empty())
        error_msg += "Unknown compilation error.";
      fmt(last_error, sizeof(last_error), "{}", error_msg);
      return RULES_FORGE_ERROR_COMPILATION_FAILED;
    }

    for (auto const &[name, func] : saved_native_functions)
      compiled_kb->register_native_function(name, func.callback, func.user_data);
    kb_wrapper->kb = std::move(compiled_kb);

    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to load RFL files: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_kb_load_decision_table_csv(ruleforge_knowledge_base_t kb,
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
    auto kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);

    // Save the native functions before rebuilding
    auto saved_native_functions = kb_wrapper->kb->get_native_functions();

    // Build first; do not overwrite old KB until success.
    auto compiled_kb = build_knowledge_base_from_csv_string(csv_source, result, "C_API_CSV_Source");

    if (!result.success || !compiled_kb) {
      std::string error_msg = "Decision table compilation failed: ";
      for (const auto &err : result.errors) {
        error_msg += err.to_string() + "; ";
      }
      if (result.errors.empty()) {
        error_msg += "Unknown compilation error.";
      }
      fmt(last_error, sizeof(last_error), "{}", error_msg);
      return RULES_FORGE_ERROR_COMPILATION_FAILED;
    }

    // Restore the native functions
    for (auto const &[name, func] : saved_native_functions) {
      compiled_kb->register_native_function(name, func.callback, func.user_data);
    }
    kb_wrapper->kb = std::move(compiled_kb);

    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to load CSV: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_kb_destroy(ruleforge_knowledge_base_t kb) {
  if (!kb) {
    set_error("Knowledge Base handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);
    for (void *handle : kb_wrapper->plugin_handles) {
      close_library(handle);
    }
    kb_wrapper->plugin_handles.clear();
    delete kb_wrapper;
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to destroy Knowledge Base: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_kb_register_native_function(ruleforge_knowledge_base_t kb,
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
    auto kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);
    kb_wrapper->kb->register_native_function(
        function_name, reinterpret_cast<NativeFunctionCallback>(callback), user_data);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to register native function: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_kb_load_native_function_table(ruleforge_knowledge_base_t kb,
                                                           const char *library_path,
                                                           const char *symbol_name) {
  if (!kb) {
    set_error("Knowledge Base handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (!library_path) {
    set_error("Library path is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  auto kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);
  void *handle = open_library(library_path);
  if (!handle) {
    set_error("Failed to load plugin library");
    return RULES_FORGE_ERROR_GENERIC;
  }

  char const *symbol =
      (symbol_name && symbol_name[0] != '\0') ? symbol_name : "ruleforge_get_function_table";
  auto get_table = reinterpret_cast<ruleforge_get_function_table_t>(load_symbol(handle, symbol));
  if (!get_table) {
    close_library(handle);
    set_error("Failed to resolve plugin function table symbol");
    return RULES_FORGE_ERROR_GENERIC;
  }

  ruleforge_plugin_function_table_t table{};
  ruleforge_status_t status = get_table(&table);
  if (status != RULES_FORGE_OK) {
    close_library(handle);
    set_error("Plugin function table callback failed");
    return status;
  }
  if (table.abi_version != RULEFORGE_PLUGIN_ABI_V1) {
    close_library(handle);
    set_error("Plugin ABI version mismatch");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (table.function_count > 0 && table.functions == nullptr) {
    close_library(handle);
    set_error("Plugin function table is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  try {
    for (uint32_t i = 0; i < table.function_count; ++i) {
      auto const &entry = table.functions[i];
      if (!entry.name || !entry.callback) {
        close_library(handle);
        set_error("Plugin entry has NULL name or callback");
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
      }
      kb_wrapper->kb->register_native_function(
          entry.name, reinterpret_cast<NativeFunctionCallback>(entry.callback), entry.user_data);
    }
    kb_wrapper->plugin_handles.push_back(handle);
  } catch (const std::exception &e) {
    close_library(handle);
    set_error_fmt("Failed to register plugin functions: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }

  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

namespace {
// Bridge to call exprtk function from ruleforge native callback
struct TsFuncBridge {
  exprtk_builtin_fn fn;
  void *env;
};

int ts_func_callback_bridge(void *ctx, int argc, const char **argv, char **out_result) {
  auto *bridge = static_cast<TsFuncBridge *>(ctx);
  if (!bridge || !bridge->fn)
    return RULES_FORGE_ERROR_GENERIC;

  std::vector<exprtk_value_t> expr_args;
  expr_args.reserve(argc);

  for (int i = 0; i < argc; ++i) {
    try {
      auto j = jsoncons::json::parse(argv[i]);
      if (j.is_double() || j.is_int64()) {
        expr_args.push_back(exprtk_val_num(j.as<double>()));
      } else if (j.is_string()) {
        std::string s = j.as<std::string>();
        char *internal_s = strdup(s.c_str());
        tstr_v tv;
        tv.data = internal_s;
        tv.len = s.length();
        expr_args.push_back(exprtk_val_str(tv));
      } else {
        expr_args.push_back(exprtk_val_num(0.0));
      }
    } catch (...) {
      expr_args.push_back(exprtk_val_num(0.0));
    }
  }

  // Call the function
  exprtk_value_t res =
      bridge->fn(expr_args.size(), expr_args.data(), (exprtk_env_t *)bridge->env, nullptr);

  // Convert back to JSON
  jsoncons::json j_res;
  if (res.type == EXPRTK_VAL_NUMBER)
    j_res = res.data.number;
  else if (res.type == EXPRTK_VAL_STRING) {
    j_res = std::string(res.data.string.data, res.data.string.len);
  } else {
    j_res = 0.0;
  }

  *out_result = strdup(j_res.to_string().c_str());

  for (auto &arg : expr_args) {
    if (arg.type == EXPRTK_VAL_STRING)
      free((void *)arg.data.string.data);
  }

  return RULES_FORGE_OK;
}

struct RuleForgeExprtkEnv {
  KnowledgeBase *kb;
  std::vector<TsFuncBridge *> bridges;
};
} // namespace

extern "C" void exprtk_env_add_module(exprtk_env_t *env, const exprtk_module_t *mod) {
  if (!env || !mod)
    return;
  auto *rfe = reinterpret_cast<RuleForgeExprtkEnv *>(env);

  for (size_t i = 0; i < mod->count; ++i) {
    auto &entry = mod->entries[i];
    auto *bridge = new TsFuncBridge{entry.fn, env};
    rfe->bridges.push_back(bridge);
    rfe->kb->register_native_function(entry.name, ts_func_callback_bridge, bridge);
  }
}

ruleforge_status_t ruleforge_kb_load_ts_plugin(ruleforge_knowledge_base_t kb,
                                               const char *library_path) {
  if (!kb || !library_path)
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  auto kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);

  void *handle = open_library(library_path);
  if (!handle) {
    set_error("Failed to load TurboScript plugin library");
    return RULES_FORGE_ERROR_GENERIC;
  }

  auto ts_api_create = reinterpret_cast<ts_api_create_fn>(load_symbol(handle, "ts_api_create"));
  if (!ts_api_create) {
    close_library(handle);
    set_error("Not a TurboScript plugin (ts_api_create missing)");
    return RULES_FORGE_ERROR_GENERIC;
  }

  const ts_plugin_t *plugin = ts_api_create();
  if (!plugin) {
    close_library(handle);
    set_error("ts_api_create returned NULL");
    return RULES_FORGE_ERROR_GENERIC;
  }

  auto *rfe = new RuleForgeExprtkEnv{kb_wrapper->kb.get()};
  void *instance = plugin->load(rfe, nullptr);
  if (!instance) {
    // Plugin load failed or just stateless
  }

  kb_wrapper->plugin_handles.push_back(handle);
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

// Session wrapper for safe C++/C interop
struct StatefulSessionWrapper {
  std::unique_ptr<StatefulSession> session;
};

ruleforge_status_t ruleforge_session_create(ruleforge_knowledge_base_t kb,
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
    auto kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);
    if (!kb_wrapper->kb) {
      set_error("Knowledge Base is not initialized");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    auto session_wrapper = new StatefulSessionWrapper();
    session_wrapper->session = kb_wrapper->kb->create_session();
    if (!session_wrapper->session) {
      delete session_wrapper;
      set_error("Failed to create session from Knowledge Base");
      return RULES_FORGE_ERROR_SESSION_CREATION_FAILED;
    }
    *out_session = reinterpret_cast<ruleforge_stateful_session_t>(session_wrapper);
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

static ruleforge_status_t ruleforge_session_add_fact_json_impl(ruleforge_stateful_session_t session,
                                                               const char *fact_type,
                                                               const char *fact_json,
                                                               ruleforge_fact_t *out_fact) {
  if (!session || !fact_type || !fact_json) {
    set_error("Session handle, fact type, or fact JSON is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (out_fact) {
    *out_fact = nullptr;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);

    // Parse JSON and create fact in session-owned arena.
    Fact *fact = session_wrapper->session->create_fact(fact_type);
    jsoncons::json j = jsoncons::json::parse(fact_json);

    for (auto const &member : j.object_range()) {
      const std::string &key = member.key();
      const jsoncons::json &value = member.value();

      if (value.is_string()) {
        fact->fields[key] = value.as<std::string>();
      } else if (value.is_int64()) {
        fact->fields[key] = value.as<int64_t>();
      } else if (value.is_double()) {
        fact->fields[key] = value.as<double>();
      } else if (value.is_bool()) {
        fact->fields[key] = static_cast<int64_t>(value.as<bool>() ? 1 : 0);
      }
    }

    // Add the fact to the session
    session_wrapper->session->add_fact(fact);
    if (out_fact) {
      *out_fact = reinterpret_cast<ruleforge_fact_t>(fact);
    }
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const jsoncons::json_exception &e) {
    set_error_fmt("JSON parsing failed: ", e.what());
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (const std::exception &e) {
    set_error_fmt("Failed to add fact: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

static ruleforge_status_t
ruleforge_session_add_fact_binary_impl(ruleforge_stateful_session_t session, const char *fact_type,
                                       const uint8_t *fact_data, size_t fact_len,
                                       ruleforge_fact_t *out_fact) {
  if (!session || !fact_type || !fact_data || fact_len == 0) {
    set_error("Session handle, fact type, or binary fact payload is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (out_fact) {
    *out_fact = nullptr;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    Fact *fact = session_wrapper->session->add_fact_from_binary(fact_type, fact_data, fact_len);
    if (!fact) {
      set_error("Binary fact parsing returned NULL");
      return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
    }
    if (out_fact) {
      *out_fact = reinterpret_cast<ruleforge_fact_t>(fact);
    }
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (const std::exception &e) {
    set_error_fmt("Failed to add binary fact: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t ruleforge_session_add_fact_json(ruleforge_stateful_session_t session,
                                                   const char *fact_type, const char *fact_json) {
  return ruleforge_session_add_fact_json_impl(session, fact_type, fact_json, nullptr);
}

ruleforge_status_t ruleforge_session_add_fact_json_ex(ruleforge_stateful_session_t session,
                                                      const char *fact_type, const char *fact_json,
                                                      ruleforge_fact_t *out_fact) {
  if (!out_fact) {
    set_error("Output Fact pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  return ruleforge_session_add_fact_json_impl(session, fact_type, fact_json, out_fact);
}

ruleforge_status_t ruleforge_session_add_fact_binary(ruleforge_stateful_session_t session,
                                                     const char *fact_type,
                                                     const uint8_t *fact_data, size_t fact_len) {
  return ruleforge_session_add_fact_binary_impl(session, fact_type, fact_data, fact_len, nullptr);
}

ruleforge_status_t ruleforge_session_add_fact_binary_ex(ruleforge_stateful_session_t session,
                                                        const char *fact_type,
                                                        const uint8_t *fact_data, size_t fact_len,
                                                        ruleforge_fact_t *out_fact) {
  if (!out_fact) {
    set_error("Output Fact pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  return ruleforge_session_add_fact_binary_impl(session, fact_type, fact_data, fact_len, out_fact);
}

ruleforge_status_t ruleforge_session_add_facts_csv(ruleforge_stateful_session_t session,
                                                   const char *fact_type, const char *csv_source,
                                                   int *out_loaded_count) {
  if (!session || !fact_type || !csv_source) {
    set_error("Session handle, fact type, or CSV source is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    auto status = load_csv_facts_into_session(session_wrapper->session.get(), fact_type, csv_source,
                                              out_loaded_count, nullptr);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (const std::exception &e) {
    set_error_fmt("Failed to add CSV facts: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t ruleforge_session_add_facts_csv_ex(ruleforge_stateful_session_t session,
                                                      const char *fact_type, const char *csv_source,
                                                      ruleforge_fact_t **out_facts,
                                                      int *out_loaded_count) {
  if (!out_facts) {
    set_error("Output Fact array pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  *out_facts = nullptr;

  if (!session || !fact_type || !csv_source) {
    set_error("Session handle, fact type, or CSV source is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    auto status = load_csv_facts_into_session(session_wrapper->session.get(), fact_type, csv_source,
                                              out_loaded_count, out_facts);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (const std::exception &e) {
    set_error_fmt("Failed to add CSV facts: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t ruleforge_session_add_facts_csv_file(ruleforge_stateful_session_t session,
                                                        const char *fact_type,
                                                        const char *csv_file_path,
                                                        int *out_loaded_count) {
  if (!session || !fact_type || !csv_file_path) {
    set_error("Session handle, fact type, or CSV file path is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  try {
    std::ifstream file(csv_file_path, std::ios::binary);
    if (!file) {
      set_error("Cannot open CSV file");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    std::string csv_content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    auto status = load_csv_facts_into_session(session_wrapper->session.get(), fact_type,
                                              csv_content.c_str(), out_loaded_count, nullptr);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (const std::exception &e) {
    set_error_fmt("Failed to add CSV facts from file: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t ruleforge_session_add_facts_csv_file_ex(ruleforge_stateful_session_t session,
                                                           const char *fact_type,
                                                           const char *csv_file_path,
                                                           ruleforge_fact_t **out_facts,
                                                           int *out_loaded_count) {
  if (!out_facts) {
    set_error("Output Fact array pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  *out_facts = nullptr;

  if (!session || !fact_type || !csv_file_path) {
    set_error("Session handle, fact type, or CSV file path is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  try {
    std::ifstream file(csv_file_path, std::ios::binary);
    if (!file) {
      set_error("Cannot open CSV file");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    std::string csv_content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    auto status = load_csv_facts_into_session(session_wrapper->session.get(), fact_type,
                                              csv_content.c_str(), out_loaded_count, out_facts);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (const std::exception &e) {
    set_error_fmt("Failed to add CSV facts from file: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

void ruleforge_fact_array_free(ruleforge_fact_t *facts) { std::free(facts); }

ruleforge_status_t ruleforge_session_fire_all_rules(ruleforge_stateful_session_t session,
                                                    int max_rules, int *out_fired_count) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    if (!session_wrapper->session->is_consistent()) {
      set_error("Session is inconsistent due to a previous failed RHS transaction. Call reset() "
                "before firing again.");
      return RULES_FORGE_ERROR_SESSION_INCONSISTENT;
    }
    int fired = session_wrapper->session->fire_all_rules(max_rules);
    if (out_fired_count) {
      *out_fired_count = fired;
    }
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (const std::exception &e) {
    set_error_fmt("Failed to fire rules: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_session_reset(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    session_wrapper->session->reset();
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to reset session: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

int ruleforge_session_get_fact_count(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return -1;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    last_error[0] = '\0';
    return static_cast<int>(session_wrapper->session->get_fact_count());
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get fact count: ", e.what());
    return -1;
  }
}

ruleforge_status_t ruleforge_session_query(ruleforge_stateful_session_t session,
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
  *out_query_result = nullptr;
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);

    // Execute query
    QueryResult query_result = session_wrapper->session->execute_query(query_name);
    if (!query_result.success()) {
      set_error(query_result.error_message().c_str());
      return RULES_FORGE_ERROR_QUERY_FAILED;
    }

    // Wrap in QueryResultWrapper for safe C interop
    auto result_wrapper = new QueryResultWrapper();
    result_wrapper->query_result = std::make_unique<QueryResult>(query_result);

    *out_query_result = reinterpret_cast<ruleforge_query_result_t>(result_wrapper);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to execute query: ", e.what());
    return RULES_FORGE_ERROR_QUERY_FAILED;
  }
}

ruleforge_status_t ruleforge_session_destroy(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    delete session_wrapper;
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to destroy session: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_session_set_validation_mode(ruleforge_stateful_session_t session,
                                                         ruleforge_validation_mode_t mode) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  ValidationMode cpp_mode = ValidationMode::None;
  switch (mode) {
  case RULES_FORGE_VALIDATION_NONE:
    cpp_mode = ValidationMode::None;
    break;
  case RULES_FORGE_VALIDATION_WARN:
    cpp_mode = ValidationMode::Warn;
    break;
  case RULES_FORGE_VALIDATION_STRICT:
    cpp_mode = ValidationMode::Strict;
    break;
  default:
    set_error("Validation mode is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    session_wrapper->session->set_validation_mode(cpp_mode);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (std::exception const &e) {
    set_error_fmt("Failed to set validation mode: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

// Query result functions
int ruleforge_query_result_get_size(ruleforge_query_result_t query_result) {
  if (!query_result) {
    set_error("Query Result handle is NULL");
    return -1;
  }
  try {
    auto result_wrapper = reinterpret_cast<QueryResultWrapper *>(query_result);
    last_error[0] = '\0';
    return static_cast<int>(result_wrapper->query_result->size());
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get query result size: ", e.what());
    return -1;
  }
}

ruleforge_status_t ruleforge_query_result_get_fact_at_index(ruleforge_query_result_t query_result,
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
    auto result_wrapper = reinterpret_cast<QueryResultWrapper *>(query_result);

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
    *out_fact = reinterpret_cast<ruleforge_fact_t>(*fact_opt);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get fact: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_query_result_destroy(ruleforge_query_result_t query_result) {
  if (!query_result) {
    set_error("Query Result handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto result_wrapper = reinterpret_cast<QueryResultWrapper *>(query_result);
    delete result_wrapper;
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to destroy query result: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

// Fact functions
ruleforge_status_t ruleforge_fact_get_field_as_string(ruleforge_fact_t fact, const char *field_name,
                                                      char *buffer, size_t buffer_size,
                                                      size_t *out_actual_length) {
  if (!fact || !field_name || !buffer || !out_actual_length) {
    set_error("Fact handle, field name, buffer, or actual length pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto f = reinterpret_cast<const Fact *>(fact);
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
    if (buffer_size < field.length() + 1) {
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

ruleforge_status_t ruleforge_fact_get_field_as_double(ruleforge_fact_t fact, const char *field_name,
                                                      double *out_value) {
  if (!fact || !field_name || !out_value) {
    set_error("Fact handle, field name, or output value pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto f = reinterpret_cast<const Fact *>(fact);
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

ruleforge_status_t ruleforge_fact_get_field_as_int(ruleforge_fact_t fact, const char *field_name,
                                                   int64_t *out_value) {
  if (!fact || !field_name || !out_value) {
    set_error("Fact handle, field name, or output value pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto f = reinterpret_cast<const Fact *>(fact);
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

ruleforge_status_t ruleforge_fact_get_field_as_bool(ruleforge_fact_t fact, const char *field_name,
                                                    int *out_value) {
  if (!fact || !field_name || !out_value) {
    set_error("Fact handle, field name, or output value pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto f = reinterpret_cast<const Fact *>(fact);
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

ruleforge_status_t ruleforge_session_enable_tracing(ruleforge_stateful_session_t session,
                                                    int enabled) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    session_wrapper->session->enable_tracing(enabled != 0);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (const std::exception &e) {
    set_error_fmt("Failed to enable tracing: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_session_get_execution_trace(ruleforge_stateful_session_t session,
                                                         int include_network, char *buffer,
                                                         size_t buffer_size,
                                                         size_t *out_actual_length) {
  if (!session || !buffer || !out_actual_length) {
    set_error("Session handle, buffer, or actual length pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    std::string trace = session_wrapper->session->get_execution_trace(include_network != 0);

    *out_actual_length = trace.length();
    if (buffer_size < trace.length() + 1) {
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

ruleforge_status_t
ruleforge_session_get_rule_performance_summary(ruleforge_stateful_session_t session, char *buffer,
                                               size_t buffer_size, size_t *out_actual_length) {
  if (!session || !buffer || !out_actual_length) {
    set_error("Session handle, buffer, or actual length pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    std::string summary = session_wrapper->session->get_rule_performance_summary();

    *out_actual_length = summary.length();
    if (buffer_size < summary.length() + 1) {
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

ruleforge_status_t ruleforge_session_clear_trace(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    session_wrapper->session->get_tracer().clear_trace();
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to clear trace: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

// --- Session Memory Statistics Functions ---

int64_t ruleforge_session_get_memory_used(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return -1;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    last_error[0] = '\0';
    return static_cast<int64_t>(session_wrapper->session->get_arena().memory_used());
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get memory used: ", e.what());
    return -1;
  }
}

int64_t ruleforge_session_get_memory_peak(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return -1;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    last_error[0] = '\0';
    return static_cast<int64_t>(session_wrapper->session->get_arena().memory_peak());
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get memory peak: ", e.what());
    return -1;
  }
}

ruleforge_status_t ruleforge_session_get_memory_stats(ruleforge_stateful_session_t session,
                                                      char *buffer, size_t buffer_size,
                                                      size_t *out_actual_length) {
  if (!session || !buffer || !out_actual_length) {
    set_error("Session handle, buffer, or actual length pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    std::string stats = session_wrapper->session->get_memory_stats();

    *out_actual_length = stats.length();
    if (buffer_size < stats.length() + 1) {
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
