#include "tinytest.h"
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

struct ImportTestFixture {
    std::string const test_dir = "import_test_files";

    ImportTestFixture() {
        if (std::filesystem::exists(test_dir)) { std::filesystem::remove_all(test_dir); }
        std::filesystem::create_directory(test_dir);
    }

    ~ImportTestFixture() {
        std::filesystem::remove_all(test_dir);
    }

    void write_file(std::string const& relative_path, std::string const& content) {
        std::filesystem::path full_path = std::filesystem::path(test_dir) / relative_path;

        if (full_path.has_parent_path()) { std::filesystem::create_directories(full_path.parent_path()); }

        std::ofstream out(full_path);
        if (!out.is_open()) { throw std::runtime_error("Failed to open file: " + full_path.string()); }
        out << content;
    }
};

suite("Parser Import Resolution") {
    it("resolves imports correctly") {
        ImportTestFixture fixture;

        std::string const main_drl = R"(
            package com.example.main;

            // Import a specific type and a whole package
            import com.example.model.Customer;
            import com.example.rules.*;

            rule "Main Rule" extends "Base Loyalty Rule"
            when
                $c: Customer(status == "GOLD")
            then
                rfl.insert({type: "com.example.model.HighValueCustomer"});
            end
        )";

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

        fixture.write_file("com/example/main.rfl", main_drl);
        fixture.write_file("com/example/model/Customer.rfl", customer_drl);
        fixture.write_file("com/example/rules/Rules.rfl", base_rule_drl);

        ParsingResult result;
        std::vector<std::string> search_paths = {fixture.test_dir};

        std::shared_ptr<KnowledgeBase> kb =
            build_knowledge_base((std::filesystem::path(fixture.test_dir) / "com/example/main.rfl").string(), search_paths, result);

        if (!result.success) {
            for (auto const& err : result.errors) { check(false, err.to_string().c_str()); }
        }
        check(result.success);
        check(kb != nullptr);

        parser_state const& state = kb->get_parser_state();

        check(state.parsed_declarations.size() == 2);
        auto has_declaration = [&](std::string const& type_name) {
            return std::any_of(state.parsed_declarations.begin(), state.parsed_declarations.end(),
                               [&](ParsedDeclaration const& d) { return d.type_name == type_name; });
        };
        check(has_declaration("com.example.model.Customer"));
        check(has_declaration("com.example.model.HighValueCustomer"));

        auto const& rules = kb->get_rules();
        check(rules.size() == 2);

        auto main_rule_it =
            std::find_if(rules.begin(), rules.end(), [](ParsedRule const& r) { return r.name == "Main Rule"; });

        check(main_rule_it != rules.end());

        check(main_rule_it->condition_groups.size() == 1);
        check(main_rule_it->condition_groups[0].size() == 2);
        check(main_rule_it->condition_groups[0][0].fact_type == "com.example.model.Customer");
        check(main_rule_it->condition_groups[0][1].fact_type == "com.example.model.Customer");
    }
}
