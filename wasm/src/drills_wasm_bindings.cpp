#include <emscripten.h>
#include <emscripten/bind.h>
#include <string>
#include <vector>
#include <memory>
#include <capi/drills_capi.h>
#include <iostream>
#include <sstream>

using namespace emscripten;

class DrillsEngine {
private:
    drills_knowledge_base_t kb_;
    drills_stateful_session_t session_;
    bool initialized_;

public:
    DrillsEngine() : kb_(nullptr), session_(nullptr), initialized_(false) {}

    ~DrillsEngine() {
        cleanup();
    }

    // Initialize the Drills engine
    std::string init() {
        drills_status_t status = drills_init();
        if (status != DRILLS_OK) {
            return std::string("Failed to initialize Drills engine: ") + drills_get_last_error_message();
        }
        initialized_ = true;
        return "Drills engine initialized successfully";
    }

    // Create knowledge base from DRL rules
    std::string createKnowledgeBase(const std::string& drlRules) {
        if (!initialized_) {
            return "Engine not initialized. Call init() first.";
        }

        // Destroy existing knowledge base if any
        if (kb_ != nullptr) {
            drills_kb_destroy(kb_);
            kb_ = nullptr;
        }
        if (session_ != nullptr) {
            drills_session_destroy(session_);
            session_ = nullptr;
        }

        drills_status_t status = drills_kb_create(&kb_);
        if (status != DRILLS_OK) {
            return std::string("Failed to create knowledge base: ") + drills_get_last_error_message();
        }

        // Load DRL rules
        if (!drlRules.empty()) {
            status = drills_kb_load_drl(kb_, drlRules.c_str());
            if (status != DRILLS_OK) {
                drills_kb_destroy(kb_);
                kb_ = nullptr;
                return std::string("Failed to load DRL rules: ") + drills_get_last_error_message();
            }
        }

        return "Knowledge base created successfully";
    }

    // Create stateful session
    std::string createSession() {
        if (kb_ == nullptr) {
            return "Knowledge base not created. Call createKnowledgeBase() first.";
        }

        // Destroy existing session if any
        if (session_ != nullptr) {
            drills_session_destroy(session_);
        }

        drills_status_t status = drills_session_create(kb_, &session_);
        if (status != DRILLS_OK) {
            return std::string("Failed to create session: ") + drills_get_last_error_message();
        }

        return "Session created successfully";
    }

    // Add fact to session
    std::string insertFact(const std::string& factType, const std::string& factJson) {
        if (session_ == nullptr) {
            return "Session not created. Call createSession() first.";
        }

        drills_status_t status = drills_session_add_fact_json(session_, factType.c_str(), factJson.c_str());
        if (status != DRILLS_OK) {
            return std::string("Failed to insert fact: ") + drills_get_last_error_message();
        }

        return "Fact inserted successfully";
    }

    // Fire all rules
    std::string fireRules() {
        if (session_ == nullptr) {
            return "Session not created. Call createSession() first.";
        }

        drills_status_t status = drills_session_fire_all_rules(session_);
        if (status != DRILLS_OK) {
            return std::string("Failed to fire rules: ") + drills_get_last_error_message();
        }

        return "Rules fired successfully";
    }

    // Execute query and return results as JSON
    std::string query(const std::string& queryName) {
        if (session_ == nullptr) {
            return "Session not created. Call createSession() first.";
        }

        drills_query_result_t query_result;
        drills_status_t status = drills_session_query(session_, queryName.c_str(), &query_result);
        if (status != DRILLS_OK) {
            return std::string("Failed to execute query: ") + drills_get_last_error_message();
        }

        // Build JSON response
        std::stringstream result_ss;
        result_ss << "{\"query\":\"" << queryName << "\",\"results\":[";

        int size = drills_query_result_get_size(query_result);
        for (int i = 0; i < size; ++i) {
            if (i > 0) result_ss << ",";

            result_ss << "{\"row\":" << i << ",\"bindings\":{";

            // For this demo, we'll assume a binding named "$result"
            // In a real implementation, you'd iterate through all bindings
            drills_fact_t fact;
            status = drills_query_result_get_fact_at_index(query_result, i, "$result", &fact);
            if (status == DRILLS_OK) {
                result_ss << "\"$result\":" << factToJson(fact);
            }

            result_ss << "}}";
        }

        result_ss << "]}";

        drills_query_result_destroy(query_result);
        return result_ss.str();
    }

    // Helper function to convert fact to JSON
    std::string factToJson(drills_fact_t fact) {
        std::stringstream json_ss;
        json_ss << "{";

        // For demo purposes, we'll try to read common fields
        // In practice, you'd need to know the specific structure
        const char* fields[] = {"name", "age", "type", "value", "id"};

        for (size_t i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i) {
            if (i > 0) json_ss << ",";

            CharVector buffer(256);
            size_t actual_length;
            drills_status_t status = drills_fact_get_field_as_string(fact, fields[i],
                buffer.data(), buffer.size(), &actual_length);

            if (status == DRILLS_OK && actual_length > 0) {
                json_ss << "\"" << fields[i] << "\":\"" << buffer.data() << "\"";
            } else {
                // Try as number
                double num_val;
                status = drills_fact_get_field_as_double(fact, fields[i], &num_val);
                if (status == DRILLS_OK) {
                    json_ss << "\"" << fields[i] << "\":" << num_val;
                } else {
                    // Try as boolean
                    int bool_val;
                    status = drills_fact_get_field_as_bool(fact, fields[i], &bool_val);
                    if (status == DRILLS_OK) {
                        json_ss << "\"" << fields[i] << "\":" << (bool_val ? "true" : "false");
                    } else {
                        // Try as integer
                        int64_t int_val;
                        status = drills_fact_get_field_as_int(fact, fields[i], &int_val);
                        if (status == DRILLS_OK) {
                            json_ss << "\"" << fields[i] << "\":" << int_val;
                        }
                    }
                }
            }
        }

        json_ss << "}";
        return json_ss.str();
    }

    // Cleanup resources
    void cleanup() {
        if (session_ != nullptr) {
            drills_session_destroy(session_);
            session_ = nullptr;
        }
        if (kb_ != nullptr) {
            drills_kb_destroy(kb_);
            kb_ = nullptr;
        }
        initialized_ = false;
    }

    // Get engine status
    std::string getStatus() {
        std::stringstream ss;
        ss << "{";
        ss << "\"initialized\":" << (initialized_ ? "true" : "false") << ",";
        ss << "\"hasKnowledgeBase\":" << (kb_ != nullptr ? "true" : "false") << ",";
        ss << "\"hasSession\":" << (session_ != nullptr ? "true" : "false");
        ss << "}";
        return ss.str();
    }
};

// Helper class for vector<char> to work with C strings
class CharVector {
private:
    std::vector<char> vec_;
public:
    explicit CharVector(size_t size) : vec_(size, 0) {}
    char* data() { return vec_.data(); }
    size_t size() const { return vec_.size(); }
};

// Export class to JavaScript
EMSCRIPTEN_BINDINGS(drills_module) {
    class_<DrillsEngine>("DrillsEngine")
        .constructor()
        .function("init", &DrillsEngine::init)
        .function("createKnowledgeBase", &DrillsEngine::createKnowledgeBase)
        .function("createSession", &DrillsEngine::createSession)
        .function("insertFact", &DrillsEngine::insertFact)
        .function("fireRules", &DrillsEngine::fireRules)
        .function("query", &DrillsEngine::query)
        .function("getStatus", &DrillsEngine::getStatus)
        .function("cleanup", &DrillsEngine::cleanup);
}

// Export free functions
EMSCRIPTEN_BINDINGS(drills_functions) {
    function("getVersion", []() { return "WebAssembly Build 1.0.0"; });
}

// Entry point for the module
int main() {
    // WebAssembly modules need a main function, even if empty
    return 0;
}
