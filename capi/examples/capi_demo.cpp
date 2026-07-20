#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <cxxopts.hpp>
#include <turbo_parser.h>

#include "rules_forge.h"


// RAII wrapper for RulesForge resources
class RulesForgeGuard {
public:
  RulesForgeGuard() { ruleforge_init(); }
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

ruleforge_validation_mode_t parse_validation_mode(const std::string &mode) {
  if (mode == "none") {
    return RULES_FORGE_VALIDATION_NONE;
  }
  if (mode == "warn") {
    return RULES_FORGE_VALIDATION_WARN;
  }
  if (mode == "strict") {
    return RULES_FORGE_VALIDATION_STRICT;
  }
  throw std::runtime_error("Invalid validation mode: " + mode + " (expected none|warn|strict)");
}

struct TurboJsonDeleter {
  void operator()(turbo_json_doc_t *value) const {
    if (value) {
      turbo_free_json(&value);
    }
  }
};

using TurboJsonHandle = std::unique_ptr<turbo_json_doc_t, TurboJsonDeleter>;

struct TurboJsonStringDeleter {
  void operator()(char *value) const {
    if (value) {
      turbo_json_serialize_free(value);
    }
  }
};

using TurboJsonStringHandle = std::unique_ptr<char, TurboJsonStringDeleter>;

TurboJsonHandle parse_json_document(const std::string &content) {
  turbo_json_doc_t *root = nullptr;
  int rc = turbo_parse_json(reinterpret_cast<const uint8_t *>(content.data()), content.size(), &root);
  if (rc != 0 || !root) {
    if (root) {
      turbo_free_json(&root);
    }
    throw std::runtime_error("JSON parsing failed");
  }
  return TurboJsonHandle(root);
}

TurboJsonHandle filter_fact_json(const json_value_t *fact) {
  if (!fact || turbo_json_type(fact) != TURBO_JSON_OBJECT) {
    return TurboJsonHandle(nullptr);
  }

  TurboJsonHandle filtered(turbo_json_create_object());
  if (!filtered) {
    throw std::runtime_error("Failed to allocate filtered JSON object");
  }

  size_t count = turbo_json_object_size(fact);
  for (size_t i = 0; i < count; ++i) {
    const char *key = turbo_json_object_key(fact, i);
    json_value_t *value = turbo_json_object_value(fact, i);
    if (!key || key[0] == '_' || !value) {
      continue;
    }
    json_value_t *clone = turbo_json_clone(value);
    if (clone) {
      turbo_json_object_add(filtered.get(), key, clone);
    }
  }
  return filtered;
}

// Load facts from JSON array and optionally keep stable fact handles.
int load_facts_from_json(ruleforge_stateful_session_t session, const json_value_t *facts_array,
                         const std::string &fact_type, bool verbose,
                         std::vector<ruleforge_fact_t> *loaded_facts) {
  int loaded = 0;
  if (!facts_array || turbo_json_type(facts_array) != TURBO_JSON_ARRAY) {
    return loaded;
  }

  size_t count = turbo_json_array_size(facts_array);
  for (size_t i = 0; i < count; ++i) {
    json_value_t *item = turbo_json_array_get(facts_array, i);
    TurboJsonHandle filtered = filter_fact_json(item);
    if (!filtered) {
      continue;
    }

    size_t json_len = 0;
    TurboJsonStringHandle json_text(turbo_json_serialize(filtered.get(), &json_len));
    if (!json_text) {
      continue;
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
    } else if (verbose) {
      std::cerr << "  Failed JSON: " << json_str.substr(0, 100) << "..." << std::endl;
    }
  }
  return loaded;
}

// Find the main data array in JSON (heuristic: largest array)
std::string find_data_array_key(const json_value_t *root) {
  std::string best_key;
  size_t best_size = 0;
  if (!root || turbo_json_type(root) != TURBO_JSON_OBJECT) {
    return best_key;
  }

  size_t count = turbo_json_object_size(root);
  for (size_t i = 0; i < count; ++i) {
    const char *key = turbo_json_object_key(root, i);
    json_value_t *value = turbo_json_object_value(root, i);
    if (key && value && turbo_json_type(value) == TURBO_JSON_ARRAY) {
      size_t size = turbo_json_array_size(value);
      if (size > best_size) {
        best_size = size;
        best_key = key;
      }
    }
  }
  return best_key;
}

// Parse mapping string "array:type" into pair
std::pair<std::string, std::string> parse_mapping(const std::string &mapping) {
  auto pos = mapping.find(':');
  if (pos == std::string::npos) {
    throw std::runtime_error("Invalid mapping format: " + mapping + " (expected array:type)");
  }
  return {mapping.substr(0, pos), mapping.substr(pos + 1)};
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
  cxxopts::Options options("capi_demo",
                           "RulesForge C API Demo - Load RFL rules and JSON/CSV facts");
  // clang-format off
  options.add_options()
      ("r,rfl", "RFL rules file path", cxxopts::value<std::string>())
      ("s,schema", "Schema file path for all external input data", cxxopts::value<std::string>())
      ("j,json", "JSON facts file path", cxxopts::value<std::string>())
      ("c,csv", "CSV facts file path (header row required)", cxxopts::value<std::string>())
      ("T,csv-type", "Fact type for CSV rows", cxxopts::value<std::string>())
      ("m,map", "Map JSON array to fact type (format: array:com.package.Type). Can be repeated.",
                                                cxxopts::value<std::vector<std::string>>())
      ("t,type", "Default fact type for auto-detected array", cxxopts::value<std::string>())
      ("a,array", "JSON array key (for single-type loading)", cxxopts::value<std::string>())
      ("q,query", "Query name to execute after firing rules", cxxopts::value<std::string>())
      ("b,binding", "Binding name for query results", cxxopts::value<std::string>()->default_value("$result"))
      ("f,fields", "Comma-separated field names to display from results", cxxopts::value<std::string>())
      ("validation", "Validation mode: none|warn|strict", cxxopts::value<std::string>()->default_value("none"))
      ("trace", "Print execution trace after firing rules", cxxopts::value<bool>()->default_value("false"))
      ("trace-network", "Include RETE network events in trace output", cxxopts::value<bool>()->default_value("false"))
      ("memory", "Print session memory statistics", cxxopts::value<bool>()->default_value("false"))
      ("v,verbose", "Enable verbose output", cxxopts::value<bool>()->default_value("false"))
      ("h,help",  "Print usage");
  // clang-format on
  options.parse_positional({"rfl", "json"});
  options.positional_help("<rfl-file> [json-file]");

  try {
    auto result = options.parse(argc, argv);

    if (result.count("help") || !result.count("rfl")) {
      std::cout << options.help() << std::endl;
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
      std::cout << "  capi_demo -r payments.rfl -s payments.schema -c payments_test_data.csv \\"
                << std::endl;
      std::cout << "    -T Order -q OrdersWithDiscount \\"
                << std::endl;
      std::cout << "    -b order \\"
                << std::endl;
      std::cout << "    -f quantity,unitPrice,finalPrice" << std::endl;
      std::cout << std::endl;
      std::cout << "  # Enable validation, tracing, and memory stats" << std::endl;
      std::cout << "  capi_demo -r payments.rfl -s payments.schema -c payments_test_data.csv \\" << std::endl;
      std::cout << "    -T Order --validation warn --trace --memory \\" << std::endl;
      std::cout << "    -q OrdersWithDiscount -b order -f quantity,finalPrice" << std::endl;
      std::cout << std::endl;
      std::cout << "  # Run loan eligibility example" << std::endl;
      std::cout << "  capi_demo -r loan-eligibility.rfl -s loan.schema -j loan-applications-sample.json \\"
                << std::endl;
      std::cout << "    -m applications:LoanApplication \\" << std::endl;
      std::cout << "    -q LoanDecisions -b decision \\" << std::endl;
      std::cout << "    -f applicationId,approved,approvedAmount,reason" << std::endl;
      return result.count("help") ? 0 : 1;
    }

    std::string drl_path = result["rfl"].as<std::string>();
    std::string schema_path;
    if (result.count("csv")) {
      if (!result.count("schema")) {
        std::cerr << "Error: --schema is required for external CSV input" << std::endl;
        return 1;
      }
      schema_path = result["schema"].as<std::string>();
    }
    bool verbose = result["verbose"].as<bool>();

    // Initialize RulesForge
    RulesForgeGuard guard;

    std::cout << "=== RulesForge C API Demo ===" << std::endl;
    std::cout << "Version: " << ruleforge_get_version() << std::endl << std::endl;

    ruleforge_knowledge_base_t kb = nullptr;
    if (!check_result(ruleforge_kb_create(&kb), "Create Knowledge Base")) {
      return 1;
    }
    if (!check_result(ruleforge_kb_load_drl_file(kb, drl_path.c_str(), nullptr, 0),
                      "Load RFL rules")) {
      ruleforge_kb_destroy(kb);
      return 1;
    }
    std::cout << "Rules compiled successfully from: " << drl_path << std::endl;

    // Create session
    ruleforge_stateful_session_t session = nullptr;
    if (!check_result(ruleforge_session_create(kb, &session), "Create Session")) {
      ruleforge_kb_destroy(kb);
      return 1;
    }

    std::string validation_mode = result["validation"].as<std::string>();
    if (!check_result(ruleforge_session_set_validation_mode(
                          session, parse_validation_mode(validation_mode)),
                      "Set validation mode")) {
      ruleforge_session_destroy(session);
      ruleforge_kb_destroy(kb);
      return 1;
    }

    bool trace_enabled = result["trace"].as<bool>() || result["trace-network"].as<bool>();
    if (trace_enabled &&
        !check_result(ruleforge_session_enable_tracing(session, 1), "Enable tracing")) {
      ruleforge_session_destroy(session);
      ruleforge_kb_destroy(kb);
      return 1;
    }

    std::vector<std::string> fields;
    if (result.count("fields")) {
      std::string fields_str = result["fields"].as<std::string>();
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
    if (result.count("json") && result.count("csv")) {
      std::cerr << "Error: --json and --csv cannot be used together" << std::endl;
      ruleforge_session_destroy(session);
      ruleforge_kb_destroy(kb);
      return 1;
    }

    if (result.count("json")) {
      std::string json_path = result["json"].as<std::string>();
      std::string json_content = read_file(json_path);
      TurboJsonHandle root = parse_json_document(json_content);

      // Build type mappings
      std::map<std::string, std::string> type_mappings;

      if (result.count("map")) {
        for (const auto &mapping : result["map"].as<std::vector<std::string>>()) {
          auto [array_key, fact_type] = parse_mapping(mapping);
          type_mappings[array_key] = fact_type;
          if (verbose) {
            std::cout << "Mapping: " << array_key << " -> " << fact_type << std::endl;
          }
        }
      }

      // If no explicit mappings, use --type and --array or auto-detect
      if (type_mappings.empty()) {
        if (!result.count("type")) {
          std::cerr << "Error: --type or --map is required when loading JSON facts" << std::endl;
          ruleforge_session_destroy(session);
          ruleforge_kb_destroy(kb);
          return 1;
        }

        std::string fact_type = result["type"].as<std::string>();
        std::string array_key;

        if (result.count("array")) {
          array_key = result["array"].as<std::string>();
        } else {
          array_key = find_data_array_key(root.get());
          if (array_key.empty()) {
            std::cerr << "Error: Could not find data array in JSON. Use --array to specify."
                      << std::endl;
            ruleforge_session_destroy(session);
            ruleforge_kb_destroy(kb);
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
        json_value_t *array_value = turbo_json_object_get(root.get(), array_key.c_str());
        if (!array_value) {
          std::cerr << "Warning: JSON key '" << array_key << "' not found, skipping" << std::endl;
          continue;
        }
        if (turbo_json_type(array_value) != TURBO_JSON_ARRAY) {
          std::cerr << "Warning: JSON key '" << array_key << "' is not an array, skipping"
                    << std::endl;
          continue;
        }

        int loaded = load_facts_from_json(session, array_value, fact_type, verbose,
                                          &loaded_facts);
        std::cout << "Loaded " << loaded << " " << fact_type << " facts from '" << array_key << "'"
                  << std::endl;
        total_facts_loaded += loaded;
      }

      std::cout << "Total facts loaded from: " << json_path << ": " << total_facts_loaded
                << std::endl;
    } else if (result.count("csv")) {
      if (!result.count("csv-type")) {
        std::cerr << "Error: --csv-type is required when loading CSV facts" << std::endl;
        ruleforge_session_destroy(session);
        ruleforge_kb_destroy(kb);
        return 1;
      }

      std::string csv_path = result["csv"].as<std::string>();
      std::string fact_type = result["csv-type"].as<std::string>();
      std::string csv_content = read_file(csv_path);
      ruleforge_fact_t *csv_facts = nullptr;
      if (!check_result(ruleforge_session_add_facts_csv(
                            session, fact_type.c_str(), csv_content.c_str(),
                            &csv_facts, &total_facts_loaded),
                        "Load CSV facts")) {
        ruleforge_session_destroy(session);
        ruleforge_kb_destroy(kb);
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
      ruleforge_session_destroy(session);
      ruleforge_kb_destroy(kb);
      return 1;
    }
    std::cout << "Rules fired: " << fired << std::endl;
    std::cout << "Facts after rules: " << ruleforge_session_get_fact_count(session) << std::endl;
    if (result["memory"].as<bool>()) {
      print_memory_stats(session);
    }
    if (trace_enabled) {
      print_trace(session, result["trace-network"].as<bool>());
    }

    // Execute query if specified
    if (result.count("query")) {
      std::string query_name = result["query"].as<std::string>();
      std::string binding = result["binding"].as<std::string>();

      // Remove $ prefix if present for internal use
      if (!binding.empty() && binding[0] == '$') {
        binding = binding.substr(1);
      }

      ruleforge_query_result_t query_result = nullptr;
      if (!check_result(ruleforge_session_query(session, query_name.c_str(), &query_result),
                        "Execute query")) {
        ruleforge_session_destroy(session);
        ruleforge_kb_destroy(kb);
        return 1;
      }

      int count = ruleforge_query_result_get_size(query_result);
      std::cout << "\n=== Query Results: " << query_name << " ===" << std::endl;
      std::cout << "Found " << count << " result(s)" << std::endl;

      for (int i = 0; i < count; ++i) {
        ruleforge_fact_t fact = nullptr;
        if (ruleforge_query_result_get_fact_at_index(query_result, i, binding.c_str(), &fact) !=
            RULES_FORGE_OK) {
          std::cerr << "  Row " << i << ": Failed to get fact for binding '" << binding << "'"
                    << std::endl;
          continue;
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

      ruleforge_query_result_destroy(query_result);
    }

    // Cleanup
    ruleforge_session_destroy(session);
    ruleforge_kb_destroy(kb);

    std::cout << "\nDemo completed successfully." << std::endl;
    return 0;

  } catch (const cxxopts::exceptions::exception &e) {
    std::cerr << "Error parsing options: " << e.what() << std::endl;
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
