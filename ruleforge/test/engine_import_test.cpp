#include "catch2/catch_all.hpp"
#include "drools_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

struct ImportTestFixture {
    std::string const test_dir = "import_test_files";

    ImportTestFixture() {
        // Create a unique directory for this test run to avoid conflicts.
        if (std::filesystem::exists(test_dir)) { std::filesystem::remove_all(test_dir); }
        std::filesystem::create_directory(test_dir);
    }

    ~ImportTestFixture() {
        // Clean up the temporary directory and all its contents.
        std::filesystem::remove_all(test_dir);
    }

    // Helper to write content to a file within our temporary test directory.
    void write_file(std::string const& relative_path, std::string const& content) {
        std::filesystem::path full_path = std::filesystem::path(test_dir) / relative_path;

        // Ensure parent directory exists
        if (full_path.has_parent_path()) { std::filesystem::create_directories(full_path.parent_path()); }

        std::ofstream out(full_path);
        REQUIRE(out.is_open());
        out << content;
    }
};

TEST_CASE_METHOD(ImportTestFixture, "Parser: Import Resolution", "[parser][import]") {

    // --- 1. Define the DRL content for each file ---

    // File 1: The main entry point, importing other modules.
    std::string const main_drl = R"(
        package com.example.main;

        // Import a specific type and a whole package
        import com.example.model.Customer;
        import com.example.rules.*;

        rule "Main Rule" extends "Base Loyalty Rule"
        when
            $c: Customer(status == "GOLD")
        then
            drools.insert({type: "com.example.model.HighValueCustomer"});
        end
    )";

    // File 2: A type declaration in its own file.
    std::string const customer_drl = R"(
        package com.example.model;

        declare Customer
            name: String,
            status: String
        end

        // Another type in the same file to show it gets parsed.
        declare HighValueCustomer
        end
    )";

    // File 3: A file containing a base rule to be extended.
    std::string const base_rule_drl = R"(
        package com.example.rules;

        // Note: The type must be fully qualified here because this file
        // does not import com.example.model.Customer itself.
        rule "Base Loyalty Rule"
        when
            $base_cust: com.example.model.Customer()
        then
            // This is an abstract/parent rule, no RHS action.
        end
    )";

    // --- 2. Write the DRL files to disk ---

    // The paths correspond to the package structure.
    write_file("com/example/main.drl", main_drl);
    write_file("com/example/model/Customer.drl", customer_drl);
    write_file("com/example/rules/Rules.drl", base_rule_drl);   // The wildcard import will find this.

    // --- 3. Parse the main file with the correct search path ---

    ParsingResult result;
    // The search path is the root directory containing the package structure.
    std::vector<std::string> search_paths = {test_dir};

    std::shared_ptr<KnowledgeBase> kb =
        build_knowledge_base((std::filesystem::path(test_dir) / "com/example/main.drl").string(), search_paths, result);

    // --- 4. Assertions ---

    // The parse must succeed.
    if (!result.success) {
        for (auto const& err : result.errors) { FAIL(err.to_string()); }
    }
    REQUIRE(result.success);
    REQUIRE(kb != nullptr);

    // Get the final, merged parser state from the knowledge base.
    parser_state const& state = kb->get_parser_state();

    // Check that all declarations from all files were loaded.
    CHECK(state.parsed_declarations.size() == 2);
    // Use a lambda to check for the existence of a type, regardless of order.
    auto has_declaration = [&](std::string const& type_name) {
        return std::any_of(state.parsed_declarations.begin(), state.parsed_declarations.end(),
                           [&](ParsedDeclaration const& d) { return d.type_name == type_name; });
    };
    CHECK(has_declaration("com.example.model.Customer"));
    CHECK(has_declaration("com.example.model.HighValueCustomer"));

    // Check that all rules from all files were loaded and processed.
    // The `processed_rules_` in the KnowledgeBase includes the merged, final rules.
    auto const& rules = kb->get_rules();
    CHECK(rules.size() == 2);   // "Main Rule" and "Base Loyalty Rule"

    auto main_rule_it =
        std::find_if(rules.begin(), rules.end(), [](ParsedRule const& r) { return r.name == "Main Rule"; });

    REQUIRE(main_rule_it != rules.end());

    // Verify that the `extends` logic worked correctly by checking the merged conditions.
    // The "Main Rule" should have its own condition PLUS the one from "Base Loyalty Rule".
    REQUIRE(main_rule_it->condition_groups.size() == 1);
    CHECK(main_rule_it->condition_groups[0].size() == 2);
    CHECK(main_rule_it->condition_groups[0][0].fact_type == "com.example.model.Customer");   // From parent
    CHECK(main_rule_it->condition_groups[0][1].fact_type == "com.example.model.Customer");   // From child
}
