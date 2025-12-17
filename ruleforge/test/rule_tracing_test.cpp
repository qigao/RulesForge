#include "catch2/catch_all.hpp"
#include "drools_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

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
            drools.insert({type: "Adult", name: $p.name});
        end
        
        rule "Classify Seniors"
        salience 5
        when
            $p : Person(age >= 65, status == "Active")
        then
            drools.insert({type: "Senior", name: $p.name, age: $p.age});
        end
    )";
    
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    REQUIRE(result.success);
    return kb->create_session();
}

TEST_CASE("Rule Execution Tracing", "[tracing]") {
    auto session = create_tracing_session();
    
    // Enable tracing
    session->enable_tracing(true);
    
    SECTION("Basic rule firing trace") {
        // Add some test facts
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
        
        session->add_fact(john);
        session->add_fact(mary);
        session->add_fact(bob);
        
        int rules_fired = session->fire_all_rules();
        
        CHECK(rules_fired == 2); // Adult rule for John, Senior rule for Mary
        
        // Get trace and verify events
        auto trace = session->get_tracer().get_trace();
        
        // Should have fact addition events
        int fact_additions = 0;
        int rule_matches = 0;
        int rule_fires = 0;
        
        for (auto const& event : trace) {
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
        
        CHECK(fact_additions >= 3); // At least our 3 input facts
        CHECK(rule_matches >= 2);   // Rules should match
        CHECK(rule_fires == 2);     // 2 rules should fire
        
        std::cout << "\n" << session->get_execution_trace() << std::endl;
        std::cout << "\n" << session->get_rule_performance_summary() << std::endl;
    }
    
    SECTION("Batch operations tracing") {
        std::vector<std::shared_ptr<Fact>> people;
        
        for (int i = 0; i < 10; ++i) {
            auto person = std::make_shared<Fact>();
            person->type = "Person";
            person->fields["name"] = "Person" + std::to_string(i);
            person->fields["age"] = (int64_t)(20 + i * 5);
            person->fields["status"] = "Active";
            people.push_back(person);
        }
        
        session->add_facts(people);
        int rules_fired = session->fire_all_rules();
        
        CHECK(rules_fired > 0);
        
        // Verify tracing captured batch operation
        auto trace = session->get_tracer().get_trace();
        int fact_additions = 0;
        
        for (auto const& event : trace) {
            if (event.type == RuleTraceEvent::Type::FACT_ADDED) {
                fact_additions++;
            }
        }
        
        CHECK(fact_additions >= 10);
        
        std::cout << "\nBatch operation trace:\n" << session->get_execution_trace() << std::endl;
    }
}

TEST_CASE("Rule Performance Analysis", "[tracing][performance]") {
    auto session = create_tracing_session();
    session->enable_tracing(true);
    
    // Add many facts to trigger rules multiple times
    std::vector<std::shared_ptr<Fact>> people;
    for (int i = 0; i < 100; ++i) {
        auto person = std::make_shared<Fact>();
        person->type = "Person";
        person->fields["name"] = "Person" + std::to_string(i);
        person->fields["age"] = (int64_t)(18 + i % 60); // Ages 18-77
        person->fields["status"] = "Active";
        people.push_back(person);
    }
    
    session->add_facts(people);
    int rules_fired = session->fire_all_rules();
    
    CHECK(rules_fired > 0);
    
    // Analyze performance
    auto stats = session->get_tracer().get_rule_statistics();
    CHECK(stats.size() >= 2); // Should have stats for both rules
    
    for (auto const& stat : stats) {
        CHECK(stat.fire_count > 0);
        CHECK(stat.total_execution_time_us >= 0);
        std::cout << "Rule '" << stat.rule_name << "' fired " << stat.fire_count 
                  << " times, avg time: " << stat.avg_execution_time_us << "μs" << std::endl;
    }
    
    std::cout << "\n" << session->get_rule_performance_summary() << std::endl;
}

TEST_CASE("Fact Event Tracing", "[tracing]") {
    auto session = create_tracing_session();
    session->enable_tracing(true);
    
    auto person = std::make_shared<Fact>();
    person->type = "Person";
    person->fields["name"] = "TestPerson";
    person->fields["age"] = (int64_t)25;
    person->fields["status"] = "Active";
    
    session->add_fact(person);
    session->fire_all_rules();
    
    // Get events related to this specific fact
    auto fact_events = session->get_tracer().get_fact_events(person->id);
    CHECK(fact_events.size() >= 2); // At least fact addition and rule involvement
    
    bool found_addition = false;
    bool found_in_rule = false;
    
    for (auto const& event : fact_events) {
        if (event.type == RuleTraceEvent::Type::FACT_ADDED && event.fact_id == person->id) {
            found_addition = true;
        }
        if (event.type == RuleTraceEvent::Type::RULE_FIRED && 
            std::find(event.involved_fact_ids.begin(), event.involved_fact_ids.end(), person->id) 
            != event.involved_fact_ids.end()) {
            found_in_rule = true;
        }
    }
    
    CHECK(found_addition);
    CHECK(found_in_rule);
}

