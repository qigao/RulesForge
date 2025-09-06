#include "catch2/catch_all.hpp"
#include "memory_optimized_types.hpp"
#include "object_pool.hpp"
#include "optimized_fact_builder.hpp"
#include "fact_builder.hpp"
#include "drools_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

#include <chrono>
#include <vector>

// Helper for timing operations
template<typename Func>
auto time_operation(Func&& func) {
    auto start = std::chrono::high_resolution_clock::now();
    auto result = func();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    return std::make_pair(std::move(result), duration.count());
}

TEST_CASE("String Interning Performance", "[memory][performance]") {
    constexpr int NUM_OPERATIONS = 10000;
    
    SECTION("String interning efficiency") {
        auto& interner = StringInterner::instance();
        interner.clear();
        
        std::vector<std::string> test_strings;
        for (int i = 0; i < 100; ++i) {
            test_strings.push_back("field_name_" + std::to_string(i));
        }
        
        auto [result, duration] = time_operation([&]() {
            for (int i = 0; i < NUM_OPERATIONS; ++i) {
                auto sv = interner.intern(test_strings[i % test_strings.size()]);
                (void)sv; // Suppress unused variable warning
            }
            return interner.size();
        });
        
        CHECK(result == test_strings.size()); // Should only have unique strings
        std::cout << "String interning: " << NUM_OPERATIONS << " operations in " 
                  << duration << "μs (" << (duration / NUM_OPERATIONS) << "μs per op)\n";
    }
}

TEST_CASE("Object Pool Performance", "[memory][performance]") {
    constexpr int NUM_FACTS = 5000;
    
    SECTION("Standard allocation vs pool allocation") {
        // Warm up pools
        GlobalPools::reserve_all(NUM_FACTS / 4);
        
        // Standard allocation
        auto [std_facts, std_duration] = time_operation([&]() {
            std::vector<std::shared_ptr<Fact>> facts;
            facts.reserve(NUM_FACTS);
            
            for (int i = 0; i < NUM_FACTS; ++i) {
                auto fact = std::make_shared<Fact>();
                fact->type = "TestFact";
                fact->fields["id"] = static_cast<int64_t>(i);
                fact->fields["value"] = 42.0;
                facts.push_back(fact);
            }
            
            return facts.size();
        });
        
        // Pool allocation
        auto [pool_facts, pool_duration] = time_operation([&]() {
            std::vector<decltype(GlobalPools::make_pooled_fact())> facts;
            facts.reserve(NUM_FACTS);
            
            for (int i = 0; i < NUM_FACTS; ++i) {
                auto fact = GlobalPools::make_pooled_fact();
                fact->type = "TestFact";
                fact->fields["id"] = static_cast<int64_t>(i);
                fact->fields["value"] = 42.0;
                facts.push_back(std::move(fact));
            }
            
            return facts.size();
        });
        
        CHECK(std_facts == NUM_FACTS);
        CHECK(pool_facts == NUM_FACTS);
        
        std::cout << "Standard allocation: " << NUM_FACTS << " facts in " << std_duration << "μs\n";
        std::cout << "Pool allocation: " << NUM_FACTS << " facts in " << pool_duration << "μs\n";
        
        if (pool_duration > 0) {
            double speedup = static_cast<double>(std_duration) / pool_duration;
            std::cout << "Pool allocation speedup: " << speedup << "x\n";
        }
        
        // Pool should be faster or at least competitive
        CHECK(pool_duration <= std_duration * 1.2); // Allow 20% variance
    }
}

TEST_CASE("Optimized Fact Builder Performance", "[memory][performance]") {
    constexpr int NUM_FACTS = 10000;
    
    SECTION("Standard builder vs optimized builder") {
        // Standard fact builder
        auto [std_facts, std_duration] = time_operation([&]() {
            std::vector<std::shared_ptr<Fact>> facts;
            facts.reserve(NUM_FACTS);
            
            for (int i = 0; i < NUM_FACTS; ++i) {
                auto fact = FACT("Customer")
                    .set("id", i)
                    .set("name", "Customer" + std::to_string(i))
                    .set("balance", 1000.0 + i)
                    .set("active", true)
                    .build();
                facts.push_back(fact);
            }
            
            return facts.size();
        });
        
        // Optimized fact builder
        auto [opt_facts, opt_duration] = time_operation([&]() {
            std::vector<std::shared_ptr<Fact>> facts;
            facts.reserve(NUM_FACTS);
            
            for (int i = 0; i < NUM_FACTS; ++i) {
                auto fact = FAST_CUSTOMER()
                    .id(i)
                    .name("Customer" + std::to_string(i))
                    .balance(1000.0 + i)
                    .active()
                    .build();
                facts.push_back(fact);
            }
            
            return facts.size();
        });
        
        CHECK(std_facts == NUM_FACTS);
        CHECK(opt_facts == NUM_FACTS);
        
        std::cout << "Standard builder: " << NUM_FACTS << " facts in " << std_duration << "μs\n";
        std::cout << "Optimized builder: " << NUM_FACTS << " facts in " << opt_duration << "μs\n";
        
        if (opt_duration > 0) {
            double speedup = static_cast<double>(std_duration) / opt_duration;
            std::cout << "Optimized builder speedup: " << speedup << "x\n";
        }
    }
}

TEST_CASE("StringViewMap Performance", "[memory][performance]") {
    constexpr int NUM_LOOKUPS = 50000;
    
    SECTION("std::map vs StringViewMap") {
        // Clean state for consistent testing
        StringInterner::instance().clear();
        
        // Setup data
        std::map<std::string, int> std_map;
        StringViewMap<int> sv_map;
        
        std::vector<std::string> keys;
        for (int i = 0; i < 100; ++i) {
            std::string key = "key_" + std::to_string(i);
            keys.push_back(key);
            std_map[key] = i;
            sv_map[StringInterner::instance().intern(key)] = i;
        }
        
        // Benchmark std::map
        auto [std_sum, std_duration] = time_operation([&]() {
            int sum = 0;
            for (int i = 0; i < NUM_LOOKUPS; ++i) {
                auto const& key = keys[i % keys.size()];
                auto it = std_map.find(key);
                if (it != std_map.end()) {
                    sum += it->second;
                }
            }
            return sum;
        });
        
        // Benchmark StringViewMap
        auto [sv_sum, sv_duration] = time_operation([&]() {
            int sum = 0;
            int found_count = 0;
            for (int i = 0; i < NUM_LOOKUPS; ++i) {
                auto const& key_str = keys[i % keys.size()];
                std::string_view key = StringInterner::instance().intern(key_str);
                auto it = sv_map.find(key);
                if (it != sv_map.end()) {
                    sum += it->second;
                    found_count++;
                }
            }
            std::cout << "StringViewMap found " << found_count << "/" << NUM_LOOKUPS << " entries\\n";
            return sum;
        });
        
        CHECK(std_sum == sv_sum); // Should find same values
        
        std::cout << "std::map lookups: " << NUM_LOOKUPS << " in " << std_duration << "μs\n";
        std::cout << "StringViewMap lookups: " << NUM_LOOKUPS << " in " << sv_duration << "μs\n";
        
        if (sv_duration > 0) {
            double speedup = static_cast<double>(std_duration) / sv_duration;
            std::cout << "StringViewMap speedup: " << speedup << "x\n";
        }
    }
}

TEST_CASE("Integrated Memory Optimization", "[memory][performance][integration]") {
    std::string drl = R"(
        declare Customer
            id: int
            name: String
            balance: double
            status: String
        end
        declare VipCustomer
            id: int
        end
        rule "Promote VIP"
        when
            $c : Customer(balance > 50000.0, status == "Active")
        then
            drools.insert({type: "VipCustomer", id: $c.id});
        end
    )";
    
    constexpr int NUM_CUSTOMERS = 1000;
    
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    REQUIRE(result.success);
    
    SECTION("Standard vs optimized fact creation in rules engine") {
        // Test with standard fact builders
        auto session1 = kb->create_session();
        
        auto [std_count, std_duration] = time_operation([&]() {
            std::vector<std::shared_ptr<Fact>> customers;
            customers.reserve(NUM_CUSTOMERS);
            
            for (int i = 0; i < NUM_CUSTOMERS; ++i) {
                auto customer = FACT("Customer")
                    .set("id", i)
                    .set("name", "Customer" + std::to_string(i))
                    .set("balance", 30000.0 + (i * 50.0))
                    .set("status", "Active")
                    .build();
                customers.push_back(customer);
            }
            
            session1->add_facts(customers);
            return session1->fire_all_rules();
        });
        
        // Test with optimized fact builders
        auto session2 = kb->create_session();
        
        auto [opt_count, opt_duration] = time_operation([&]() {
            std::vector<std::shared_ptr<Fact>> customers;
            customers.reserve(NUM_CUSTOMERS);
            
            for (int i = 0; i < NUM_CUSTOMERS; ++i) {
                auto customer = FAST_CUSTOMER()
                    .id(i)
                    .name("Customer" + std::to_string(i))
                    .balance(30000.0 + (i * 50.0))
                    .active()
                    .build();
                customers.push_back(customer);
            }
            
            session2->add_facts(customers);
            return session2->fire_all_rules();
        });
        
        CHECK(std_count == opt_count); // Should fire same number of rules
        
        std::cout << "Standard facts processing: " << std_duration << "μs\n";
        std::cout << "Optimized facts processing: " << opt_duration << "μs\n";
        
        if (opt_duration > 0) {
            double speedup = static_cast<double>(std_duration) / opt_duration;
            std::cout << "End-to-end speedup: " << speedup << "x\n";
        }
        
        // Print memory pool statistics
        auto stats = PoolStatsCollector::collect();
        std::cout << "\n" << PoolStatsCollector::format_stats(stats) << "\n";
    }
}