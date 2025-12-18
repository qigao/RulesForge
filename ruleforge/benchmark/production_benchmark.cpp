// PROD-005: Production Load Testing Benchmarks
//
// This file contains benchmarks to validate production readiness:
// 1. Rule compilation scalability (10K rules)
// 2. Fact throughput (1M facts)
// 3. Latency percentiles (p50, p95, p99)
// 4. Memory usage under load
//
// Run with: ./production_benchmark.RuleForge.exe
// For detailed output: ./production_benchmark.RuleForge.exe -s

#include "catch2/catch_all.hpp"
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <numeric>
#include <random>
#include <sstream>
#include <vector>

using Clock = std::chrono::high_resolution_clock;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;

// Helper to generate N simple rules
std::string generate_rules(int count, std::string const& fact_type = "TestFact") {
    std::ostringstream oss;
    oss << "package benchmark\n\n";
    oss << "declare " << fact_type << "\n";
    oss << "    id: int\n";
    oss << "    value: int\n";
    oss << "    category: String\n";
    oss << "end\n\n";

    oss << "declare Result\n";
    oss << "    ruleId: int\n";
    oss << "    factId: int\n";
    oss << "end\n\n";

    for (int i = 0; i < count; ++i) {
        oss << "rule \"Rule_" << i << "\"\n";
        oss << "when\n";
        oss << "    $f : " << fact_type << "(value >= " << (i % 100) << ", value < " << ((i % 100) + 10) << ")\n";
        oss << "then\n";
        oss << "    // Rule " << i << " matched\n";
        oss << "end\n\n";
    }

    return oss.str();
}

// Helper to create test facts
std::vector<std::shared_ptr<Fact>> create_facts(int count, std::string const& type = "benchmark.TestFact") {
    std::vector<std::shared_ptr<Fact>> facts;
    facts.reserve(count);

    std::mt19937 gen(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<> value_dist(0, 109);
    std::vector<std::string> categories = {"A", "B", "C", "D", "E"};

    for (int i = 0; i < count; ++i) {
        auto fact = std::make_shared<Fact>();
        fact->type = type;
        fact->fields["id"] = (int64_t)i;
        fact->fields["value"] = (int64_t)value_dist(gen);
        fact->fields["category"] = categories[i % categories.size()];
        facts.push_back(fact);
    }

    return facts;
}

// Calculate percentile from sorted vector
template <typename T>
T percentile(std::vector<T>& data, double p) {
    if (data.empty()) return T{};
    std::sort(data.begin(), data.end());
    size_t idx = static_cast<size_t>(p * (data.size() - 1));
    return data[idx];
}

// ============================================================================
// BENCHMARK 1: Rule Compilation Scalability
// ============================================================================

TEST_CASE("Benchmark: Rule Compilation Time", "[benchmark][compilation]") {
    std::vector<int> rule_counts = {100, 500, 1000, 2000, 5000, 10000};

    std::cout << "\n=== Rule Compilation Benchmark ===" << std::endl;
    std::cout << std::setw(10) << "Rules" << std::setw(15) << "Time (ms)"
              << std::setw(15) << "Rules/sec" << std::endl;
    std::cout << std::string(40, '-') << std::endl;

    for (int count : rule_counts) {
        std::string drl = generate_rules(count);

        auto start = Clock::now();
        ParsingResult result;
        auto kb = build_knowledge_base(drl, result);
        auto end = Clock::now();

        auto duration_ms = std::chrono::duration_cast<Milliseconds>(end - start).count();
        double rules_per_sec = (duration_ms > 0) ? (count * 1000.0 / duration_ms) : 0;

        std::cout << std::setw(10) << count
                  << std::setw(15) << duration_ms
                  << std::setw(15) << std::fixed << std::setprecision(0) << rules_per_sec
                  << std::endl;

        REQUIRE(result.success);
        REQUIRE(kb);

        // Performance assertion: 10K rules should compile in < 30 seconds
        if (count == 10000) {
            CHECK(duration_ms < 30000);
        }
    }
}

// ============================================================================
// BENCHMARK 2: Fact Insertion Throughput
// ============================================================================

TEST_CASE("Benchmark: Fact Insertion Throughput", "[benchmark][throughput]") {
    // Create a simple rule set
    std::string drl = R"(
package benchmark

declare TestFact
    id: int
    value: int
    category: String
end

rule "Simple Match"
when
    $f : TestFact(value > 50)
then
    // matched
end
)";

    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    REQUIRE(result.success);

    std::vector<int> fact_counts = {1000, 10000, 100000, 500000, 1000000};

    std::cout << "\n=== Fact Insertion Throughput Benchmark ===" << std::endl;
    std::cout << std::setw(12) << "Facts" << std::setw(15) << "Insert (ms)"
              << std::setw(15) << "Facts/sec" << std::setw(15) << "Memory (MB)" << std::endl;
    std::cout << std::string(57, '-') << std::endl;

    for (int count : fact_counts) {
        auto session = kb->create_session();
        auto facts = create_facts(count);

        auto start = Clock::now();
        session->add_facts(facts);
        auto end = Clock::now();

        auto duration_ms = std::chrono::duration_cast<Milliseconds>(end - start).count();
        double facts_per_sec = (duration_ms > 0) ? (count * 1000.0 / duration_ms) : 0;

        // Get memory stats
        auto metrics = session->get_metrics();
        double memory_mb = metrics.memory_used_bytes / (1024.0 * 1024.0);

        std::cout << std::setw(12) << count
                  << std::setw(15) << duration_ms
                  << std::setw(15) << std::fixed << std::setprecision(0) << facts_per_sec
                  << std::setw(15) << std::setprecision(2) << memory_mb
                  << std::endl;

        CHECK(session->get_fact_count() == count);

        // Performance assertion: 1M facts should insert in < 60 seconds
        if (count == 1000000) {
            CHECK(duration_ms < 60000);
        }
    }
}

// ============================================================================
// BENCHMARK 3: Rule Firing Throughput
// ============================================================================

TEST_CASE("Benchmark: Rule Firing Throughput", "[benchmark][firing]") {
    std::string drl = R"(
package benchmark

declare TestFact
    id: int
    value: int
    category: String
end

declare ProcessedFact
    factId: int
end

rule "Process Facts"
when
    $f : TestFact(value > 50)
    not ProcessedFact(factId == $f.id)
then
    rfl.insert({type: "benchmark.ProcessedFact", factId: f.id});
end
)";

    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    REQUIRE(result.success);

    std::vector<int> fact_counts = {1000, 5000, 10000, 50000};

    std::cout << "\n=== Rule Firing Throughput Benchmark ===" << std::endl;
    std::cout << std::setw(10) << "Facts" << std::setw(15) << "Fire (ms)"
              << std::setw(12) << "Fired" << std::setw(15) << "Rules/sec" << std::endl;
    std::cout << std::string(52, '-') << std::endl;

    for (int count : fact_counts) {
        auto session = kb->create_session();
        auto facts = create_facts(count);
        session->add_facts(facts);

        auto start = Clock::now();
        int fired = session->fire_all_rules();
        auto end = Clock::now();

        auto duration_ms = std::chrono::duration_cast<Milliseconds>(end - start).count();
        double rules_per_sec = (duration_ms > 0) ? (fired * 1000.0 / duration_ms) : 0;

        std::cout << std::setw(10) << count
                  << std::setw(15) << duration_ms
                  << std::setw(12) << fired
                  << std::setw(15) << std::fixed << std::setprecision(0) << rules_per_sec
                  << std::endl;

        CHECK(fired > 0);
    }
}

// ============================================================================
// BENCHMARK 4: Latency Percentiles
// ============================================================================

TEST_CASE("Benchmark: Latency Percentiles", "[benchmark][latency]") {
    std::string drl = R"(
package benchmark

declare Event
    id: int
    timestamp: int
end

rule "Process Event"
when
    $e : Event()
then
    // processed
end
)";

    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    REQUIRE(result.success);

    constexpr int ITERATIONS = 1000;
    std::vector<int64_t> insert_latencies;
    std::vector<int64_t> fire_latencies;
    insert_latencies.reserve(ITERATIONS);
    fire_latencies.reserve(ITERATIONS);

    auto session = kb->create_session();

    for (int i = 0; i < ITERATIONS; ++i) {
        auto fact = std::make_shared<Fact>();
        fact->type = "benchmark.Event";
        fact->fields["id"] = (int64_t)i;
        fact->fields["timestamp"] = (int64_t)Clock::now().time_since_epoch().count();

        // Measure insert latency
        auto start = Clock::now();
        session->add_fact(fact);
        auto end = Clock::now();
        insert_latencies.push_back(std::chrono::duration_cast<Microseconds>(end - start).count());

        // Measure fire latency
        start = Clock::now();
        session->fire_all_rules();
        end = Clock::now();
        fire_latencies.push_back(std::chrono::duration_cast<Microseconds>(end - start).count());
    }

    // Calculate percentiles
    auto insert_p50 = percentile(insert_latencies, 0.50);
    auto insert_p95 = percentile(insert_latencies, 0.95);
    auto insert_p99 = percentile(insert_latencies, 0.99);

    auto fire_p50 = percentile(fire_latencies, 0.50);
    auto fire_p95 = percentile(fire_latencies, 0.95);
    auto fire_p99 = percentile(fire_latencies, 0.99);

    std::cout << "\n=== Latency Percentiles (microseconds) ===" << std::endl;
    std::cout << std::setw(15) << "Operation" << std::setw(10) << "p50"
              << std::setw(10) << "p95" << std::setw(10) << "p99" << std::endl;
    std::cout << std::string(45, '-') << std::endl;
    std::cout << std::setw(15) << "Insert" << std::setw(10) << insert_p50
              << std::setw(10) << insert_p95 << std::setw(10) << insert_p99 << std::endl;
    std::cout << std::setw(15) << "Fire" << std::setw(10) << fire_p50
              << std::setw(10) << fire_p95 << std::setw(10) << fire_p99 << std::endl;

    // Performance assertions
    CHECK(insert_p99 < 10000);  // Insert p99 < 10ms
    CHECK(fire_p99 < 50000);    // Fire p99 < 50ms
}

// ============================================================================
// BENCHMARK 5: Memory Usage Under Sustained Load
// ============================================================================

TEST_CASE("Benchmark: Memory Under Sustained Load", "[benchmark][memory]") {
    std::string drl = R"(
package benchmark

declare TestFact
    id: int
    value: int
end

rule "Match and Insert"
when
    $f : TestFact(value > 50)
then
    // matched
end
)";

    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    REQUIRE(result.success);

    auto session = kb->create_session();

    constexpr int BATCH_SIZE = 10000;
    constexpr int BATCHES = 10;

    std::cout << "\n=== Memory Usage Under Sustained Load ===" << std::endl;
    std::cout << std::setw(10) << "Batch" << std::setw(15) << "Facts"
              << std::setw(15) << "Memory (MB)" << std::setw(15) << "Delta (MB)" << std::endl;
    std::cout << std::string(55, '-') << std::endl;

    double prev_memory = 0;

    for (int batch = 0; batch < BATCHES; ++batch) {
        auto facts = create_facts(BATCH_SIZE);
        for (auto& f : facts) {
            f->fields["id"] = (int64_t)(batch * BATCH_SIZE + std::get<int64_t>(f->fields["id"]));
        }
        session->add_facts(facts);
        session->fire_all_rules();

        auto metrics = session->get_metrics();
        double memory_mb = metrics.memory_used_bytes / (1024.0 * 1024.0);
        double delta = memory_mb - prev_memory;

        std::cout << std::setw(10) << (batch + 1)
                  << std::setw(15) << ((batch + 1) * BATCH_SIZE)
                  << std::setw(15) << std::fixed << std::setprecision(2) << memory_mb
                  << std::setw(15) << (batch > 0 ? delta : 0.0)
                  << std::endl;

        prev_memory = memory_mb;
    }

    // Memory should be bounded (not growing unbounded)
    auto final_metrics = session->get_metrics();
    double final_memory_mb = final_metrics.memory_used_bytes / (1024.0 * 1024.0);
    CHECK(final_memory_mb < 500);  // Should stay under 500MB for 100K facts
}

// ============================================================================
// BENCHMARK SUMMARY
// ============================================================================

TEST_CASE("Benchmark: Summary Report", "[benchmark][summary]") {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "PRODUCTION BENCHMARK SUMMARY" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "\nTarget Metrics:" << std::endl;
    std::cout << "  - 10K rules compile time: < 30 seconds" << std::endl;
    std::cout << "  - 1M facts insert time: < 60 seconds" << std::endl;
    std::cout << "  - Insert latency p99: < 10ms" << std::endl;
    std::cout << "  - Fire latency p99: < 50ms" << std::endl;
    std::cout << "  - Memory for 100K facts: < 500MB" << std::endl;
    std::cout << "\nRun individual benchmarks with -c flag:" << std::endl;
    std::cout << "  ./production_benchmark.RuleForge.exe \"[compilation]\"" << std::endl;
    std::cout << "  ./production_benchmark.RuleForge.exe \"[throughput]\"" << std::endl;
    std::cout << "  ./production_benchmark.RuleForge.exe \"[latency]\"" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
}
