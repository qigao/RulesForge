#include "engine/knowledge_base.hpp"
#include "rfl_parser.hpp"
#include "engine/stateful_session.hpp"
#include "tinytest.h"
#include <iostream>

std::unique_ptr<StatefulSession> create_tracing_session() {
  std::string drl = R"(
        declare Person
            name: String
            age: int
            status: String
        end
        declare Adult
            name: String
        end
        declare Senior
            name: String
            age: int
        end

        rule "Classify Adults"
        salience 10
        when
            $p : Person(age >= 18, age < 65, status == "Active")
        then
            insert Adult { name = $p.name }
        end

        rule "Classify Seniors"
        salience 5
        when
            $p : Person(age >= 65, status == "Active")
        then
            insert Senior { name = $p.name, age = $p.age }
        end
    )";

  ParsingResult result;
  auto kb = build_knowledge_base(drl, result);
  if (!result.success || !kb) {
    throw std::runtime_error("Failed to build knowledge base");
  }
  return kb->create_session();
}

suite("Rule Execution Tracing") {
  group("Basic rule firing trace") {
    it("traces fact additions and rule firings") {
      auto session = create_tracing_session();
      session->enable_tracing(true);

      auto john = std::make_shared<Fact>();
      john->type = "Person";
      john->fields["name"] = "John";
      john->fields["age"] = (int64_t)30;
      john->fields["status"] = "Active";

      auto mary = std::make_shared<Fact>();
      mary->type = "Person";
      mary->fields["name"] = "Mary";
      mary->fields["age"] = (int64_t)70;
      mary->fields["status"] = "Active";

      auto bob = std::make_shared<Fact>();
      bob->type = "Person";
      bob->fields["name"] = "Bob";
      bob->fields["age"] = (int64_t)16;
      bob->fields["status"] = "Active";

      session->add_fact(john.get());
      session->add_fact(mary.get());
      session->add_fact(bob.get());

      int rules_fired = session->fire_all_rules();

      check(rules_fired == 2);

      auto trace = session->get_tracer().get_trace();

      int fact_additions = 0;
      int rule_matches = 0;
      int rule_fires = 0;

      for (auto const &event : trace) {
        switch (event.type) {
        case RuleTraceEvent::Type::FACT_ADDED:
          fact_additions++;
          break;
        case RuleTraceEvent::Type::RULE_MATCHED:
          rule_matches++;
          break;
        case RuleTraceEvent::Type::RULE_FIRED:
          rule_fires++;
          break;
        default:
          break;
        }
      }

      check(fact_additions >= 3);
      check(rule_matches >= 2);
      check(rule_fires == 2);

      std::cout << "\n" << session->get_execution_trace() << std::endl;
      std::cout << "\n" << session->get_rule_performance_summary() << std::endl;
    }
  }

  group("Batch operations tracing") {
    it("traces batch fact additions") {
      auto session = create_tracing_session();
      session->enable_tracing(true);

      std::vector<std::shared_ptr<Fact>> people;

      for (int i = 0; i < 10; ++i) {
        auto person = std::make_shared<Fact>();
        person->type = "Person";
        person->fields["name"] = "Person" + std::to_string(i);
        person->fields["age"] = (int64_t)(20 + i * 5);
        person->fields["status"] = "Active";
        people.push_back(person);
      }

      std::vector<Fact*> raw_people;
      for (auto& p : people) raw_people.push_back(p.get());
      session->add_facts(raw_people);
      int rules_fired = session->fire_all_rules();

      check(rules_fired > 0);

      auto trace = session->get_tracer().get_trace();
      int fact_additions = 0;

      for (auto const &event : trace) {
        if (event.type == RuleTraceEvent::Type::FACT_ADDED) {
          fact_additions++;
        }
      }

      check(fact_additions >= 10);

      std::cout << "\nBatch operation trace:\n" << session->get_execution_trace() << std::endl;
    }
  }
}

suite("Rule Performance Analysis") {
  it("analyzes rule performance") {
    auto session = create_tracing_session();
    session->enable_tracing(true);

    std::vector<std::shared_ptr<Fact>> people;
    for (int i = 0; i < 100; ++i) {
      auto person = std::make_shared<Fact>();
      person->type = "Person";
      person->fields["name"] = "Person" + std::to_string(i);
      person->fields["age"] = (int64_t)(18 + i % 60);
      person->fields["status"] = "Active";
      people.push_back(person);
    }

    for (auto& p : people) session->add_fact(p.get());
    int rules_fired = session->fire_all_rules();

    check(rules_fired > 0);

    auto stats = session->get_tracer().get_rule_statistics();
    check(stats.size() >= 2);

    for (auto const &stat : stats) {
      check(stat.fire_count > 0);
      check(stat.total_execution_time_us >= 0);
      std::cout << "Rule '" << stat.rule_name << "' fired " << stat.fire_count
                << " times, avg time: " << stat.avg_execution_time_us << "μs" << std::endl;
    }

    std::cout << "\n" << session->get_rule_performance_summary() << std::endl;
  }
}

suite("Fact Event Tracing") {
  it("traces fact-related events") {
    auto session = create_tracing_session();
    session->enable_tracing(true);

    auto person = std::make_shared<Fact>();
    person->type = "Person";
    person->fields["name"] = "TestPerson";
    person->fields["age"] = (int64_t)25;
    person->fields["status"] = "Active";

    session->add_fact(person.get());
    session->fire_all_rules();

    auto fact_events = session->get_tracer().get_fact_events(person->id);
    check(fact_events.size() >= 2);

    bool found_addition = false;
    bool found_in_rule = false;

    for (auto const &event : fact_events) {
      if (event.type == RuleTraceEvent::Type::FACT_ADDED && event.fact_id == person->id) {
        found_addition = true;
      }
      if (event.type == RuleTraceEvent::Type::RULE_FIRED &&
          std::find(event.involved_fact_ids.begin(), event.involved_fact_ids.end(), person->id) !=
              event.involved_fact_ids.end()) {
        found_in_rule = true;
      }
    }

    check(found_addition);
    check(found_in_rule);
  }
}
