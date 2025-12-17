#include "catch2/catch_all.hpp"
#include "drools_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

#include <chrono>
#include <vector>

std::unique_ptr<StatefulSession> create_test_session() {
    std::string drl = R"(
        declare Person
            name: String
            age: int
            salary: double
        end
        declare HighEarner
            name: String
        end
        rule "Find High Earners"
        when
            $p : Person(age > 25, salary > 50000.0)
        then
            drools.insert({type: "HighEarner", name: $p.name});
        end
    )";
    
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    REQUIRE(result.success);
    return kb->create_session();
}

std::vector<std::shared_ptr<Fact>> create_test_facts(int count) {
    std::vector<std::shared_ptr<Fact>> facts;
    facts.reserve(count);
    
    for (int i = 0; i < count; ++i) {
        auto fact = std::make_shared<Fact>();
        fact->type = "Person";
        fact->fields["name"] = "Person" + std::to_string(i);
        fact->fields["age"] = (int64_t)(25 + i % 40);
        fact->fields["salary"] = 55000.0 + (i % 50000);
        facts.push_back(fact);
    }
    
    return facts;
}

TEST_CASE("Performance: Batch vs Single Operations", "[performance]") {
    constexpr int FACT_COUNT = 1000;
    
    SECTION("Single fact operations") {
        auto session = create_test_session();
        auto facts = create_test_facts(FACT_COUNT);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (auto& fact : facts) {
            session->add_fact(fact);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        int fired = session->fire_all_rules();
        
        std::cout << "Single operations: " << FACT_COUNT << " facts in " 
                  << duration.count() << " μs (" << fired << " rules fired)" << std::endl;
        
        CHECK(session->get_fact_count() > FACT_COUNT); // Original facts + generated HighEarner facts
        CHECK(fired > 0);
    }
    
    SECTION("Batch operations") {
        auto session = create_test_session();
        auto facts = create_test_facts(FACT_COUNT);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        session->add_facts(facts);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        int fired = session->fire_all_rules();
        
        std::cout << "Batch operations: " << FACT_COUNT << " facts in " 
                  << duration.count() << " μs (" << fired << " rules fired)" << std::endl;
        
        CHECK(session->get_fact_count() > FACT_COUNT);
        CHECK(fired > 0);
    }
}

TEST_CASE("Performance: Large Batch Operations", "[performance]") {
    constexpr int LARGE_COUNT = 10000;
    
    auto session = create_test_session();
    auto facts = create_test_facts(LARGE_COUNT);
    
    auto start = std::chrono::high_resolution_clock::now();
    session->add_facts(facts);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    int fired = session->fire_all_rules();
    
    std::cout << "Large batch: " << LARGE_COUNT << " facts in " 
              << duration.count() << " ms (" << fired << " rules fired)" << std::endl;
    
    CHECK(session->get_fact_count() >= LARGE_COUNT);
    CHECK(fired > 0);
}

TEST_CASE("Performance: Batch Retraction", "[performance]") {
    constexpr int COUNT = 500;
    
    auto session = create_test_session();
    auto facts = create_test_facts(COUNT);
    
    // Add facts first
    session->add_facts(facts);
    session->fire_all_rules();
    
    auto initial_count = session->get_fact_count();
    
    // Benchmark retraction
    auto start = std::chrono::high_resolution_clock::now();
    session->retract_facts(facts);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Batch retraction: " << COUNT << " facts in " 
              << duration.count() << " μs" << std::endl;
    
    // Should have fewer facts now (some HighEarner facts might remain due to TMS)
    CHECK(session->get_fact_count() < initial_count);
}

