#include "parser/rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "tinytest.h"
#include <filesystem>
#include <fstream>

#ifndef RFL_TEST_DATA_DIR
#define RFL_TEST_DATA_DIR "test"
#endif

static std::string test_path(std::string const& relative) {
    return (std::filesystem::path(RFL_TEST_DATA_DIR) / relative).string();
}

suite("Engine Multi-File Loading") {
    group("Multi-File Loading (API)") {
        it("loads multiple files") {
            std::vector<std::string> files = {
                test_path("test_file1.rfl"),
                test_path("test_file2.rfl")
            };

            ParsingResult result;
            auto kb = build_knowledge_base(files, {}, result);


            check(result.success);
            check(kb != nullptr);

            auto session = kb->create_session();
            check(session != nullptr);

            auto factA = std::make_shared<Fact>();
            factA->type = "test.multi.FactA";
            factA->fields["value"] = "Hello from A";
            session->add_fact(factA);

            auto factB = std::make_shared<Fact>();
            factB->type = "test.multi.FactB";
            factB->fields["value"] = (int64_t)42;
            session->add_fact(factB);

            int fired = session->fire_all_rules();
            check(fired == 3);
        }
    }

    group("Import Statement") {
        it("resolves imports correctly") {
            std::string rules_file = test_path("multi/rules.rfl");
            std::string base = std::filesystem::path(RFL_TEST_DATA_DIR).parent_path().string();
            std::vector<std::string> base_dirs = {base};

            ParsingResult result;
            auto kb = build_knowledge_base(rules_file, base_dirs, result);

            check(result.success);
            check(kb != nullptr);

            auto session = kb->create_session();
            check(session != nullptr);

            auto factA = std::make_shared<Fact>();
            factA->type = "test.multi.FactA";
            factA->fields["value"] = "Hello from A";
            session->add_fact(factA);

            auto factB = std::make_shared<Fact>();
            factB->type = "test.multi.FactB";
            factB->fields["value"] = (int64_t)42;
            session->add_fact(factB);

            int fired = session->fire_all_rules();
            check(fired == 3);
        }
    }

    group("Wildcard Import") {
        it("imports all files from package") {
            std::string drl_content = R"(
package test.wildcard

import test.multi.*

rule "Use Imported Types"
when
    $a : FactA()
    $b : FactB()
then
    // Both facts present
end
)";

            std::filesystem::path base = std::filesystem::path(RFL_TEST_DATA_DIR).parent_path();
            std::filesystem::path temp_dir = base / "test" / "wildcard";
            std::filesystem::create_directories(temp_dir);
            std::filesystem::path temp_file = temp_dir / "main.rfl";
            {
                std::ofstream ofs(temp_file);
                ofs << drl_content;
            }

            std::vector<std::string> base_dirs = {base.string()};

            ParsingResult result;
            auto kb = build_knowledge_base(temp_file.string(), base_dirs, result);

            check(result.success);
            check(kb != nullptr);

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
            check(fired >= 1);

            std::filesystem::remove(temp_file);
            std::filesystem::remove(temp_dir);
        }
    }
}
