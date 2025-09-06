#include "rule_forge.h"

#include <iostream>
#include <string>
#include <vector>

// Helper to print errors
void print_error_and_exit(const std::string& message) {
    std::cerr << "Error: " << message << std::endl;
    const char* last_err = ruleforge_get_last_error_message();
    if (last_err && std::strlen(last_err) > 0) {
        std::cerr << "Last API Error: " << last_err << std::endl;
    }
    ruleforge_cleanup();
    exit(EXIT_FAILURE);
}

int main() {
    std::cout << "Initializing RuleForge CAPI..." << std::endl;
    if (ruleforge_init() != DRILLS_OK) {
        print_error_and_exit("Failed to initialize RuleForge API.");
    }

    ruleforge_knowledge_base_t kb = nullptr;
    ruleforge_stateful_session_t session = nullptr;
    ruleforge_query_result_t query_result = nullptr;

    // 1. Create Knowledge Base
    std::cout << "Creating Knowledge Base..." << std::endl;
    if (ruleforge_kb_create(&kb) != DRILLS_OK) {
        print_error_and_exit("Failed to create Knowledge Base.");
    }

    // 2. Load DRL rules
    std::cout << "Loading DRL rules..." << std::endl;
    const char* simple_drl = R"(
rule "AdultPersonRule"
    when
        $p : Person(age > 18)
    then
        // No action, just for demonstration
end

query "AdultPersonsQuery"
    $p : Person(age > 18)
end
)";
    if (ruleforge_kb_load_drl(kb, simple_drl) != DRILLS_OK) {
        print_error_and_exit("Failed to load DRL rules.");
    }

    // 3. Create Stateful Session
    std::cout << "Creating Stateful Session..." << std::endl;
    if (ruleforge_session_create(kb, &session) != DRILLS_OK) {
        print_error_and_exit("Failed to create Stateful Session.");
    }

    // 4. Add Facts
    std::cout << "Adding facts..." << std::endl;
    if (ruleforge_session_add_fact_json(session, "Person", R"({"name": "Alice", "age": 25})") != DRILLS_OK) {
        print_error_and_exit("Failed to add fact Alice.");
    }
    if (ruleforge_session_add_fact_json(session, "Person", R"({"name": "Bob", "age": 17})") != DRILLS_OK) {
        print_error_and_exit("Failed to add fact Bob.");
    }
    if (ruleforge_session_add_fact_json(session, "Person", R"({"name": "Charlie", "age": 30})") != DRILLS_OK) {
        print_error_and_exit("Failed to add fact Charlie.");
    }

    // 5. Fire All Rules
    std::cout << "Firing all rules..." << std::endl;
    if (ruleforge_session_fire_all_rules(session) != DRILLS_OK) {
        print_error_and_exit("Failed to fire all rules.");
    }

    // 6. Execute Query
    std::cout << "Executing query 'AdultPersonsQuery'..." << std::endl;
    if (ruleforge_session_query(session, "AdultPersonsQuery", &query_result) != DRILLS_OK) {
        print_error_and_exit("Failed to execute query.");
    }

    // 7. Process Query Results
    int result_count = ruleforge_query_result_get_size(query_result);
    std::cout << "Query returned " << result_count << " adult persons." << std::endl;

    for (int i = 0; i < result_count; ++i) {
        ruleforge_fact_t person_fact = nullptr;
        if (ruleforge_query_result_get_fact_at_index(query_result, i, "p", &person_fact) != DRILLS_OK) {
            print_error_and_exit("Failed to get fact from query result.");
        }

        char name_buffer[100];
        size_t actual_length = 0;
        if (ruleforge_fact_get_field_as_string(person_fact, "name", name_buffer, sizeof(name_buffer), &actual_length) != DRILLS_OK) {
            print_error_and_exit("Failed to get name field.");
        }

        int64_t age_val = 0;
        if (ruleforge_fact_get_field_as_int(person_fact, "age", &age_val) != DRILLS_OK) {
            print_error_and_exit("Failed to get age field.");
        }
        std::cout << "  - Name: " << name_buffer << ", Age: " << age_val << std::endl;
    }

    // 8. Cleanup
    std::cout << "Cleaning up resources..." << std::endl;
    if (ruleforge_query_result_destroy(query_result) != DRILLS_OK) {
        std::cerr << "Warning: Failed to destroy query result." << std::endl;
    }
    if (ruleforge_session_destroy(session) != DRILLS_OK) {
        std::cerr << "Warning: Failed to destroy session." << std::endl;
    }
    if (ruleforge_kb_destroy(kb) != DRILLS_OK) {
        std::cerr << "Warning: Failed to destroy knowledge base." << std::endl;
    }
    if (ruleforge_cleanup() != DRILLS_OK) {
        std::cerr << "Warning: Failed to cleanup RuleForge API." << std::endl;
    }

    std::cout << "RuleForge CAPI example finished successfully." << std::endl;
    return 0;
}
