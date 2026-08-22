#include "data/token_pool.hpp"
#include "data/token_arena.hpp"
#include "core/token.hpp"
#include "core/fact.hpp"

#include "tinytest.hpp"

#include <vector>
#include <random>

using namespace rulesforge;

suite("TokenPool Benchmarks") {
    bench("Performance Comparison") {
        constexpr size_t kTokenCount = 10000;
        constexpr size_t kFactCount = 1000;
        constexpr size_t kCycleCount = 100;
        constexpr size_t kTokensPerCycle = 10;
        constexpr size_t kBenchmarkSamples = 10;

        benchmark("TokenPool: 10k alloc/free", kBenchmarkSamples,
                  static_cast<double>(kTokenCount)) {
            TokenPool pool(kTokenCount);

            std::vector<Fact> facts(kTokenCount);
            for (size_t i = 0; i < kTokenCount; ++i) {
                facts[i].id = static_cast<int64_t>(i);
            }

            std::vector<TokenWME*> tokens;
            tokens.reserve(kTokenCount);

            for (size_t i = 0; i < kTokenCount; ++i) {
                tokens.push_back(pool.create_token(nullptr, &facts[i]));
            }

            for (auto* token : tokens) {
                pool.destroy_token(token);
            }
        }

        benchmark("TokenArena: 10k alloc", kBenchmarkSamples,
                  static_cast<double>(kTokenCount)) {
            TokenArena arena(64 * 1024 * 1024);

            std::vector<Fact> facts(kTokenCount);
            for (size_t i = 0; i < kTokenCount; ++i) {
                facts[i].id = static_cast<int64_t>(i);
            }

            for (size_t i = 0; i < kTokenCount; ++i) {
                arena.create_token(nullptr, &facts[i]);
            }

            // Cannot free individual tokens
        }

        benchmark("TokenPool: ASSERT/RETRACT pattern", kBenchmarkSamples,
                  static_cast<double>(kCycleCount * kTokensPerCycle)) {
            TokenPool pool(1000);

            std::vector<Fact> facts(100);
            for (size_t i = 0; i < 100; ++i) {
                facts[i].id = static_cast<int64_t>(i);
            }

            for (size_t cycle = 0; cycle < kCycleCount; ++cycle) {
                std::vector<TokenWME*> tokens;
                tokens.reserve(kTokensPerCycle);

                for (size_t i = 0; i < kTokensPerCycle; ++i) {
                    tokens.push_back(pool.create_token(nullptr, &facts[i]));
                }

                for (auto* token : tokens) {
                    pool.destroy_token(token);
                }
            }
        }

        benchmark("TokenArena: ASSERT/RETRACT pattern", kBenchmarkSamples,
                  static_cast<double>(kCycleCount * kTokensPerCycle)) {
            TokenArena arena(64 * 1024 * 1024);

            std::vector<Fact> facts(100);
            for (size_t i = 0; i < 100; ++i) {
                facts[i].id = static_cast<int64_t>(i);
            }

            for (size_t cycle = 0; cycle < kCycleCount; ++cycle) {
                for (size_t i = 0; i < kTokensPerCycle; ++i) {
                    arena.create_token(nullptr, &facts[i]);
                }

                // Cannot free - memory leaks
            }
        }

        benchmark("TokenPool: random depth tokens", kBenchmarkSamples,
                  static_cast<double>(kTokenCount)) {
            TokenPool pool(kTokenCount);

            std::vector<Fact> facts(kFactCount);
            for (size_t i = 0; i < kFactCount; ++i) {
                facts[i].id = static_cast<int64_t>(i);
            }

            std::mt19937 rng(42);
            std::uniform_int_distribution<size_t> dist(0, 999);

            std::vector<TokenWME*> tokens;
            tokens.push_back(pool.create_token(nullptr, &facts[0]));

            for (size_t i = 1; i < kTokenCount; ++i) {
                size_t parent_idx = dist(rng) % tokens.size();
                TokenWME* parent = tokens[parent_idx];
                size_t fact_idx = dist(rng);

                tokens.push_back(pool.create_token(parent, &facts[fact_idx]));
            }

            for (auto* token : tokens) {
                pool.destroy_token(token);
            }
        }

        benchmark("TokenArena: random depth tokens", kBenchmarkSamples,
                  static_cast<double>(kTokenCount)) {
            TokenArena arena(64 * 1024 * 1024);

            std::vector<Fact> facts(kFactCount);
            for (size_t i = 0; i < kFactCount; ++i) {
                facts[i].id = static_cast<int64_t>(i);
            }

            std::mt19937 rng(42);
            std::uniform_int_distribution<size_t> dist(0, 999);

            std::vector<TokenWME*> tokens;
            tokens.push_back(arena.create_token(nullptr, &facts[0]));

            for (size_t i = 1; i < kTokenCount; ++i) {
                size_t parent_idx = dist(rng) % tokens.size();
                TokenWME* parent = tokens[parent_idx];
                size_t fact_idx = dist(rng);

                tokens.push_back(arena.create_token(parent, &facts[fact_idx]));
            }
        }

        benchmark("TokenPool: statistics overhead", kBenchmarkSamples,
                  static_cast<double>(kTokenCount)) {
            TokenPool pool(kTokenCount);

            Fact fact;
            fact.id = 1;

            for (size_t i = 0; i < kTokenCount; ++i) {
                TokenWME* token = pool.create_token(nullptr, &fact);

                // Access statistics
                volatile size_t allocated = pool.allocated_count();
                volatile size_t free_count = pool.free_count();
                volatile size_t capacity = pool.capacity();
                volatile size_t peak = pool.peak_usage();
                (void)allocated;
                (void)free_count;
                (void)capacity;
                (void)peak;

                pool.destroy_token(token);
            }
        }
    }
}
