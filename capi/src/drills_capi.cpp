// Include the header file to get type definitions
#include "drills_capi.h"

// Include C string handling
#include <cstring>

// Include C++ backend
#include "knowledge_base.hpp"
#include "stateful_session.hpp"
#include "drools_parser.hpp"
#include "fact_builder.hpp"
#include "query_result.hpp"
#include "errors.hpp"

// Include JSON parsing
#include <jsoncons/json.hpp>

// Simple implementation for core C API functions
extern "C" {

// Basic error handling
static char last_error[1024] = "";

drills_status_t drills_init() {
    last_error[0] = '\0';
    return DRILLS_OK;
}

drills_status_t drills_cleanup() {
    last_error[0] = '\0';
    return DRILLS_OK;
}

const char* drills_get_last_error_message() {
    return last_error;
}

// Knowledge Base functions
// Helper function for crossing C++/pure C boundaries safely
struct KnowledgeBaseWrapper {
    std::shared_ptr<KnowledgeBase> kb;
};

drills_status_t drills_kb_create(drills_knowledge_base_t* out_kb) {
    if (!out_kb) {
        strcpy(last_error, "Output Knowledge Base pointer is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    try {
        // Create empty knowledge base
        parser_state empty_state;
        auto kb_wrapper = new KnowledgeBaseWrapper();
        kb_wrapper->kb = KnowledgeBase::create(empty_state);
        *out_kb = static_cast<drills_knowledge_base_t>(kb_wrapper);
        last_error[0] = '\0';
        return DRILLS_OK;
    }
    catch (const std::exception& e) {
        strcpy(last_error, std::string("Failed to create Knowledge Base: ").append(e.what()).c_str());
        return DRILLS_ERROR_GENERIC;
    }
}

drills_status_t drills_kb_load_drl(drills_knowledge_base_t kb, const char* drl_source) {
    if (!kb) {
        strcpy(last_error, "Knowledge Base handle is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    if (!drl_source) {
        strcpy(last_error, "DRL source is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    try {
        ParsingResult result;
        auto kb_wrapper = static_cast<KnowledgeBaseWrapper*>(kb);

        // Use the real DRL parser
        kb_wrapper->kb = build_knowledge_base(drl_source, result, "C_API_Source");

        if (!result.success) {
            std::string error_msg = "DRL compilation failed: ";
            for (const auto& err : result.errors) {
                error_msg += err.to_string() + "; ";
            }
            strcpy(last_error, error_msg.c_str());
            return DRILLS_ERROR_COMPILATION_FAILED;
        }

        last_error[0] = '\0';
        return DRILLS_OK;
    }
    catch (const std::exception& e) {
        strcpy(last_error, std::string("Failed to load DRL: ").append(e.what()).c_str());
        return DRILLS_ERROR_GENERIC;
    }
}

drills_status_t drills_kb_load_decision_table_csv(drills_knowledge_base_t kb, const char* csv_source) {
    if (!kb) {
        strcpy(last_error, "Knowledge Base handle is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    if (!csv_source) {
        strcpy(last_error, "CSV source is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    // For now, stub - would need to implement CSV parsing
    try {
        ParsingResult result;
        auto kb_wrapper = static_cast<KnowledgeBaseWrapper*>(kb);

        // Simple placeholder - in real implementation would parse CSV and create DRL
        // kb_wrapper->kb = build_knowledge_base_from_csv_string(csv_source, result);

        if (!result.success) {
            std::string error_msg = "Decision table compilation failed: ";
            for (const auto& err : result.errors) {
                error_msg += err.to_string() + "; ";
            }
            strcpy(last_error, error_msg.c_str());
            return DRILLS_ERROR_COMPILATION_FAILED;
        }

        last_error[0] = '\0';
        return DRILLS_OK;
    }
    catch (const std::exception& e) {
        strcpy(last_error, std::string("Failed to load CSV: ").append(e.what()).c_str());
        return DRILLS_ERROR_GENERIC;
    }
}

drills_status_t drills_kb_destroy(drills_knowledge_base_t kb) {
    if (!kb) {
        strcpy(last_error, "Knowledge Base handle is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    try {
        auto kb_wrapper = static_cast<KnowledgeBaseWrapper*>(kb);
        delete kb_wrapper;
        last_error[0] = '\0';
        return DRILLS_OK;
    }
    catch (const std::exception& e) {
        strcpy(last_error, std::string("Failed to destroy Knowledge Base: ").append(e.what()).c_str());
        return DRILLS_ERROR_GENERIC;
    }
}

// Session wrapper for safe C++/C interop
struct StatefulSessionWrapper {
    std::unique_ptr<StatefulSession> session;
};

drills_status_t drills_session_create(drills_knowledge_base_t kb, drills_stateful_session_t* out_session) {
    if (!kb) {
        strcpy(last_error, "Knowledge Base handle is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    if (!out_session) {
        strcpy(last_error, "Output Session pointer is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    try {
        auto kb_wrapper = static_cast<KnowledgeBaseWrapper*>(kb);
        auto session_wrapper = new StatefulSessionWrapper();
        session_wrapper->session = kb_wrapper->kb->create_session();
        *out_session = static_cast<drills_stateful_session_t>(session_wrapper);
        last_error[0] = '\0';
        return DRILLS_OK;
    }
    catch (const std::exception& e) {
        strcpy(last_error, std::string("Failed to create session: ").append(e.what()).c_str());
        return DRILLS_ERROR_SESSION_CREATION_FAILED;
    }
}

// QueryResult wrapper for safe C++/C interop
struct QueryResultWrapper {
    std::unique_ptr<QueryResult> query_result;
};

drills_status_t drills_session_add_fact_json(drills_stateful_session_t session, const char* fact_type, const char* fact_json) {
    if (!session || !fact_type || !fact_json) {
        strcpy(last_error, "Session handle, fact type, or fact JSON is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    try {
        auto session_wrapper = static_cast<StatefulSessionWrapper*>(session);

        // Parse JSON and create fact using FactBuilder
        auto builder = FactBuilder::create(fact_type);
        jsoncons::json j = jsoncons::json::parse(fact_json);

        for (auto const& member : j.object_range()) {
            const std::string& key = member.key();
            const jsoncons::json& value = member.value();

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
        return DRILLS_OK;
    }
    catch (const jsoncons::json_exception& e) {
        strcpy(last_error, std::string("JSON parsing failed: ").append(e.what()).c_str());
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    catch (const std::exception& e) {
        strcpy(last_error, std::string("Failed to add fact: ").append(e.what()).c_str());
        return DRILLS_ERROR_FACT_INSERTION_FAILED;
    }
}

drills_status_t drills_session_fire_all_rules(drills_stateful_session_t session) {
    if (!session) {
        strcpy(last_error, "Session handle is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    try {
        auto session_wrapper = static_cast<StatefulSessionWrapper*>(session);
        int fired_rules = session_wrapper->session->fire_all_rules();
        last_error[0] = '\0';
        return DRILLS_OK;
    }
    catch (const std::exception& e) {
        strcpy(last_error, std::string("Failed to fire rules: ").append(e.what()).c_str());
        return DRILLS_ERROR_GENERIC;
    }
}

drills_status_t drills_session_query(drills_stateful_session_t session, const char* query_name, drills_query_result_t* out_query_result) {
    if (!session) {
        strcpy(last_error, "Session handle is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    if (!query_name) {
        strcpy(last_error, "Query name is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    if (!out_query_result) {
        strcpy(last_error, "Output Query Result pointer is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    try {
        auto session_wrapper = static_cast<StatefulSessionWrapper*>(session);

        // Execute query
        QueryResult query_result = session_wrapper->session->execute_query(query_name);

        // Wrap in QueryResultWrapper for safe C interop
        auto result_wrapper = new QueryResultWrapper();
        result_wrapper->query_result = std::make_unique<QueryResult>(query_result);

        *out_query_result = static_cast<drills_query_result_t>(result_wrapper);
        last_error[0] = '\0';
        return DRILLS_OK;
    }
    catch (const std::exception& e) {
        strcpy(last_error, std::string("Failed to execute query: ").append(e.what()).c_str());
        return DRILLS_ERROR_QUERY_FAILED;
    }
}

// Fact field access functions need proper implementation
// For now, stub implementation for basic functionality

drills_status_t drills_session_destroy(drills_stateful_session_t session) {
    if (!session) {
        strcpy(last_error, "Session handle is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    try {
        auto session_wrapper = static_cast<StatefulSessionWrapper*>(session);
        delete session_wrapper;
        last_error[0] = '\0';
        return DRILLS_OK;
    }
    catch (const std::exception& e) {
        strcpy(last_error, std::string("Failed to destroy session: ").append(e.what()).c_str());
        return DRILLS_ERROR_GENERIC;
    }
}

// Query result functions
int drills_query_result_get_size(drills_query_result_t query_result) {
    if (!query_result) {
        strcpy(last_error, "Query Result handle is NULL");
        return -1;
    }
    try {
        auto result_wrapper = static_cast<QueryResultWrapper*>(query_result);
        last_error[0] = '\0';
        return static_cast<int>(result_wrapper->query_result->size());
    }
    catch (const std::exception& e) {
        strcpy(last_error, std::string("Failed to get query result size: ").append(e.what()).c_str());
        return -1;
    }
}

drills_status_t drills_query_result_get_fact_at_index(drills_query_result_t query_result, int row_index, const char* binding_name, drills_fact_t* out_fact) {
    if (!query_result || !binding_name || !out_fact) {
        strcpy(last_error, "Query Result handle, binding name, or output Fact pointer is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    if (row_index < 0) {
        strcpy(last_error, "Row index is negative");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    try {
        auto result_wrapper = static_cast<QueryResultWrapper*>(query_result);

        if (row_index >= static_cast<int>(result_wrapper->query_result->size())) {
            strcpy(last_error, "Row index out of bounds");
            return DRILLS_ERROR_INVALID_ARGUMENT;
        }

        // Use iterator to access row at index
        auto it = result_wrapper->query_result->begin();
        std::advance(it, row_index);

        // Get the fact from the query result row
        auto fact_opt = (*it).get(std::string(binding_name));
        if (!fact_opt) {
            strcpy(last_error, "Binding name not found");
            return DRILLS_ERROR_INVALID_ARGUMENT;
        }

        // Return the raw pointer - ownership remains with QueryResult
        *out_fact = static_cast<drills_fact_t>(fact_opt->get());
        last_error[0] = '\0';
        return DRILLS_OK;
    }
    catch (const std::exception& e) {
        strcpy(last_error, std::string("Failed to get fact: ").append(e.what()).c_str());
        return DRILLS_ERROR_GENERIC;
    }
}

drills_status_t drills_query_result_destroy(drills_query_result_t query_result) {
    if (!query_result) {
        strcpy(last_error, "Query Result handle is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    try {
        auto result_wrapper = static_cast<QueryResultWrapper*>(query_result);
        delete result_wrapper;
        last_error[0] = '\0';
        return DRILLS_OK;
    }
    catch (const std::exception& e) {
        strcpy(last_error, std::string("Failed to destroy query result: ").append(e.what()).c_str());
        return DRILLS_ERROR_GENERIC;
    }
}

// Fact functions
drills_status_t drills_fact_get_field_as_string(drills_fact_t fact, const char* field_name, char* buffer, size_t buffer_size, size_t* out_actual_length) {
    if (!fact || !field_name || !buffer || !out_actual_length) {
        strcpy(last_error, "Fact handle, field name, buffer, or actual length pointer is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    strcpy(last_error, "Stub implementation - no actual field extraction");
    return DRILLS_ERROR_GENERIC;
}

drills_status_t drills_fact_get_field_as_double(drills_fact_t fact, const char* field_name, double* out_value) {
    if (!fact || !field_name || !out_value) {
        strcpy(last_error, "Fact handle, field name, or output value pointer is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    strcpy(last_error, "Stub implementation - no actual field extraction");
    return DRILLS_ERROR_GENERIC;
}

drills_status_t drills_fact_get_field_as_int(drills_fact_t fact, const char* field_name, int64_t* out_value) {
    if (!fact || !field_name || !out_value) {
        strcpy(last_error, "Fact handle, field name, or output value pointer is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    strcpy(last_error, "Stub implementation - no actual field extraction");
    return DRILLS_ERROR_GENERIC;
}

drills_status_t drills_fact_get_field_as_bool(drills_fact_t fact, const char* field_name, int* out_value) {
    if (!fact || !field_name || !out_value) {
        strcpy(last_error, "Fact handle, field name, or output value pointer is NULL");
        return DRILLS_ERROR_INVALID_ARGUMENT;
    }
    strcpy(last_error, "Stub implementation - no actual field extraction");
    return DRILLS_ERROR_GENERIC;
}

} // extern "C"
