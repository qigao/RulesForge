#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <cxxopts.hpp>
#include <jsoncons/json.hpp>

#include "rule_forge.h"


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

// Filter JSON object to remove fields starting with '_'
jsoncons::json filter_fact_json(const jsoncons::json &fact) {
  jsoncons::json filtered = jsoncons::json::object();
  for (const auto &member : fact.object_range()) {
    if (!member.key().empty() && member.key()[0] != '_') {
      filtered[member.key()] = member.value();
    }
  }
  return filtered;
}

// Load facts from JSON array
int load_facts_from_json(ruleforge_stateful_session_t session, const jsoncons::json &facts_array,
                         const std::string &fact_type, bool verbose) {
  int loaded = 0;
  for (const auto &fact_json : facts_array.array_range()) {
    jsoncons::json filtered = filter_fact_json(fact_json);
    std::string json_str = filtered.to_string();
    if (check_result(ruleforge_session_add_fact_json(session, fact_type.c_str(), json_str.c_str()),
                     "Add fact")) {
      ++loaded;
    } else if (verbose) {
      std::cerr << "  Failed JSON: " << json_str.substr(0, 100) << "..." << std::endl;
    }
  }
  return loaded;
}

// Find the main data array in JSON (heuristic: largest array)
std::string find_data_array_key(const jsoncons::json &root) {
  std::string best_key;
  size_t best_size = 0;

  for (const auto &member : root.object_range()) {
    if (member.value().is_array() && member.value().size() > best_size) {
      best_size = member.value().size();
      best_key = member.key();
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

int main(int argc, char *argv[]) {
  cxxopts::Options options("capi_demo",
                           "RulesForge C API Demo - Load RFL rules and JSON/CSV facts");
  // clang-format off
  options.add_options()
      ("r,rfl", "RFL rules file path", cxxopts::value<std::string>())
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
      std::cout << "  capi_demo -r rules.rfl -j data.json -t com.example.Order -q AllOrders"
                << std::endl;
      std::cout << std::endl;
      std::cout << "  # Multiple fact types with explicit mappings" << std::endl;
      std::cout << "  capi_demo -r rules.rfl -j data.json \\" << std::endl;
      std::cout << "    -m orders:com.shop.Order \\" << std::endl;
      std::cout << "    -m customers:com.shop.Customer" << std::endl;
      std::cout << std::endl;
      std::cout << "  # CSV data source" << std::endl;
      std::cout << "  capi_demo -r payments.rfl -c payments_test_data.csv \\"
                << std::endl;
      std::cout << "    -T com.example.pricing.Order -q OrdersWithDiscount \\"
                << std::endl;
      std::cout << "    -f quantity,unitPrice,finalPrice" << std::endl;
      std::cout << std::endl;
      std::cout << "  # Run loan eligibility example" << std::endl;
      std::cout << "  capi_demo -r loan-eligibility.rfl -j loan-applications-sample.json \\"
                << std::endl;
      std::cout << "    -m applications:com.bank.loan.LoanApplication \\" << std::endl;
      std::cout << "    -q LoanDecisions -b decision \\" << std::endl;
      std::cout << "    -f applicationId,approved,approvedAmount,reason" << std::endl;
      return result.count("help") ? 0 : 1;
    }

    std::string drl_path = result["rfl"].as<std::string>();
    bool verbose = result["verbose"].as<bool>();

    // Initialize RulesForge
    RulesForgeGuard guard;

    std::cout << "=== RulesForge C API Demo ===" << std::endl;
    std::cout << "Version: " << ruleforge_get_version() << std::endl << std::endl;

    // Read and compile RFL
    std::string drl_content = read_file(drl_path);
    if (verbose) {
      std::cout << "Loaded rules from: " << drl_path << std::endl;
    }

    ruleforge_knowledge_base_t kb = nullptr;
    if (!check_result(ruleforge_kb_create(&kb), "Create Knowledge Base")) {
      return 1;
    }

    if (!check_result(ruleforge_kb_load_drl(kb, drl_content.c_str()), "Load RFL rules")) {
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
      jsoncons::json root = jsoncons::json::parse(json_content);

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
          array_key = find_data_array_key(root);
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
        if (!root.contains(array_key)) {
          std::cerr << "Warning: JSON key '" << array_key << "' not found, skipping" << std::endl;
          continue;
        }
        if (!root[array_key].is_array()) {
          std::cerr << "Warning: JSON key '" << array_key << "' is not an array, skipping"
                    << std::endl;
          continue;
        }

        int loaded = load_facts_from_json(session, root[array_key], fact_type, verbose);
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
      if (!check_result(ruleforge_session_add_facts_csv_file(
                            session, fact_type.c_str(), csv_path.c_str(), &total_facts_loaded),
                        "Load CSV facts")) {
        ruleforge_session_destroy(session);
        ruleforge_kb_destroy(kb);
        return 1;
      }
      std::cout << "Loaded " << total_facts_loaded << " " << fact_type << " facts from CSV: "
                << csv_path << std::endl;
    }

    std::cout << "Facts in session: " << ruleforge_session_get_fact_count(session) << std::endl;

    // Fire rules
    int fired = 0;
    if (!check_result(ruleforge_session_fire_all_rules(session, -1, &fired), "Fire rules")) {
      ruleforge_session_destroy(session);
      ruleforge_kb_destroy(kb);
      return 1;
    }
    std::cout << "Rules fired: " << fired << std::endl;
    std::cout << "Facts after rules: " << ruleforge_session_get_fact_count(session) << std::endl;

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

      // Parse fields to display
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
