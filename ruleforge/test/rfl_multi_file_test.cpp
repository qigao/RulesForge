#include "catch2/catch_all.hpp"
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"
#include <filesystem>
#include <fstream>

TEST_CASE("Engine: Multi-File Loading (API)", "[engine][multi-file]") {
    std::vector<std::string> files = {
        "test/test_file1.rfl",
        "test/test_file2.rfl"
    };

    ParsingResult result;
    auto kb = build_knowledge_base(files, {}, result);

    for (auto const& err : result.errors) {
        UNSCOPED_INFO(err.to_string());
    }
    REQUIRE(result.success);
    REQUIRE(kb != nullptr);

    auto session = kb->create_session();
    REQUIRE(session != nullptr);

    auto factA = std::make_shared<Fact>();
    factA->type = "test.multi.FactA";
    factA->fields["value"] = "Hello from A";
    session->add_fact(factA);

    auto factB = std::make_shared<Fact>();
    factB->type = "test.multi.FactB";
    factB->fields["value"] = (int64_t)42;
    session->add_fact(factB);

    int fired = session->fire_all_rules();
    // Rule A, Rule B, Cross Reference
    CHECK(fired == 3);
}

TEST_CASE("Engine: Import Statement", "[engine][import]") {
    // Test import syntax: rules.rfl imports common.rfl
    // Directory structure:
    //   test/multi/common.rfl  - shared declarations (FactA, FactB, GetFactA query)
    //   test/multi/rules.rfl   - imports common, defines rules using shared types

    std::string rules_file = "test/multi/rules.rfl";
    std::vector<std::string> base_dirs = {std::filesystem::current_path().string()};

    ParsingResult result;
    auto kb = build_knowledge_base(rules_file, base_dirs, result);

    for (auto const& err : result.errors) {
        UNSCOPED_INFO(err.to_string());
    }
    REQUIRE(result.success);
    REQUIRE(kb != nullptr);

    auto session = kb->create_session();
    REQUIRE(session != nullptr);

    // FactA and FactB are defined in common.rfl, imported by rules.rfl
    auto factA = std::make_shared<Fact>();
    factA->type = "test.multi.FactA";
    factA->fields["value"] = "Hello from A";
    session->add_fact(factA);

    auto factB = std::make_shared<Fact>();
    factB->type = "test.multi.FactB";
    factB->fields["value"] = (int64_t)42;
    session->add_fact(factB);

    int fired = session->fire_all_rules();
    // Rule A, Rule B, Cross Reference (Common Validation skipped - value not empty)
    CHECK(fired == 3);
}

TEST_CASE("Engine: Wildcard Import", "[engine][import]") {
    // Test wildcard import: import test.multi.*
    // This should import all .rfl files in test/multi/ directory

    std::string drl_content = R"(
package test.wildcard

import test.multi.*

rule "Use Imported Types"
when
    $a : FactA()
    $b : FactB()
then
    console.log("Both facts present");
end
)";

    // Write temporary file
    std::filesystem::path temp_dir = std::filesystem::current_path() / "test" / "wildcard";
    std::filesystem::create_directories(temp_dir);
    std::filesystem::path temp_file = temp_dir / "main.rfl";
    {
        std::ofstream ofs(temp_file);
        ofs << drl_content;
    }

    std::vector<std::string> base_dirs = {std::filesystem::current_path().string()};

    ParsingResult result;
    auto kb = build_knowledge_base(temp_file.string(), base_dirs, result);

    for (auto const& err : result.errors) {
        UNSCOPED_INFO(err.to_string());
    }
    REQUIRE(result.success);
    REQUIRE(kb != nullptr);

    auto session = kb->create_session();

    auto factA = std::make_shared<Fact>();
    factA->type = "test.multi.FactA";
    factA->fields["value"] = "Test";
    session->add_fact(factA);

    auto factB = std::make_shared<Fact>();
    factB->type = "test.multi.FactB";
    factB->fields["value"] = (int64_t)1;
    session->add_fact(factB);

    int fired = session->fire_all_rules();
    // "Use Imported Types" + rules from common.rfl and rules.rfl
    CHECK(fired >= 1);

    // Cleanup
    std::filesystem::remove(temp_file);
    std::filesystem::remove(temp_dir);
}
