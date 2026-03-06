// Fact/Rule Interaction Benchmarks
//
// Benchmarks for fact-rule interaction patterns:
// 1. Rule complexity impact on firing
// 2. Rule count scaling on firing
// 3. Incremental insert + fire (streaming)
// 4. Fact retraction throughput and latency

#include "tinytest.h"
#include "parser/rfl_parser.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"

#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <vector>

using Clock = std::chrono::high_resolution_clock;
using Microseconds = std::chrono::microseconds;

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

suite("Fact/Rule Benchmarks") {
    group("Rule Complexity vs Firing") {
        bench("benchmarks firing cost by rule complexity") {
            // Simple: single field condition
            std::string drl_simple = R"(
package benchmark
declare TestFact id: int, value: int, category: String end
rule "Simple" when $f : TestFact(value > 50) then end
)";
            // Medium: multiple field conditions
            std::string drl_medium = R"(
package benchmark
declare TestFact id: int, value: int, category: String end
rule "Medium" when $f : TestFact(value > 20, value < 80, category == "A") then end
)";
            // Complex: cross-fact join with not pattern
            std::string drl_complex = R"(
package benchmark
declare TestFact id: int, value: int, category: String end
declare ProcessedFact factId: int end
rule "Complex"
when
    $f : TestFact(value > 50)
    not ProcessedFact(factId == $f.id)
then
    insert ProcessedFact { factId = $f.id }
end
)";

            ParsingResult result;
            auto kb_simple = build_knowledge_base(drl_simple, result);
            check(result.success);
            auto kb_medium = build_knowledge_base(drl_medium, result);
            check(result.success);
            auto kb_complex = build_knowledge_base(drl_complex, result);
            check(result.success);

            auto facts = create_facts(10000);

            benchmark("fire 10K simple rule", 5) {
                auto s = kb_simple->create_session();
                s->add_facts(facts);
                s->fire_all_rules();
            }

            benchmark("fire 10K medium rule", 5) {
                auto s = kb_medium->create_session();
                s->add_facts(facts);
                s->fire_all_rules();
            }

            benchmark("fire 10K complex rule", 5) {
                auto s = kb_complex->create_session();
                s->add_facts(facts);
                s->fire_all_rules();
            }
        }
    }

    group("Rule Count vs Firing") {
        bench("fire with 10 rules") {
            ParsingResult result;
            auto kb = build_knowledge_base(generate_rules(10), result);
            check(result.success);
            auto facts = create_facts(1000);

            benchmark("fire 1K facts / 10 rules", 10) {
                auto s = kb->create_session();
                s->add_facts(facts);
                s->fire_all_rules();
            }
        }

        bench("fire with 50 rules") {
            ParsingResult result;
            auto kb = build_knowledge_base(generate_rules(50), result);
            check(result.success);
            auto facts = create_facts(1000);

            benchmark("fire 1K facts / 50 rules", 10) {
                auto s = kb->create_session();
                s->add_facts(facts);
                s->fire_all_rules();
            }
        }

        bench("fire with 100 rules") {
            ParsingResult result;
            auto kb = build_knowledge_base(generate_rules(100), result);
            check(result.success);
            auto facts = create_facts(1000);

            benchmark("fire 1K facts / 100 rules", 5) {
                auto s = kb->create_session();
                s->add_facts(facts);
                s->fire_all_rules();
            }
        }

        bench("fire with 500 rules") {
            ParsingResult result;
            auto kb = build_knowledge_base(generate_rules(500), result);
            check(result.success);
            auto facts = create_facts(200);

            benchmark("fire 200 facts / 500 rules", 3) {
                auto s = kb->create_session();
                s->add_facts(facts);
                s->fire_all_rules();
            }
        }

        bench("fire with 1000 rules") {
            ParsingResult result;
            auto kb = build_knowledge_base(generate_rules(1000), result);
            check(result.success);
            auto facts = create_facts(100);

            benchmark("fire 100 facts / 1000 rules", 2) {
                auto s = kb->create_session();
                s->add_facts(facts);
                s->fire_all_rules();
            }
        }
    }

    group("Incremental Insert + Fire") {
        bench("benchmarks streaming insert-fire pattern") {
            auto kb = build_simple_kb();
            auto facts = create_facts(10000);

            benchmark("stream 1K insert+fire", 5) {
                auto s = kb->create_session();
                for (int i = 0; i < 1000; ++i) {
                    s->add_fact(facts[i]);
                    s->fire_all_rules();
                }
            }

            benchmark("stream 10K insert+fire", 2) {
                auto s = kb->create_session();
                for (int i = 0; i < 10000; ++i) {
                    s->add_fact(facts[i]);
                    s->fire_all_rules();
                }
            }
        }

        bench("measures per-op latency for streaming") {
            try {
                auto kb = build_simple_kb();

                constexpr int N = 500;
                std::vector<int64_t> insert_us, fire_us;
                insert_us.reserve(N);
                fire_us.reserve(N);

                auto session = kb->create_session();
                for (int i = 0; i < N; ++i) {
                    auto fact = std::make_shared<Fact>();
                    fact->type = "benchmark.TestFact";
                    fact->fields["id"] = (int64_t)i;
                    fact->fields["value"] = (int64_t)(i % 110);
                    fact->fields["category"] = std::string("A");

                    auto t0 = Clock::now();
                    session->add_fact(fact);
                    auto t1 = Clock::now();
                    session->fire_all_rules();
                    auto t2 = Clock::now();

                    insert_us.push_back(std::chrono::duration_cast<Microseconds>(t1 - t0).count());
                    fire_us.push_back(std::chrono::duration_cast<Microseconds>(t2 - t1).count());
                }

                info("Stream insert (us): p50=%lld p95=%lld p99=%lld",
                     (long long)percentile(insert_us, 0.50),
                     (long long)percentile(insert_us, 0.95),
                     (long long)percentile(insert_us, 0.99));

                info("Stream fire (us): p50=%lld p95=%lld p99=%lld",
                     (long long)percentile(fire_us, 0.50),
                     (long long)percentile(fire_us, 0.95),
                     (long long)percentile(fire_us, 0.99));

                check_int_lt(percentile(insert_us, 0.99), 10000);
                check_int_lt(percentile(fire_us, 0.99), 50000);
            } catch (std::exception const& e) {
                info("EXCEPTION: %s", e.what());
                check(false);
            }
        }
    }

    group("Fact Retraction") {
        bench("benchmarks retract throughput") {
            auto kb = build_simple_kb();
            auto facts_1k = create_facts(1000);
            auto facts_10k = create_facts(10000);

            benchmark("retract 1K facts", 10) {
                auto s = kb->create_session();
                s->add_facts(facts_1k);
                s->fire_all_rules();
                for (auto& f : facts_1k) {
                    s->retract_fact(f);
                }
                s->fire_all_rules();
            }

            benchmark("retract 10K facts", 3) {
                auto s = kb->create_session();
                s->add_facts(facts_10k);
                s->fire_all_rules();
                for (auto& f : facts_10k) {
                    s->retract_fact(f);
                }
                s->fire_all_rules();
            }
        }

        bench("measures retract latency percentiles") {
            auto kb = build_simple_kb();
            auto session = kb->create_session();
            auto facts = create_facts(1000);
            session->add_facts(facts);
            session->fire_all_rules();

            std::vector<int64_t> retract_us;
            retract_us.reserve(facts.size());

            for (auto& f : facts) {
                auto t0 = Clock::now();
                session->retract_fact(f);
                auto t1 = Clock::now();
                retract_us.push_back(std::chrono::duration_cast<Microseconds>(t1 - t0).count());
            }

            info("Retract (us): p50=%lld p95=%lld p99=%lld",
                 (long long)percentile(retract_us, 0.50),
                 (long long)percentile(retract_us, 0.95),
                 (long long)percentile(retract_us, 0.99));

            check_int_lt(percentile(retract_us, 0.99), 10000);
        }
    }
}
