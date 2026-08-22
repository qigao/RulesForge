// import_test.cpp
// Verifies that the raw Lemon parser (rfl_parse_lemon) correctly captures
// import statements, package declarations, and type declarations from
// individual source strings.
//
// The tests deliberately do NOT exercise multi-file import resolution or
// KnowledgeBase construction — those higher-level concerns belong to
// integration tests.  Here we only assert that the grammar recognises the
// import-related syntax and populates parser_state correctly.

#include "rfl_parser_impl.hpp"    // rfl_parse_lemon
#include "core/rfl_parser_state.hpp"
#include "core/errors.hpp"
#include "tinytest.hpp"

#include <algorithm>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static parser_state parse_success(std::string const& drl,
                                  std::string const& source = "test.rfl") {
    std::vector<StructuredError> errors;
    parser_state state = rfl_parse_lemon(drl, source, errors);
    if (!errors.empty()) {
        std::string msg = "Parse failed:\n";
        for (auto const& e : errors) msg += "  " + e.to_string() + "\n";
        throw std::runtime_error(msg);
    }
    return state;
}

static bool has_import(parser_state const& state, std::string const& import_path) {
    return std::any_of(state.parsed_imports.begin(), state.parsed_imports.end(),
                       [&](std::string const& s) { return s == import_path; });
}

static bool has_declaration(parser_state const& state, std::string const& type_name) {
    return std::any_of(state.parsed_declarations.begin(), state.parsed_declarations.end(),
                       [&](ParsedDeclaration const& d) { return d.type_name == type_name; });
}

// ---------------------------------------------------------------------------
// Suite
// ---------------------------------------------------------------------------

suite("Parser Import Syntax") {
    // ------------------------------------------------------------------
    group("Package Declaration") {
        it("captures package name") {
            auto state = parse_success(R"(
                package com.example.main;
            )");
            check(state.package_name == "com.example.main");
        }

        it("captures empty package name when omitted") {
            auto state = parse_success(R"(
                declare Foo end
            )");
            check(state.package_name.empty());
        }
    }

    // ------------------------------------------------------------------
    group("Import Statements") {
        it("captures a single named import") {
            auto state = parse_success(R"(
                package com.example;
                import com.example.model.Customer;
            )");
            check(state.parsed_imports.size() == 1);
            check(has_import(state, "com.example.model.Customer"));
        }

        it("captures multiple named imports") {
            auto state = parse_success(R"(
                package com.example.main;
                import com.example.model.Customer;
                import com.example.model.HighValueCustomer;
            )");
            check(state.parsed_imports.size() == 2);
            check(has_import(state, "com.example.model.Customer"));
            check(has_import(state, "com.example.model.HighValueCustomer"));
        }

        it("captures a wildcard import") {
            auto state = parse_success(R"(
                package com.example.main;
                import com.example.rules.*;
            )");
            check(state.parsed_imports.size() == 1);
            check(has_import(state, "com.example.rules.*"));
        }

        it("captures a mix of named and wildcard imports") {
            auto state = parse_success(R"(
                package com.example.main;
                import com.example.model.Customer;
                import com.example.model.HighValueCustomer;
                import com.example.rules.*;
            )");
            check(state.parsed_imports.size() == 3);
            check(has_import(state, "com.example.model.Customer"));
            check(has_import(state, "com.example.model.HighValueCustomer"));
            check(has_import(state, "com.example.rules.*"));
        }
    }

    // ------------------------------------------------------------------
    group("Declarations in files with packages") {
        it("captures declarations alongside package and imports") {
            // This mirrors what a typical 'model' file would look like.
            auto state = parse_success(R"(
                package com.example.model;

                declare Customer
                    name: String,
                    status: String
                end

                declare HighValueCustomer
                end
            )");
            check(state.package_name == "com.example.model");
            check(state.parsed_declarations.size() == 2);
            // Raw parser stores the simple (unqualified) type name.
            // The package prefix is applied by a later stage (semantic analysis).
            check(has_declaration(state, "Customer"));
            check(has_declaration(state, "HighValueCustomer"));
        }

        it("captures rules that reference fully-qualified types in the from-clause") {
            // Mirrors a 'rules' file that uses FQN because it doesn't import the model.
            auto state = parse_success(R"(
                package com.example.rules;

                rule "Base Loyalty Rule"
                when
                    $base_cust: com.example.model.Customer()
                then
                end
            )");
            check(state.package_name == "com.example.rules");
            check(state.parsed_rules.size() == 1);
            check(state.parsed_rules[0].name == "Base Loyalty Rule");
            // The FQN should be stored verbatim as the fact_type before type resolution.
            check(state.parsed_rules[0].condition_groups[0][0].fact_type == "com.example.model.Customer");
        }
    }

    // ------------------------------------------------------------------
    group("Rule with 'extends'") {
        it("captures the parent rule name in a rule that uses 'extends'") {
            auto state = parse_success(R"(
                package com.example.main;
                import com.example.model.Customer;
                import com.example.model.HighValueCustomer;
                import com.example.rules.*;

                rule "Main Rule" extends "Base Loyalty Rule"
                when
                    $c: Customer(status == "GOLD")
                then
                    insert HighValueCustomer { }
                end
            )");
            check(state.parsed_rules.size() == 1);
            auto const& rule = state.parsed_rules[0];
            check(rule.name == "Main Rule");
            check(rule.parent_rule_name.has_value());
            check(rule.parent_rule_name.value() == "Base Loyalty Rule");
        }
    }
}
