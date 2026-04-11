/**
 * @file test_codec_registry.cpp
 * @brief Test codec registry with KnowledgeBase integration using TinyTest
 */

#include "tinytest.h"
#include "rfl_parser.hpp"
#include "engine/data_source.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> make_truncated_person_binary() {
  std::vector<uint8_t> data;
  data.push_back(4);
  data.push_back(0);
  data.push_back('J');
  data.push_back('o');
  return data;
}

}

suite("Codec Registry") {
  section("JSON to Fact") {
    given("a schema with Person type") {
      char const* rfl = R"(
        declare Person
            name: String
            age: int
            city: String
        end

        rule "Find adults in London"
        when
            $p: Person(age >= 18, city == "London")
        then
            // Adult found
        end
      )";

      when("building knowledge base") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should parse successfully") {
          check(result.success);
          check_not_null(kb.get());
        }

        then("should create session") {
          if (kb) {
            auto session = kb->create_session();
            check_not_null(session.get());

            if (session) {
              // Add fact from JSON
              Fact* fact = session->add_fact_from_json("Person", R"({"name":"John","age":30,"city":"London"})");

              check_not_null(fact);
              if (fact) {
                check(fact->type == "Person");
                check(std::get<std::string>(fact->fields["name"]) == "John");
                check(std::get<int64_t>(fact->fields["age"]) == 30);
              }

              // Fire rules
              int fired = session->fire_all_rules();
              check(fired == 1);
            }
          }
        }
      }
    }
  }

  section("CSV to Facts") {
    given("a schema with Person type") {
      char const* rfl = R"(
        declare Person
            name: String
            age: int
            city: String
        end

        rule "Count people"
        when
            $p: Person()
        then
            // Person found
        end
      )";

      when("parsing CSV data") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should parse multiple facts") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();

            if (session) {
              char const* csv = R"(name,age,city
John,30,London
Amy,25,Paris
Bob,35,Berlin)";

              std::vector<Fact*> facts = session->add_facts_from_csv("Person", csv);

              check(facts.size() == 3);
              if (facts.size() >= 3) {
                check(std::get<std::string>(facts[0]->fields["name"]) == "John");
                check(std::get<std::string>(facts[1]->fields["name"]) == "Amy");
                check(std::get<std::string>(facts[2]->fields["name"]) == "Bob");
              }

              // Fire rules
              int fired = session->fire_all_rules();
              check(fired == 3);
            }
          }
        }
      }
    }
  }

  section("All Formats") {
    given("a simple schema") {
      char const* rfl = R"(
        declare Person
            name: String
            age: int
        end
      )";

      when("using both JSON and CSV") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should support both formats") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();

            if (session) {
              // JSON
              session->add_fact_from_json("Person", R"({"name":"JSON_Person","age":20})");

              // CSV
              session->add_facts_from_csv("Person", "name,age\nCSV_Person,25");

              // Check all facts inserted
              check(session->get_fact_count() == 2);
            }
          }
        }
      }
    }
  }

  section("DataSource CSV path usage") {
    given("a schema with exactly one declared type") {
      char const* rfl = R"(
        declare Person
            name: String
            age: int
        end
      )";

      when("loading CSV via DataSource::csv(path)") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should read the file content instead of treating the path as CSV text") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();
            check_not_null(session.get());

            auto csv_path = std::filesystem::temp_directory_path() / "rulesforge_databind_people.csv";
            {
              std::ofstream out(csv_path, std::ios::binary);
              out << "name,age\nJohn,30\nAmy,25\n";
            }

            try {
              session->add_data(rulesforge::DataSource::csv(csv_path.string()));
              check(session->get_fact_count() == 2);
            } catch (...) {
              std::filesystem::remove(csv_path);
              throw;
            }

            std::filesystem::remove(csv_path);
          }
        }
      }
    }
  }

  section("DataSource JSON jmespath usage") {
    given("a schema with exactly one declared type") {
      char const* rfl = R"(
        declare Person
            name: String
            age: int
        end
      )";

      when("loading JSON via DataSource::json(content, jmespath)") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should insert one fact per extracted JSON object") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();
            check_not_null(session.get());

            char const* json = R"({"people":[{"name":"John","age":30},{"name":"Amy","age":25}]})";

            session->add_data(rulesforge::DataSource::json(json, "people[*]"));

            check(session->get_fact_count() == 2);
          }
        }
      }
    }
  }

  section("Binary codec import wiring") {
    given("a rule file that imports a missing binary codec") {
      char const* rfl = R"(
        import binary codec "Person" from "missing_codec.dll"

        declare Person
            name: String
        end
      )";

      when("building the knowledge base") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result, "missing_codec_test.rfl", false);

        then("should fail instead of silently ignoring the binary codec import") {
          check(kb == nullptr);
          check(!result.success);
          check(!result.errors.empty());
        }
      }
    }
  }

  section("Binary failure does not poison JSON adapter state") {
    given("a schema that supports both binary and json parsing") {
      char const* rfl = R"(
        declare Person
            name: String
            age: int
        end
      )";

      when("a binary parse fails before a json parse") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("the later json parse should still succeed") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();
            check_not_null(session.get());

            if (session) {
              bool binary_failed = false;
              auto binary = make_truncated_person_binary();

              try {
                session->add_fact_from_binary("Person", binary.data(), binary.size());
              } catch (std::runtime_error const&) {
                binary_failed = true;
              }

              check(binary_failed);

              Fact* fact = session->add_fact_from_json("Person", R"({"name":"John","age":30})");
              check_not_null(fact);
              if (fact) {
                check(fact->type == "Person");
                check(std::get<std::string>(fact->fields["name"]) == "John");
                check(std::get<int64_t>(fact->fields["age"]) == 30);
              }
            }
          }
        }
      }
    }
  }

  section("Binary codec creation reports actual schema errors") {
    given("a schema that JSON supports but binary does not") {
      char const* rfl = R"(
        declare Item
            id: int
        end

        declare Order
            items: List<Item>
        end
      )";

      when("requesting a binary parse") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("the thrown error should preserve the concrete codec creation reason") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();
            check_not_null(session.get());

            if (session) {
              uint8_t dummy[4] = {0, 0, 0, 0};
              bool threw = false;

              try {
                session->add_fact_from_binary("Order", dummy, sizeof(dummy));
              } catch (std::runtime_error const& e) {
                threw = true;
                check(std::string(e.what()).find("Unsupported List element type") != std::string::npos);
                check(std::string(e.what()).find("Order.items") != std::string::npos);
              }

              check(threw);
            }
          }
        }
      }
    }
  }

  section("Enum Support") {
    given("a schema with enum-backed field") {
      char const* rfl = R"(
        enum Status<int>
            PENDING
            ACTIVE
        end

        declare Order
            status: Status
        end
      )";

      when("parsing JSON and binary for the enum field") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should preserve enum metadata through CodecRegistry") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();
            check_not_null(session.get());

            if (session) {
              Fact* json_fact = session->add_fact_from_json("Order", R"({"status":1})");
              check_not_null(json_fact);
              if (json_fact) {
                check(json_fact->type == "Order");
                check(std::get<int64_t>(json_fact->fields["status"]) == 1);
              }

              std::vector<uint8_t> binary = {1, 0, 0, 0};
              Fact* binary_fact = session->add_fact_from_binary("Order", binary.data(), binary.size());
              check_not_null(binary_fact);
              if (binary_fact) {
                check(binary_fact->type == "Order");
                check(std::get<int64_t>(binary_fact->fields["status"]) == 1);
              }
            }
          }
        }
      }
    }
  }

  section("JSON with List<int>") {
    given("a schema with List<int> field") {
      char const* rfl = R"(
        declare Person
            name: String
            scores: List<int>
        end

        rule "Check scores"
        when
            $p: Person()
        then
            // Person with scores found
        end
      )";

      when("parsing JSON with integer array") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should parse List<int> correctly") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();

            if (session) {
              Fact* fact = nullptr;
              try {
                fact = session->add_fact_from_json("Person", R"({"name":"Alice","scores":[85,90,95]})");
              } catch (std::exception const& e) {
                printf("DEBUG: Exception caught: %s\n", e.what());
              }

              printf("DEBUG: fact = %p\n", (void*)fact);
              check_not_null(fact);
              if (fact) {
                printf("DEBUG: fact->type = %s\n", fact->type.c_str());
                check(fact->type == "Person");

                printf("DEBUG: fact->fields.size() = %zu\n", fact->fields.size());

                auto name_it = fact->fields.find("name");
                if (name_it != fact->fields.end()) {
                  printf("DEBUG: name field found\n");
                  check(std::get<std::string>(fact->fields["name"]) == "Alice");
                } else {
                  printf("DEBUG: name field NOT found\n");
                }

                // Verify list content
                auto scores_it = fact->fields.find("scores");
                if (scores_it != fact->fields.end()) {
                  printf("DEBUG: scores field found\n");
                  auto& scores_val = fact->fields["scores"];
                  printf("DEBUG: scores_val.index() = %zu\n", scores_val.index());
                  check(std::holds_alternative<std::shared_ptr<TypedList>>(scores_val));

                  if (std::holds_alternative<std::shared_ptr<TypedList>>(scores_val)) {
                    auto scores = std::get<std::shared_ptr<TypedList>>(scores_val);
                    printf("DEBUG: scores->values.size() = %zu\n", scores->values.size());
                    check(scores->values.size() == 3);

                    if (scores->values.size() >= 3) {
                      printf("DEBUG: values[0].index() = %zu\n", scores->values[0].index());
                      check(std::get<int64_t>(scores->values[0]) == 85);
                      check(std::get<int64_t>(scores->values[1]) == 90);
                      check(std::get<int64_t>(scores->values[2]) == 95);
                    }
                  }
                } else {
                  printf("DEBUG: scores field NOT found\n");
                }
              }
            }
          }
        }
      }
    }
  }

  section("JSON with List<string>") {
    given("a schema with List<string> field") {
      char const* rfl = R"(
        declare Person
            name: String
            tags: List<String>
        end
      )";

      when("parsing JSON with string array") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should parse List<string> correctly") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();

            if (session) {
              Fact* fact = session->add_fact_from_json("Person", R"({"name":"Bob","tags":["admin","user","developer"]})");

              check_not_null(fact);
              if (fact) {
                // Verify list content
                auto& tags_val = fact->fields["tags"];
                check(std::holds_alternative<std::shared_ptr<TypedList>>(tags_val));

                if (std::holds_alternative<std::shared_ptr<TypedList>>(tags_val)) {
                  auto tags = std::get<std::shared_ptr<TypedList>>(tags_val);
                  check(tags->values.size() == 3);

                  if (tags->values.size() >= 3) {
                    check(std::get<std::string>(tags->values[0]) == "admin");
                    check(std::get<std::string>(tags->values[1]) == "user");
                    check(std::get<std::string>(tags->values[2]) == "developer");
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  section("JSON with Nested Object") {
    given("a schema with nested object") {
      char const* rfl = R"(
        declare Address
            city: String
            country: String
        end

        declare Person
            name: String
            address: Address
        end
      )";

      when("parsing JSON with nested object") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should parse nested object correctly") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();

            if (session) {
              Fact* fact = nullptr;
              try {
                fact = session->add_fact_from_json("Person",
                  R"({"name":"Charlie","address":{"city":"London","country":"UK"}})");
              } catch (std::exception const& e) {
                printf("DEBUG: Exception caught: %s\n", e.what());
              }

              check_not_null(fact);
              if (fact) {
                printf("DEBUG: fact->type = %s\n", fact->type.c_str());
                printf("DEBUG: fact->fields.size() = %zu\n", fact->fields.size());

                // Print all field names
                printf("DEBUG: All fields:\n");
                for (auto const& [key, val] : fact->fields) {
                  printf("  - '%s' (index=%zu)\n", std::string(key).c_str(), val.index());
                }

                check(fact->type == "Person");

                auto name_it = fact->fields.find("name");
                if (name_it != fact->fields.end()) {
                  printf("DEBUG: name field found, index = %zu\n", name_it->second.index());
                  if (std::holds_alternative<std::string>(name_it->second)) {
                    printf("DEBUG: name = '%s'\n", std::get<std::string>(name_it->second).c_str());
                  }
                  check(std::get<std::string>(fact->fields["name"]) == "Charlie");
                } else {
                  printf("DEBUG: name field NOT found\n");
                }

                // Verify nested object
                auto& address_val = fact->fields["address"];
                printf("DEBUG: address_val.index() = %zu\n", address_val.index());
                check(std::holds_alternative<FactList>(address_val));

                if (std::holds_alternative<FactList>(address_val)) {
                  auto address_list = std::get<FactList>(address_val);
                  printf("DEBUG: address_list.facts.size() = %zu\n", address_list.facts.size());
                  check(address_list.facts.size() == 1);

                  if (address_list.facts.size() >= 1) {
                    Fact* address = address_list.facts[0];
                    printf("DEBUG: address->type = %s\n", address->type.c_str());
                    check(address->type == "Address");
                    check(std::get<std::string>(address->fields["city"]) == "London");
                    check(std::get<std::string>(address->fields["country"]) == "UK");
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  section("JSON with List<Object>") {
    given("a schema with List<Object> field") {
      char const* rfl = R"(
        declare Item
            id: int
            name: String
        end

        declare Order
            orderId: int
            items: List<Item>
        end
      )";

      when("parsing JSON with object array") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should parse List<Object> correctly") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();

            if (session) {
              Fact* fact = session->add_fact_from_json("Order",
                R"({"orderId":123,"items":[{"id":1,"name":"Widget"},{"id":2,"name":"Gadget"}]})");

              check_not_null(fact);
              if (fact) {
                check(fact->type == "Order");
                check(std::get<int64_t>(fact->fields["orderId"]) == 123);

                // Verify list of objects
                auto& items_val = fact->fields["items"];
                check(std::holds_alternative<std::shared_ptr<TypedList>>(items_val));

                if (std::holds_alternative<std::shared_ptr<TypedList>>(items_val)) {
                  auto items = std::get<std::shared_ptr<TypedList>>(items_val);
                  check(items->values.size() == 2);

                  if (items->values.size() >= 2) {
                    // Check first item
                    check(std::holds_alternative<FactList>(items->values[0]));
                    if (std::holds_alternative<FactList>(items->values[0])) {
                      auto item1_list = std::get<FactList>(items->values[0]);
                      check(item1_list.facts.size() == 1);

                      if (item1_list.facts.size() >= 1) {
                        Fact* item1 = item1_list.facts[0];
                        check(item1->type == "Item");
                        check(std::get<int64_t>(item1->fields["id"]) == 1);
                        check(std::get<std::string>(item1->fields["name"]) == "Widget");
                      }
                    }

                    // Check second item
                    check(std::holds_alternative<FactList>(items->values[1]));
                    if (std::holds_alternative<FactList>(items->values[1])) {
                      auto item2_list = std::get<FactList>(items->values[1]);
                      check(item2_list.facts.size() == 1);

                      if (item2_list.facts.size() >= 1) {
                        Fact* item2 = item2_list.facts[0];
                        check(item2->type == "Item");
                        check(std::get<int64_t>(item2->fields["id"]) == 2);
                        check(std::get<std::string>(item2->fields["name"]) == "Gadget");
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  section("Backward Compatibility") {
    given("old-style JSON without List/Object") {
      char const* rfl = R"(
        declare Person
            name: String
            age: int
        end
      )";

      when("parsing basic JSON") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should still work for basic types") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();

            if (session) {
              Fact* fact = session->add_fact_from_json("Person", R"({"name":"OldStyle","age":40})");

              check_not_null(fact);
              if (fact) {
                check(fact->type == "Person");
                check(std::get<std::string>(fact->fields["name"]) == "OldStyle");
                check(std::get<int64_t>(fact->fields["age"]) == 40);
              }
            }
          }
        }
      }
    }
  }

  section("Set Support") {
    given("a schema with Set<string>") {
      char const* rfl = R"(
        declare User
            name: String
            unique_tags: Set<String>
        end
      )";

      when("parsing JSON with string array (duplicates removed)") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should parse Set<string> correctly") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();

            if (session) {
              Fact* fact = session->add_fact_from_json("User", R"({"name":"Alice","unique_tags":["admin","user","admin"]})");

              check_not_null(fact);
              if (fact) {
                check(fact->type == "User");
                check(std::get<std::string>(fact->fields["name"]) == "Alice");

                // Verify set content
                auto& tags_val = fact->fields["unique_tags"];
                check(std::holds_alternative<std::shared_ptr<ValueSet>>(tags_val));

                if (std::holds_alternative<std::shared_ptr<ValueSet>>(tags_val)) {
                  auto tags = std::get<std::shared_ptr<ValueSet>>(tags_val);
                  // Set should have only 2 items (duplicate "admin" removed)
                  check(tags->values.size() == 2);
                }
              }
            }
          }
        }
      }
    }

    given("a schema with Set<long>") {
      char const* rfl = R"(
        declare User
            ids: Set<long>
        end
      )";

      when("parsing JSON with integer array") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should parse Set<long> into ValueSet<int64_t> correctly") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();

            if (session) {
              Fact* fact = session->add_fact_from_json("User", R"({"ids":[11,22,11]})");

              check_not_null(fact);
              if (fact) {
                auto& ids_val = fact->fields["ids"];
                check(std::holds_alternative<std::shared_ptr<ValueSet>>(ids_val));

                if (std::holds_alternative<std::shared_ptr<ValueSet>>(ids_val)) {
                  auto ids = std::get<std::shared_ptr<ValueSet>>(ids_val);
                  check(ids->values.size() == 2);
                  check(ids->values.count(int64_t(11)) == 1);
                  check(ids->values.count(int64_t(22)) == 1);
                }
              }
            }
          }
        }
      }
    }
  }

  section("Map Support") {
    given("a schema with Map<string, string>") {
      char const* rfl = R"(
        declare Config
            name: String
            metadata: Map<String, String>
        end
      )";

      when("parsing JSON with object") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should parse Map<string, string> correctly") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();

            if (session) {
              Fact* fact = session->add_fact_from_json("Config", R"({"name":"Config1","metadata":{"env":"prod","region":"us-west"}})");

              check_not_null(fact);
              if (fact) {
                check(fact->type == "Config");
                check(std::get<std::string>(fact->fields["name"]) == "Config1");

                // Verify map content
                auto& metadata_val = fact->fields["metadata"];
                check(std::holds_alternative<std::shared_ptr<ValueMap>>(metadata_val));

                if (std::holds_alternative<std::shared_ptr<ValueMap>>(metadata_val)) {
                  auto metadata = std::get<std::shared_ptr<ValueMap>>(metadata_val);
                  check(metadata->entries.size() == 2);

                  // Verify entries
                  auto env_it = metadata->entries.find(std::string("env"));
                  if (env_it != metadata->entries.end()) {
                    check(std::get<std::string>(env_it->second) == "prod");
                  }
                }
              }
            }
          }
        }
      }
    }

    given("a schema with Map<string, long>") {
      char const* rfl = R"(
        declare Config
            metadata: Map<String, long>
        end
      )";

      when("parsing JSON with integer values") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should parse Map<string, long> into ValueMap correctly") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();

            if (session) {
              Fact* fact = session->add_fact_from_json("Config", R"({"metadata":{"low":101,"high":202}})");

              check_not_null(fact);
              if (fact) {
                auto& metadata_val = fact->fields["metadata"];
                check(std::holds_alternative<std::shared_ptr<ValueMap>>(metadata_val));

                if (std::holds_alternative<std::shared_ptr<ValueMap>>(metadata_val)) {
                  auto metadata = std::get<std::shared_ptr<ValueMap>>(metadata_val);
                  check(metadata->entries.size() == 2);

                  auto low_it = metadata->entries.find(std::string("low"));
                  check(low_it != metadata->entries.end());
                  if (low_it != metadata->entries.end()) {
                    check(std::holds_alternative<int64_t>(low_it->second));
                    if (std::holds_alternative<int64_t>(low_it->second)) {
                      check(std::get<int64_t>(low_it->second) == 101);
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  section("Complex Nested Types") {
    given("a schema with nested object containing List") {
      char const* rfl = R"(
        declare Address
            city: String
            tags: List<String>
        end

        declare Person
            name: String
            address: Address
        end
      )";

      when("parsing JSON with nested object containing list") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should parse nested object with List correctly") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();

            if (session) {
              Fact* fact = session->add_fact_from_json("Person",
                R"({"name":"Alice","address":{"city":"NYC","tags":["home","work"]}})");

              check_not_null(fact);
              if (fact) {
                check(fact->type == "Person");
                check(std::get<std::string>(fact->fields["name"]) == "Alice");

                // Verify nested object
                auto& address_val = fact->fields["address"];
                check(std::holds_alternative<FactList>(address_val));

                if (std::holds_alternative<FactList>(address_val)) {
                  auto address_list = std::get<FactList>(address_val);
                  check(address_list.facts.size() == 1);

                  if (address_list.facts.size() >= 1) {
                    Fact* address = address_list.facts[0];
                    check(address->type == "Address");
                    check(std::get<std::string>(address->fields["city"]) == "NYC");

                    // Verify list in nested object
                    auto& tags_val = address->fields["tags"];
                    check(std::holds_alternative<std::shared_ptr<TypedList>>(tags_val));

                    if (std::holds_alternative<std::shared_ptr<TypedList>>(tags_val)) {
                      auto tags = std::get<std::shared_ptr<TypedList>>(tags_val);
                      check(tags->values.size() == 2);
                      if (tags->values.size() >= 2) {
                        check(std::get<std::string>(tags->values[0]) == "home");
                        check(std::get<std::string>(tags->values[1]) == "work");
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }

    given("a schema with nested object containing Set") {
      char const* rfl = R"(
        declare Config
            name: String
            unique_tags: Set<String>
        end

        declare System
            id: int
            config: Config
        end
      )";

      when("parsing JSON with nested object containing set") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should parse nested object with Set correctly") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();

            if (session) {
              Fact* fact = session->add_fact_from_json("System",
                R"({"id":1,"config":{"name":"prod","unique_tags":["a","b","a"]}})");

              check_not_null(fact);
              if (fact) {
                check(fact->type == "System");
                check(std::get<int64_t>(fact->fields["id"]) == 1);

                // Verify nested object
                auto& config_val = fact->fields["config"];
                check(std::holds_alternative<FactList>(config_val));

                if (std::holds_alternative<FactList>(config_val)) {
                  auto config_list = std::get<FactList>(config_val);
                  check(config_list.facts.size() == 1);

                  if (config_list.facts.size() >= 1) {
                    Fact* config = config_list.facts[0];
                    check(config->type == "Config");
                    check(std::get<std::string>(config->fields["name"]) == "prod");

                    // Verify set in nested object (duplicates removed)
                    auto& tags_val = config->fields["unique_tags"];
                    check(std::holds_alternative<std::shared_ptr<ValueSet>>(tags_val));

                    if (std::holds_alternative<std::shared_ptr<ValueSet>>(tags_val)) {
                      auto tags = std::get<std::shared_ptr<ValueSet>>(tags_val);
                      check(tags->values.size() == 2);
                    }
                  }
                }
              }
            }
          }
        }
      }
    }

    given("a schema with nested object containing Map") {
      char const* rfl = R"(
        declare Settings
            name: String
            metadata: Map<String, String>
        end

        declare App
            id: int
            settings: Settings
        end
      )";

      when("parsing JSON with nested object containing map") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should parse nested object with Map correctly") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();

            if (session) {
              Fact* fact = session->add_fact_from_json("App",
                R"({"id":100,"settings":{"name":"app1","metadata":{"env":"prod","version":"1.0"}}})");

              check_not_null(fact);
              if (fact) {
                check(fact->type == "App");
                check(std::get<int64_t>(fact->fields["id"]) == 100);

                // Verify nested object
                auto& settings_val = fact->fields["settings"];
                check(std::holds_alternative<FactList>(settings_val));

                if (std::holds_alternative<FactList>(settings_val)) {
                  auto settings_list = std::get<FactList>(settings_val);
                  check(settings_list.facts.size() == 1);

                  if (settings_list.facts.size() >= 1) {
                    Fact* settings = settings_list.facts[0];
                    check(settings->type == "Settings");
                    check(std::get<std::string>(settings->fields["name"]) == "app1");

                    // Verify map in nested object
                    auto& metadata_val = settings->fields["metadata"];
                    check(std::holds_alternative<std::shared_ptr<ValueMap>>(metadata_val));

                    if (std::holds_alternative<std::shared_ptr<ValueMap>>(metadata_val)) {
                      auto metadata = std::get<std::shared_ptr<ValueMap>>(metadata_val);
                      check(metadata->entries.size() == 2);
                    }
                  }
                }
              }
            }
          }
        }
      }
    }

    given("a schema with List of objects containing Set") {
      char const* rfl = R"(
        declare Item
            id: int
            tags: Set<String>
        end

        declare Order
            orderId: int
            items: List<Item>
        end
      )";

      when("parsing JSON with list of objects containing sets") {
        ParsingResult result;
        auto kb = build_knowledge_base(rfl, result);

        then("should parse List<Object> with Set correctly") {
          check(result.success);

          if (kb) {
            auto session = kb->create_session();

            if (session) {
              Fact* fact = session->add_fact_from_json("Order",
                R"({"orderId":123,"items":[{"id":1,"tags":["new","sale"]},{"id":2,"tags":["used"]}]})");

              check_not_null(fact);
              if (fact) {
                check(fact->type == "Order");
                check(std::get<int64_t>(fact->fields["orderId"]) == 123);

                // Verify list of objects
                auto& items_val = fact->fields["items"];
                check(std::holds_alternative<std::shared_ptr<TypedList>>(items_val));

                if (std::holds_alternative<std::shared_ptr<TypedList>>(items_val)) {
                  auto items = std::get<std::shared_ptr<TypedList>>(items_val);
                  check(items->values.size() == 2);

                  if (items->values.size() >= 2) {
                    // Check first item
                    check(std::holds_alternative<FactList>(items->values[0]));
                    if (std::holds_alternative<FactList>(items->values[0])) {
                      auto item1_list = std::get<FactList>(items->values[0]);
                      check(item1_list.facts.size() == 1);

                      if (item1_list.facts.size() >= 1) {
                        Fact* item1 = item1_list.facts[0];
                        check(item1->type == "Item");
                        check(std::get<int64_t>(item1->fields["id"]) == 1);

                        // Verify set in nested object
                        auto& tags_val = item1->fields["tags"];
                        check(std::holds_alternative<std::shared_ptr<ValueSet>>(tags_val));

                        if (std::holds_alternative<std::shared_ptr<ValueSet>>(tags_val)) {
                          auto tags = std::get<std::shared_ptr<ValueSet>>(tags_val);
                          check(tags->values.size() == 2);
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
