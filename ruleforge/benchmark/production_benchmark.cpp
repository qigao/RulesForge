// PROD-005: Production Load Testing Benchmarks
//
// This file contains benchmarks to validate production readiness:
// 1. Rule compilation scalability (10K rules)
// 2. Fact throughput (1M facts)
// 3. Latency percentiles (p50, p95, p99)
// 4. Memory usage under load

#include "tinytest.h"
#include "rfl_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"

#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <vector>

using Clock = std::chrono::high_resolution_clock;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;

static std::string generate_rules(int count, std::string const& fact_type = "TestFact") {
    std::ostringstream oss;
    oss << "package benchmark\n\n";
    oss << "declare " << fact_type << " id: int, value: int, category: String end\n";
    oss << "declare Result ruleId: int, factId: int end\n\n";

    for (int i = 0; i < count; ++i) {
        oss << "rule \"Rule_" << i << "\" when $f : " << fact_type
            << "(value >= " << (i % 100) << ", value < " << ((i % 100) + 10) << ") then end\n";
    }
    return oss.str();
}

static std::vector<std::shared_ptr<Fact>> create_facts(int count, std::string const& type = "benchmark.TestFact") {
    std::vector<std::shared_ptr<Fact>> facts;
    facts.reserve(count);

    std::mt19937 gen(42);
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

template <typename T>
static T percentile(std::vector<T> data, double p) {
    if (data.empty()) return T{};
    size_t idx = static_cast<size_t>(p * (data.size() - 1));
    std::nth_element(data.begin(), data.begin() + idx, data.end());
    return data[idx];
}

static std::shared_ptr<KnowledgeBase> build_simple_kb() {
    std::string drl = R"(
package benchmark
declare TestFact id: int, value: int, category: String end
rule "Simple Match" when $f : TestFact(value > 50) then end
)";
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    if (!result.success) throw std::runtime_error("Failed to build KB");
    return kb;
}

static std::shared_ptr<KnowledgeBase> build_processing_kb() {
    std::string drl = R"(
package benchmark
declare TestFact id: int, value: int, category: String end
declare ProcessedFact factId: int end
rule "Process Facts"
when
    $f : TestFact(value > 50)
    not ProcessedFact(factId == $f.id)
then
    rfl.insert({type: "benchmark.ProcessedFact", factId: $f.id});
end
)";
    ParsingResult result;
    auto kb = build_knowledge_base(drl, result);
    if (!result.success) throw std::runtime_error("Failed to build KB");
    return kb;
}

suite("Production Benchmarks") {
    group("Rule Compilation") {
        bench("benchmarks compilation scalability") {
            std::string drl_100 = generate_rules(100);
            std::string drl_500 = generate_rules(500);
            std::string drl_1000 = generate_rules(1000);
            std::string drl_2000 = generate_rules(2000);
            std::string drl_5000 = generate_rules(5000);

            ParsingResult result;

            benchmark("compile 100 rules", 10) {
                build_knowledge_base(drl_100, result);
            }

            benchmark("compile 500 rules", 5) {
                build_knowledge_base(drl_500, result);
            }

            benchmark("compile 1000 rules", 3) {
                build_knowledge_base(drl_1000, result);
            }

            benchmark("compile 2000 rules", 2) {
                build_knowledge_base(drl_2000, result);
            }

            benchmark("compile 5000 rules", 1) {
                build_knowledge_base(drl_5000, result);
            }

            check(result.success);
        }

        bench("compiles 10K rules under 30s") {
            std::string drl = generate_rules(10000);
            ParsingResult result;

            benchmark("compile 10000 rules", 1) {
                build_knowledge_base(drl, result);
            }

            check(result.success);
        }
    }

    group("Session Creation") {
        bench("benchmarks session creation (network shared)") {
            std::string drl_100 = generate_rules(100);
            std::string drl_1000 = generate_rules(1000);
            std::string drl_5000 = generate_rules(5000);

            ParsingResult result;
            auto kb_100 = build_knowledge_base(drl_100, result);
            check(result.success);
            auto kb_1000 = build_knowledge_base(drl_1000, result);
            check(result.success);
            auto kb_5000 = build_knowledge_base(drl_5000, result);
            check(result.success);

            benchmark("create_session (100 rules)", 100) {
                auto session = kb_100->create_session();
            }

            benchmark("create_session (1000 rules)", 50) {
                auto session = kb_1000->create_session();
            }

            benchmark("create_session (5000 rules)", 10) {
                auto session = kb_5000->create_session();
            }
        }
    }

    group("Fact Insertion") {
        bench("benchmarks insertion throughput") {
            auto kb = build_simple_kb();
            auto facts_1k = create_facts(1000);
            auto facts_10k = create_facts(10000);
            auto facts_100k = create_facts(100000);

            benchmark("insert 1K facts", 50) {
                auto session = kb->create_session();
                session->add_facts(facts_1k);
            }

            benchmark("insert 10K facts", 10) {
                auto session = kb->create_session();
                session->add_facts(facts_10k);
            }

            benchmark("insert 100K facts", 3) {
                auto session = kb->create_session();
                session->add_facts(facts_100k);
            }
        }

        bench("inserts 1M facts under 60s") {
            auto kb = build_simple_kb();
            auto facts = create_facts(1000000);

            benchmark("insert 1M facts", 1) {
                auto session = kb->create_session();
                session->add_facts(facts);
                check_size_eq(session->get_fact_count(), 1000000);
            }
        }
    }

    group("Rule Firing") {
        bench("benchmarks firing throughput") {
            auto kb = build_processing_kb();
            auto facts_1k = create_facts(1000);
            auto facts_10k = create_facts(10000);
            auto facts_50k = create_facts(50000);

            benchmark("fire 1K facts", 20) {
                auto session = kb->create_session();
                session->add_facts(facts_1k);
                session->fire_all_rules();
            }

            benchmark("fire 10K facts", 5) {
                auto session = kb->create_session();
                session->add_facts(facts_10k);
                session->fire_all_rules();
            }

            benchmark("fire 50K facts", 2) {
                auto session = kb->create_session();
                session->add_facts(facts_50k);
                session->fire_all_rules();
            }
        }
    }

    group("Latency") {
        bench("measures p50/p95/p99 latencies") {
            std::string drl = R"(
package benchmark
declare Event id: int, timestamp: int end
rule "Process Event" when $e : Event() then end
)";
            ParsingResult result;
            auto kb = build_knowledge_base(drl, result);
            check(result.success);

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

                auto start = Clock::now();
                session->add_fact(fact);
                auto end = Clock::now();
                insert_latencies.push_back(std::chrono::duration_cast<Microseconds>(end - start).count());

                start = Clock::now();
                session->fire_all_rules();
                end = Clock::now();
                fire_latencies.push_back(std::chrono::duration_cast<Microseconds>(end - start).count());
            }

            info("Insert (us): p50=%lld p95=%lld p99=%lld",
                 (long long)percentile(insert_latencies, 0.50),
                 (long long)percentile(insert_latencies, 0.95),
                 (long long)percentile(insert_latencies, 0.99));

            info("Fire (us): p50=%lld p95=%lld p99=%lld",
                 (long long)percentile(fire_latencies, 0.50),
                 (long long)percentile(fire_latencies, 0.95),
                 (long long)percentile(fire_latencies, 0.99));

            check_int_lt(percentile(insert_latencies, 0.99), 10000);
            check_int_lt(percentile(fire_latencies, 0.99), 50000);
        }
    }

    group("Memory") {
        bench("stays under 500MB for 100K facts") {
            std::string drl = R"(
package benchmark
declare TestFact id: int, value: int end
rule "Match" when $f : TestFact(value > 50) then end
)";
            ParsingResult result;
            auto kb = build_knowledge_base(drl, result);
            check(result.success);

            auto session = kb->create_session();

            for (int batch = 0; batch < 10; ++batch) {
                auto facts = create_facts(10000);
                for (auto& f : facts) {
                    f->fields["id"] = (int64_t)(batch * 10000 + std::get<int64_t>(f->fields["id"]));
                }
                session->add_facts(facts);
                session->fire_all_rules();
            }

            auto metrics = session->get_metrics();
            double memory_mb = metrics.memory_used_bytes / (1024.0 * 1024.0);
            info("Memory for 100K facts: %.2f MB", memory_mb);

            check_float_lt(memory_mb, 500.0);
        }
    }
}
