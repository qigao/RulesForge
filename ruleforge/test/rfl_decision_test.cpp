#include <filesystem>
#include <fstream>
#include <iostream>

#include "catch2/catch_all.hpp"
#include "decision_table_converter.hpp"
#include "decision_table_parser.hpp"
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "query_result.hpp"
#include "stateful_session.hpp"

struct DecisionTableTestFixture
{
  std::string const test_dir = "dt_test_files";
  std::string csv_path;
  std::shared_ptr<KnowledgeBase> kb;

  DecisionTableTestFixture()
  {
    if (std::filesystem::exists(test_dir)) {
      std::filesystem::remove_all(test_dir);
    }
    std::filesystem::create_directory(test_dir);

    std::string const csv_content = "PACKAGE,com.example.dt\n"
                                        "DECLARE,Customer,\"name: String, balance: double, status: String\"\n"
                                        "DECLARE,Offer,\"message: String\"\n"
                                        "QUERY,find_offers,$o: Offer()\n"
                                        ",CONDITION: Customer(balance > $1),CONDITION: Customer(status == "
                                        "\"$1\"),\"ACTION: rfl.insert({type: 'Offer', message: '$1'})\","
                                        "Salience\n"
                                        "\"High Balance Offer\",5000,*,You get a high balance offer!,10\n"
                                        "\"Gold Status Offer\",*,GOLD,You get a gold status offer!,20\n";

    csv_path = (std::filesystem::path(test_dir) / "test_table.csv").string();

    {
      std::ofstream out(csv_path);
      REQUIRE(out.is_open());
      out << csv_content;
    }

    // Debug: Print the generated RFL
    {
      ParsingResult parse_result;
      DecisionTable table = DecisionTableParser::parse(csv_path, parse_result);
      DecisionTableConverter converter(std::move(table));
      std::string drl = converter.generate_drl();
      std::cerr << "=== GENERATED RFL ===\n" << drl << "\n=== END RFL ===" << std::endl;
    }

    ParsingResult result;
    kb = build_knowledge_base_from_csv(csv_path, result);

    if (!result.success) {
      for (auto const& err : result.errors) {
        FAIL(err.to_string());
      }
    }
    REQUIRE(result.success);
    REQUIRE(kb != nullptr);

    // Debug: Print parsed rules and their conditions
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

TEST_CASE_METHOD(DecisionTableTestFixture,
                 "Decision Table: End-to-End Test",
                 "[decisiontable]")
{
  // Debug: Check if rules have empty condition groups
  std::cerr << "=== CHECKING CONDITION GROUPS ===" << std::endl;
  for (auto const& rule : kb->get_rules()) {
    bool has_empty = rule.condition_groups.empty() ||
                    (rule.condition_groups.size() == 1 && rule.condition_groups[0].empty());
    std::cerr << "Rule '" << rule.name << "': "
              << (has_empty ? "EMPTY CONDITIONS (immediate fire!)" : "has conditions")
              << std::endl;
  }
  std::cerr << "=== END CHECK ===" << std::endl;

  SECTION("High balance customer should trigger first rule")
  {
    auto session = kb->create_session();
    auto cust = std::make_shared<Fact>();
    cust->type = "com.example.dt.Customer";
    cust->fields = {
        {"name", "Alice"}, {"balance", 6000.0}, {"status", "SILVER"}};
    session->add_fact(cust);

    int fired = session->fire_all_rules();
    REQUIRE(fired == 1);

    QueryResult query_result = session->execute_query("find_offers");
    REQUIRE(query_result.size() == 1);
    auto msg = query_result.single().getFieldAs<std::string>("$o", "message");
    REQUIRE(msg.has_value());
    CHECK(msg.value() == "You get a high balance offer!");
  }

  SECTION("Gold status customer should trigger second rule")
  {
    auto session = kb->create_session();
    auto cust = std::make_shared<Fact>();
    cust->type = "com.example.dt.Customer";
    cust->fields = {{"name", "Bob"}, {"balance", 1000.0}, {"status", "GOLD"}};
    session->add_fact(cust);

    int fired = session->fire_all_rules();
    REQUIRE(fired == 1);

    QueryResult query_result = session->execute_query("find_offers");
    REQUIRE(query_result.size() == 1);
    auto msg = query_result.single().getFieldAs<std::string>("$o", "message");
    REQUIRE(msg.has_value());
    CHECK(msg.value() == "You get a gold status offer!");
  }

  SECTION("Gold status customer with high balance should trigger both")
  {
    auto session = kb->create_session();
    auto cust = std::make_shared<Fact>();
    cust->type = "com.example.dt.Customer";
    cust->fields = {
        {"name", "Charlie"}, {"balance", 9000.0}, {"status", "GOLD"}};
    session->add_fact(cust);

    int fired = session->fire_all_rules();
    REQUIRE(fired == 2);

    QueryResult query_result = session->execute_query("find_offers");
    REQUIRE(query_result.size() == 2);
  }
}


