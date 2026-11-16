#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <span>
#include <string_view>
#include <cstdio>
#include <cerrno>
#include <system_error>
#include <exception>

extern "C" {
#include <cmd_arger.h>
}
#include <json_parser.h>

#include "rules_forge.h"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {
// One process-level parse. Strings borrow argv or the SDK's response storage,
// which it retains until process exit. No RulesForge resource exists at parse.
struct DemoOptions {
  static constexpr uint32_t max_map_arguments = 1024;
  char* rfl = nullptr;
  char* json = nullptr;
  char* csv = nullptr;
  char* csv_type = nullptr;
  char* type = nullptr;
  char* array = nullptr;
  char* query = nullptr;
  char binding_default[8] = "$result";
  char* binding = binding_default;
  char* fields = nullptr;
  char* help = nullptr;
  int64_t validation = RULES_FORGE_VALIDATION_NONE;
  CmdArgerBool trace = 0, trace_network = 0, memory = 0, verbose = 0;
  std::array<char*, max_map_arguments> mappings{};
  uint32_t mapping_count = 0;
  std::array<char*, 2> positional{};
  uint32_t positional_count = 0;
};

// The released parser prints diagnostics and exits on malformed input. Route
// that output to stderr for the parse only; restore stdout before app output.
class ParserDiagnostics {
  int saved_;
  static int duplicate(int descriptor) {
#ifdef _WIN32
    return _dup(descriptor);
#else
    return dup(descriptor);
#endif
  }
  static int redirect(int source, int destination) {
#ifdef _WIN32
    return _dup2(source, destination);
#else
    return dup2(source, destination);
#endif
  }
  static int close_descriptor(int descriptor) {
#ifdef _WIN32
    return _close(descriptor);
#else
    return close(descriptor);
#endif
  }
public:
  ParserDiagnostics(ParserDiagnostics const&) = delete;
  ParserDiagnostics& operator=(ParserDiagnostics const&) = delete;
  ParserDiagnostics() : saved_(duplicate(1)) {
    if (saved_ < 0) throw std::system_error(errno, std::generic_category(), "Save CLI stdout");
    if (std::fflush(stdout) != 0 || redirect(2, 1) < 0) {
      int error = errno;
      close_descriptor(saved_);
      throw std::system_error(error, std::generic_category(), "Route CLI diagnostics");
    }
  }
  ~ParserDiagnostics() {
    if (std::fflush(stdout) != 0 || redirect(saved_, 1) < 0 || close_descriptor(saved_) < 0)
      std::terminate();
  }
};

// Normalize the app's help request without using the SDK's exit(1) help path.
// Descriptor metadata is the only source of option/value arity; values named
// "--help" and everything after "--" must remain literal input.
bool normalize_cli(std::span<char*> argv, std::span<CmdArgerDesc const> options) {
  static char help_request[] = "--help=";
  bool positional = false;
  for (size_t i = 1; i < argv.size(); ++i) {
    std::string_view token(argv[i]);
    if (token == "--") return positional || i + 1 < argv.size();
    if (token == "--help" || token == "-h") { argv[i] = help_request; continue; }
    bool long_option = token.starts_with("--");
    bool short_option = token.size() == 2 && token.front() == '-';
    if (!long_option && token.size() > 2 && token.front() == '-')
      throw std::invalid_argument("Write short options separately (for example -v -h)");
    if (long_option && token.find('=') != std::string_view::npos) continue;
    if (long_option || short_option) {
      auto name = token.substr(long_option ? 2 : 1);
      for (auto const& option : options)
        if ((long_option && name == option.name) ||
            (short_option && option.short_name && name == option.short_name)) {
          if (option.kind != CmdArgerDescKind_flag) ++i;
          break;
        }
    } else if (!token.starts_with('-')) positional = true;
  }
  return positional;
}

void print_options(std::span<CmdArgerDesc const> options) {
  std::cout << "RulesForge C API Demo - Load RFL rules and JSON/CSV facts\n"
               "Usage: capi_demo [options] <rfl-file> [json-file]\n\nOptions:\n";
  for (auto const& option : options) {
    std::cout << "  ";
    if (option.short_name) std::cout << '-' << option.short_name << ", ";
    std::cout << "--" << option.name;
    if (option.kind != CmdArgerDescKind_flag && std::string_view(option.name) != "help")
      std::cout << " <value>";
    std::cout << "\t" << option.info << '\n';
  }
}
} // namespace

// RAII wrapper for RulesForge resources
class RulesForgeGuard {
public:
  RulesForgeGuard() {
    if (ruleforge_init() != RULES_FORGE_OK)
      throw std::runtime_error(ruleforge_get_last_error_message());
  }
  ~RulesForgeGuard() { ruleforge_cleanup(); }
};

// Read file contents
std::string read_file(const std::string &path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("Cannot open file: " + path);
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

// Print error and return false
bool check_result(int result, const std::string &operation) {
  if (result != RULES_FORGE_OK) {
    std::cerr << "Error: " << operation << std::endl;
    const char *err = ruleforge_get_last_error_message();
    if (err && std::strlen(err) > 0) {
      std::cerr << "  Detail: " << err << std::endl;
    }
    return false;
  }
  return true;
}

// Ordinary demo sessions have no streams. Release dependents before their KB,
// including exceptions raised while reading or binding input. Never retry a
// failed destruction or destroy an owner whose dependent failed to close.
struct DemoResources {
  ruleforge_knowledge_base_t kb = nullptr;
  ruleforge_stateful_session_t session = nullptr;
  ruleforge_query_result_t query = nullptr;
  bool closing = false;

  ruleforge_status_t close() {
    closing = true;
    if (query) {
      auto status = ruleforge_query_result_destroy(query);
      if (status != RULES_FORGE_OK) return status;
      query = nullptr;
    }
    if (session) {
      auto status = ruleforge_session_destroy(session);
      if (status != RULES_FORGE_OK) return status;
      session = nullptr;
    }
    if (kb) {
      auto status = ruleforge_kb_destroy(kb);
      if (status != RULES_FORGE_OK) return status;
      kb = nullptr;
    }
    return RULES_FORGE_OK;
  }
  ~DemoResources() {
    if (!closing) check_result(close(), "Release demo resources after failure");
  }
};

struct JsonDeleter {
  void operator()(json_value_t *value) const {
    if (value) {
      json_free(value);
    }
  }
};

using JsonHandle = std::unique_ptr<json_value_t, JsonDeleter>;

struct JsonStringDeleter {
  void operator()(char *value) const {
    if (value) {
      json_serialize_free(value);
    }
  }
};

using JsonStringHandle = std::unique_ptr<char, JsonStringDeleter>;

JsonHandle parse_json_document(const std::string &content) {
  json_value_t *root = json_parse(content.data(), content.size());
  if (!root) {
    throw std::runtime_error("JSON parsing failed");
  }
  return JsonHandle(root);
}

JsonHandle filter_fact_json(const json_value_t *fact) {
  if (!fact || json_type(fact) != JSON_OBJECT) {
    throw std::runtime_error("Every mapped JSON array item must be an object");
  }

  JsonHandle filtered(json_create_object());
  if (!filtered) {
    throw std::runtime_error("Failed to allocate filtered JSON object");
  }

  size_t count = json_object_size(fact);
  for (size_t i = 0; i < count; ++i) {
    const char *key = json_object_key(fact, i);
    json_value_t *value = json_object_value(fact, i);
    if (!key || key[0] == '_' || !value) {
      continue;
    }
    json_value_t *clone = json_clone(value);
    if (!clone)
      throw std::runtime_error("Failed to copy JSON field: " + std::string(key));
    if (!json_object_add_checked(filtered.get(), key, clone)) {
      json_free(clone);
      throw std::runtime_error("Failed to store copied JSON field: " + std::string(key));
    }
  }
  return filtered;
}

// Load facts from JSON array and optionally keep stable fact handles.
int load_facts_from_json(ruleforge_stateful_session_t session, const json_value_t *facts_array,
                         const std::string &fact_type,
                         std::vector<ruleforge_fact_t> *loaded_facts) {
  int loaded = 0;
  if (!facts_array || json_type(facts_array) != JSON_ARRAY) {
    throw std::runtime_error("Mapped JSON value must be an array");
  }

  size_t count = json_array_size(facts_array);
  for (size_t i = 0; i < count; ++i) {
    json_value_t *item = json_array_get(facts_array, i);
    JsonHandle filtered = filter_fact_json(item);

    size_t json_len = 0;
    JsonStringHandle json_text(json_serialize(filtered.get(), &json_len));
    if (!json_text) {
      throw std::runtime_error("Failed to serialize mapped JSON fact");
    }

    std::string json_str(json_text.get(), json_len);
    ruleforge_fact_t fact = nullptr;
    if (check_result(ruleforge_session_add_fact_json(
                         session, fact_type.c_str(), json_str.c_str(), &fact),
                     "Add fact")) {
      if (loaded_facts && fact) {
        loaded_facts->push_back(fact);
      }
      ++loaded;
    } else {
      throw std::runtime_error("JSON insertion failed at " + fact_type + "[" + std::to_string(i) + "]");
    }
  }
  return loaded;
}

// Find the main data array in JSON (heuristic: largest array)
std::string find_data_array_key(const json_value_t *root) {
  std::string best_key;
  size_t best_size = 0;
  if (!root || json_type(root) != JSON_OBJECT) {
    return best_key;
  }

  size_t count = json_object_size(root);
  for (size_t i = 0; i < count; ++i) {
    const char *key = json_object_key(root, i);
    json_value_t *value = json_object_value(root, i);
    if (key && value && json_type(value) == JSON_ARRAY) {
      size_t size = json_array_size(value);
      if (size > best_size) {
        best_size = size;
        best_key = key;
      }
    }
  }
  return best_key;
}

// Parse mapping string "array:type" into pair
std::pair<std::string, std::string> parse_mapping(std::string_view mapping) {
  auto pos = mapping.find(':');
  if (pos == std::string::npos || pos == 0 || pos + 1 == mapping.size()) {
    throw std::runtime_error("Invalid mapping format: " + std::string(mapping) + " (expected array:type)");
  }
  return {std::string(mapping.substr(0, pos)), std::string(mapping.substr(pos + 1))};
}

// Print fact field value
void print_field_value(ruleforge_fact_t fact, const std::string &field_name) {
  char str_buffer[256];
  size_t actual_len = 0;
  double double_val = 0;
  int64_t int_val = 0;

  if (ruleforge_fact_get_field_as_string(fact, field_name.c_str(), str_buffer, sizeof(str_buffer),
                                         &actual_len) == RULES_FORGE_OK) {
    std::cout << str_buffer;
  } else if (ruleforge_fact_get_field_as_double(fact, field_name.c_str(), &double_val) ==
             RULES_FORGE_OK) {
    std::cout << std::fixed << std::setprecision(2) << double_val;
  } else if (ruleforge_fact_get_field_as_int(fact, field_name.c_str(), &int_val) == RULES_FORGE_OK) {
    std::cout << int_val;
  } else {
    std::cout << "(unknown)";
  }
}

void print_memory_stats(ruleforge_stateful_session_t session) {
  char buffer[1024] = {0};
  size_t actual_length = 0;
  if (ruleforge_session_get_memory_stats(session, buffer, sizeof(buffer), &actual_length) ==
      RULES_FORGE_OK) {
    std::cout << "\n=== Memory Stats ===" << std::endl;
    std::cout << buffer << std::endl;
  } else {
    std::cerr << "Warning: failed to get memory stats" << std::endl;
  }
}

void print_trace(ruleforge_stateful_session_t session, bool include_network) {
  size_t actual_length = 0;
  std::vector<char> buffer(64 * 1024, '\0');
  ruleforge_status_t status =
      ruleforge_session_get_execution_trace(session, include_network ? 1 : 0, buffer.data(),
                                            buffer.size(), &actual_length);
  if (status == RULES_FORGE_OK) {
    std::cout << "\n=== Execution Trace ===" << std::endl;
    std::cout << buffer.data() << std::endl;
    return;
  }

  if (status == RULES_FORGE_ERROR_INVALID_ARGUMENT && actual_length > buffer.size()) {
    buffer.assign(actual_length + 1, '\0');
    status = ruleforge_session_get_execution_trace(session, include_network ? 1 : 0, buffer.data(),
                                                   buffer.size(), &actual_length);
    if (status == RULES_FORGE_OK) {
      std::cout << "\n=== Execution Trace ===" << std::endl;
      std::cout << buffer.data() << std::endl;
      return;
    }
  }

  std::cerr << "Warning: failed to get execution trace" << std::endl;
}

void print_first_loaded_fact(ruleforge_fact_t fact, const std::vector<std::string> &fields) {
  if (!fact || fields.empty()) {
    return;
  }

  std::cout << "\n=== First Loaded Fact ===" << std::endl;
  for (const auto &field : fields) {
    std::cout << "  " << field << ": ";
    print_field_value(fact, field);
    std::cout << std::endl;
  }
}

int main(int argc, char *argv[]) {
  try {
    // Static storage supplies the owner required by the C validator callback;
    // this executable parses exactly once and exposes no global CLI service.
    static DemoOptions cli;
    CmdArgerEnumDesc validation_choices[] = {
        {"none", "No schema validation", RULES_FORGE_VALIDATION_NONE},
        {"warn", "Report validation warnings", RULES_FORGE_VALIDATION_WARN},
        {"strict", "Reject invalid facts", RULES_FORGE_VALIDATION_STRICT}};
    auto mappings = cmd_arger_desc_string_list_sh(
        cli.mappings.data(), &cli.mapping_count, cli.mappings.size(), "map", "m",
        "Map JSON array to fact type (array:Type); repeat or separate with commas");
    // The released list parser stops appending at capacity. Reject the next
    // value in its validator instead of allowing a silently truncated list.
    mappings.validator = [](char const*, char const** error) -> CmdArgerBool {
      if (cli.mapping_count == DemoOptions::max_map_arguments) {
        *error = "Too many --map arguments (maximum 1024)";
        return cmd_arger_false;
      }
      return cmd_arger_true;
    };
    CmdArgerDesc options[] = {
        cmd_arger_desc_string_sh(&cli.rfl, "rfl", "r", "RFL rules file path"),
        cmd_arger_desc_string_sh(&cli.json, "json", "j", "JSON facts file path"),
        cmd_arger_desc_string_sh(&cli.csv, "csv", "c", "CSV facts file path (header required)"),
        cmd_arger_desc_string_sh(&cli.csv_type, "csv-type", "T", "Fact type for CSV rows"),
        mappings,
        cmd_arger_desc_string_sh(&cli.type, "type", "t", "Default fact type for an array"),
        cmd_arger_desc_string_sh(&cli.array, "array", "a", "JSON array key"),
        cmd_arger_desc_string_sh(&cli.query, "query", "q", "Query after firing rules"),
        cmd_arger_desc_string_sh(&cli.binding, "binding", "b", "Query binding (default: $result)"),
        cmd_arger_desc_string_sh(&cli.fields, "fields", "f", "Comma-separated result fields"),
        cmd_arger_desc_enum(&cli.validation, "validation", "Validation: none|warn|strict",
                           validation_choices, std::size(validation_choices)),
        cmd_arger_desc_flag(&cli.trace, "trace", "Print execution trace"),
        cmd_arger_desc_flag(&cli.trace_network, "trace-network", "Include RETE trace events"),
        cmd_arger_desc_flag(&cli.memory, "memory", "Print memory statistics"),
        cmd_arger_desc_flag_sh(&cli.verbose, "verbose", "v", "Verbose output"),
        cmd_arger_desc_string_sh(&cli.help, "help", "h", "Print usage")};
    std::vector<char*> arguments(argv, argv + argc);
    bool has_positional = normalize_cli(arguments, options);
    auto positional = cmd_arger_desc_string_list(cli.positional.data(), &cli.positional_count,
        cli.positional.size(), "<rfl-file> [json-file]", "Positional input paths");
    {
      ParserDiagnostics diagnostics;
      cmd_arger_parse(options, std::size(options), &positional, has_positional ? 1 : 0,
          argc, arguments.data(), "RulesForge C API Demo", cmd_arger_false);
    }
    for (uint32_t i = 0; i < cli.positional_count; ++i) {
      if (!cli.rfl) cli.rfl = cli.positional[i];
      else if (!cli.json) cli.json = cli.positional[i];
      else throw std::invalid_argument("Unexpected positional input path");
    }

    if (cli.help || !cli.rfl) {
      print_options(options);
      std::cout << "\nExamples:" << std::endl;
      std::cout << "  # Single fact type with auto-detect array" << std::endl;
      std::cout << "  capi_demo -r rules.rfl -j data.json -t Order -q AllOrders -b order"
                << std::endl;
      std::cout << std::endl;
      std::cout << "  # Multiple fact types with explicit mappings" << std::endl;
      std::cout << "  capi_demo -r rules.rfl -j data.json \\" << std::endl;
      std::cout << "    -m orders:Order \\" << std::endl;
      std::cout << "    -m customers:Customer" << std::endl;
      std::cout << std::endl;
      std::cout << "  # CSV data source" << std::endl;
      std::cout << "  capi_demo -r payments.rfl -c payments_test_data.csv \\"
                << std::endl;
      std::cout << "    -T Order -q OrdersWithDiscount \\"
                << std::endl;
      std::cout << "    -b order \\"
                << std::endl;
      std::cout << "    -f quantity,unitPrice,finalPrice" << std::endl;
      std::cout << std::endl;
      std::cout << "  # Enable validation, tracing, and memory stats" << std::endl;
      std::cout << "  capi_demo -r payments.rfl -c payments_test_data.csv \\" << std::endl;
      std::cout << "    -T Order --validation warn --trace --memory \\" << std::endl;
      std::cout << "    -q OrdersWithDiscount -b order -f quantity,finalPrice" << std::endl;
      std::cout << std::endl;
      std::cout << "  # Loan eligibility: migrate external input declarations to an imported schema first" << std::endl;
      std::cout << "  capi_demo -r loan-eligibility.rfl -j loan-applications-sample.json \\"
                << std::endl;
      std::cout << "    -m applications:LoanApplication \\" << std::endl;
      std::cout << "    -q LoanDecisions -b decision \\" << std::endl;
      std::cout << "    -f applicationId,approved,approvedAmount,reason" << std::endl;
      return cli.help ? 0 : 1;
    }

    std::string drl_path = cli.rfl;
    bool verbose = cli.verbose != 0;
    if (cli.json && cli.csv)
      throw std::invalid_argument("--json and --csv cannot be used together");
    if (cli.json && !cli.type && cli.mapping_count == 0)
      throw std::invalid_argument("--type or --map is required when loading JSON facts");
    if (cli.csv && !cli.csv_type)
      throw std::invalid_argument("--csv-type is required when loading CSV facts");

    // Initialize RulesForge
    RulesForgeGuard guard;

    std::cout << "=== RulesForge C API Demo ===" << std::endl;
    std::cout << "Version: " << ruleforge_get_version() << std::endl << std::endl;

    DemoResources resources;
    auto &kb = resources.kb;
    if (!check_result(ruleforge_kb_create(&kb), "Create Knowledge Base")) {
      return 1;
    }
    if (!check_result(ruleforge_kb_load_drl_file(kb, drl_path.c_str(), nullptr, 0),
                      "Load RFL rules")) {
      return 1;
    }
    std::cout << "Rules compiled successfully from: " << drl_path << std::endl;

    // Create session
    auto &session = resources.session;
    if (!check_result(ruleforge_session_create(kb, &session), "Create Session")) {

      return 1;
    }

    if (!check_result(ruleforge_session_set_validation_mode(
                          session, static_cast<ruleforge_validation_mode_t>(cli.validation)),
                      "Set validation mode")) {

      return 1;
    }

    bool trace_enabled = cli.trace != 0 || cli.trace_network != 0;
    if (trace_enabled &&
        !check_result(ruleforge_session_enable_tracing(session, 1), "Enable tracing")) {

      return 1;
    }

    std::vector<std::string> fields;
    if (cli.fields) {
      std::string fields_str = cli.fields;
      std::stringstream ss(fields_str);
      std::string field;
      while (std::getline(ss, field, ',')) {
        if (!field.empty()) {
          fields.push_back(field);
        }
      }
    }

    std::vector<ruleforge_fact_t> loaded_facts;

    // Load JSON facts if provided
    int total_facts_loaded = 0;
    if (cli.json) {
      std::string json_path = cli.json;
      std::string json_content = read_file(json_path);
      JsonHandle root = parse_json_document(json_content);

      // Build type mappings
      std::map<std::string, std::string> type_mappings;

      if (cli.mapping_count != 0) {
        for (char const* raw : std::span(cli.mappings).first(cli.mapping_count)) {
          std::string_view remaining(raw);
          for (;;) {
            auto comma = remaining.find(',');
            auto [array_key, fact_type] = parse_mapping(remaining.substr(0, comma));
            type_mappings[array_key] = fact_type;
            if (verbose)
              std::cout << "Mapping: " << array_key << " -> " << fact_type << std::endl;
            if (comma == std::string_view::npos) break;
            remaining.remove_prefix(comma + 1);
          }
        }
      }

      // If no explicit mappings, use --type and --array or auto-detect
      if (type_mappings.empty()) {
        std::string fact_type = cli.type;
        std::string array_key;

        if (cli.array) {
          array_key = cli.array;
        } else {
          array_key = find_data_array_key(root.get());
          if (array_key.empty()) {
            std::cerr << "Error: Could not find data array in JSON. Use --array to specify."
                      << std::endl;

            return 1;
          }
          if (verbose) {
            std::cout << "Auto-detected array key: " << array_key << std::endl;
          }
        }
        type_mappings[array_key] = fact_type;
      }

      // Load facts for each mapping
      for (const auto &[array_key, fact_type] : type_mappings) {
        json_value_t *array_value = json_object_get(root.get(), array_key.c_str());
        if (!array_value) {
          throw std::runtime_error("JSON key '" + array_key + "' not found");
        }
        if (json_type(array_value) != JSON_ARRAY) {
          throw std::runtime_error("JSON key '" + array_key + "' is not an array");
        }

        int loaded = load_facts_from_json(session, array_value, fact_type,
                                          &loaded_facts);
        std::cout << "Loaded " << loaded << " " << fact_type << " facts from '" << array_key << "'"
                  << std::endl;
        total_facts_loaded += loaded;
      }

      std::cout << "Total facts loaded from: " << json_path << ": " << total_facts_loaded
                << std::endl;
    } else if (cli.csv) {
      std::string csv_path = cli.csv;
      std::string fact_type = cli.csv_type;
      std::string csv_content = read_file(csv_path);
      ruleforge_fact_t *csv_facts = nullptr;
      if (!check_result(ruleforge_session_add_facts_csv(
                            session, fact_type.c_str(), csv_content.c_str(),
                            &csv_facts, &total_facts_loaded),
                        "Load CSV facts")) {

        return 1;
      }
      for (int i = 0; i < total_facts_loaded; ++i) {
        loaded_facts.push_back(csv_facts[i]);
      }
      ruleforge_fact_array_free(csv_facts);
      std::cout << "Loaded " << total_facts_loaded << " " << fact_type << " facts from CSV: "
                << csv_path << std::endl;
    }

    std::cout << "Facts in session: " << ruleforge_session_get_fact_count(session) << std::endl;
    if (!loaded_facts.empty()) {
      print_first_loaded_fact(loaded_facts.front(), fields);
    }

    // Fire rules
    int fired = 0;
    if (!check_result(ruleforge_session_fire_all_rules(session, -1, &fired), "Fire rules")) {

      return 1;
    }
    std::cout << "Rules fired: " << fired << std::endl;
    std::cout << "Facts after rules: " << ruleforge_session_get_fact_count(session) << std::endl;
    if (cli.memory != 0) {
      print_memory_stats(session);
    }
    if (trace_enabled) {
      print_trace(session, cli.trace_network != 0);
    }

    // Execute query if specified
    if (cli.query) {
      std::string query_name = cli.query;
      std::string binding = cli.binding;

      // Remove $ prefix if present for internal use
      if (!binding.empty() && binding[0] == '$') {
        binding = binding.substr(1);
      }

      auto &query_result = resources.query;
      if (!check_result(ruleforge_session_query(session, query_name.c_str(), &query_result),
                        "Execute query")) {

        return 1;
      }

      int count = ruleforge_query_result_get_size(query_result);
      std::cout << "\n=== Query Results: " << query_name << " ===" << std::endl;
      std::cout << "Found " << count << " result(s)" << std::endl;

      for (int i = 0; i < count; ++i) {
        ruleforge_fact_t fact = nullptr;
        if (ruleforge_query_result_get_fact_at_index(query_result, i, binding.c_str(), &fact) !=
            RULES_FORGE_OK) {
          throw std::runtime_error("Query row " + std::to_string(i) +
                                   ": failed to get binding '" + binding + "'");
        }

        std::cout << "\nResult #" << (i + 1) << ":" << std::endl;
        if (fields.empty()) {
          std::cout << "  (use --fields to specify which fields to display)" << std::endl;
        } else {
          for (const auto &field : fields) {
            std::cout << "  " << field << ": ";
            print_field_value(fact, field);
            std::cout << std::endl;
          }
        }
      }

    }

    // Cleanup

    if (!check_result(resources.close(), "Release demo resources"))
      return 1;

    std::cout << "\nDemo completed successfully." << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
