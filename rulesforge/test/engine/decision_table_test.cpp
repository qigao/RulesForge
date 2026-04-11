#include <filesystem>
#include <fstream>
#include <iostream>

#include "decision_table_converter.hpp"
#include "decision_table_parser.hpp"
#include "rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/query_result.hpp"
#include "engine/stateful_session.hpp"
#include "test_helpers.hpp"
#include "tinytest.h"

struct DecisionTableTestFixture {
    std::string const test_dir = "dt_test_files";
    std::string csv_path;
    std::shared_ptr<KnowledgeBase> kb;

    DecisionTableTestFixture() {
        if (std::filesystem::exists(test_dir)) {
            std::filesystem::remove_all(test_dir);
        }
        std::filesystem::create_directory(test_dir);

        // CSV format: fields with quotes/commas must be quoted, internal quotes doubled
        std::string const csv_content =
            "PACKAGE,com.example.dt\n"
            "DECLARE,Customer,\"name: String, balance: double, status: String\"\n"
            "DECLARE,Offer,\"message: String\"\n"
            "QUERY,find_offers,$o: Offer()\n"
            ",CONDITION: Customer(balance > $1),\"CONDITION: Customer(status == \"\"$1\"\")\",\"ACTION: insert Offer { message = '$1' }\",Salience\n"
            "High Balance Offer,5000,*,You get a high balance offer!,10\n"
            "Gold Status Offer,*,GOLD,You get a gold status offer!,20\n";

        csv_path = (std::filesystem::path(test_dir) / "test_table.csv").string();

        {
            std::ofstream out(csv_path);
            if (!out.is_open()) { throw std::runtime_error("Failed to open file: " + csv_path); }
            out << csv_content;
        }

        {
            ParsingResult parse_result;
            DecisionTable table = DecisionTableParser::parse(csv_path, parse_result);
            DecisionTableConverter converter(std::move(table));
            std::string drl = converter.generate_drl();
            std::cerr << "=== GENERATED RFL ===\n" << drl << "\n=== END RFL ===" << std::endl;
        }

        ParsingResult result;
        kb = build_knowledge_base_from_csv(csv_path, result);

        if (!result.success) throw_parse_failure(result);
        if (!kb) { throw std::runtime_error("KnowledgeBase is null"); }

        std::cerr << "=== PARSED RULES ===" << std::endl;
        for (auto const& rule : kb->get_rules()) {
            std::cerr << "Rule: " << rule.name << ", salience: " << rule.salience << std::endl;
            std::cerr << "  condition_groups: " << rule.condition_groups.size() << std::endl;
            for (size_t gi = 0; gi < rule.condition_groups.size(); ++gi) {
                auto const& group = rule.condition_groups[gi];
                std::cerr << "    Group " << gi << ": " << group.size() << " patterns" << std::endl;
                for (size_t pi = 0; pi < group.size(); ++pi) {
                    auto const& pattern = group[pi];
                    std::cerr << "      Pattern " << pi << ": type=" << pattern.fact_type << std::endl;
                    if (pattern.constraint_root) {
                        std::cerr << "        Has constraint_root" << std::endl;
                    } else {
                        std::cerr << "        NO constraint_root!" << std::endl;
                    }
                }
            }
        }
        std::cerr << "=== END PARSED RULES ===" << std::endl;
    }

    ~DecisionTableTestFixture() { std::filesystem::remove_all(test_dir); }
};

suite("Decision Table") {
    group("End-to-End Test") {
        it("triggers rule for high balance customer") {
            DecisionTableTestFixture fixture;

            std::cerr << "=== CHECKING CONDITION GROUPS ===" << std::endl;
            for (auto const& rule : fixture.kb->get_rules()) {
                bool has_empty = rule.condition_groups.empty() ||
                                (rule.condition_groups.size() == 1 && rule.condition_groups[0].empty());
                std::cerr << "Rule '" << rule.name << "': "
                          << (has_empty ? "EMPTY CONDITIONS (immediate fire!)" : "has conditions")
                          << std::endl;
            }
            std::cerr << "=== END CHECK ===" << std::endl;

            auto session = fixture.kb->create_session();
            auto cust = std::make_shared<Fact>();
            cust->type = "com.example.dt.Customer";
            cust->fields = {
                {"name", "Alice"}, {"balance", 6000.0}, {"status", "SILVER"}};
            session->add_fact(cust);

            int fired = session->fire_all_rules();
            check(fired == 1);

            QueryResult query_result = session->execute_query("find_offers");
            check(query_result.size() == 1);
            auto msg = query_result.single().getFieldAs<std::string>("$o", "message");
            check(msg.has_value());
            check(msg.value() == "You get a high balance offer!");
        }

        it("triggers rule for gold status customer") {
            DecisionTableTestFixture fixture;

            auto session = fixture.kb->create_session();
            auto cust = std::make_shared<Fact>();
            cust->type = "com.example.dt.Customer";
            cust->fields = {{"name", "Bob"}, {"balance", 1000.0}, {"status", "GOLD"}};
            session->add_fact(cust);

            int fired = session->fire_all_rules();
            check(fired == 1);

            QueryResult query_result = session->execute_query("find_offers");
            check(query_result.size() == 1);
            auto msg = query_result.single().getFieldAs<std::string>("$o", "message");
            check(msg.has_value());
            check(msg.value() == "You get a gold status offer!");
        }

        it("triggers both rules for gold status customer with high balance") {
            DecisionTableTestFixture fixture;

            auto session = fixture.kb->create_session();
            auto cust = std::make_shared<Fact>();
            cust->type = "com.example.dt.Customer";
            cust->fields = {
                {"name", "Charlie"}, {"balance", 9000.0}, {"status", "GOLD"}};
            session->add_fact(cust);

            int fired = session->fire_all_rules();
            check(fired == 2);

            QueryResult query_result = session->execute_query("find_offers");
            check(query_result.size() == 2);
        }
    }
}
