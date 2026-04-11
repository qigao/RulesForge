#include "data/token_pool.hpp"
#include "data/token_arena.hpp"
#include "core/token.hpp"
#include "core/fact.hpp"

#include "tinytest.h"

#include <vector>
#include <random>

using namespace rulesforge;

suite("TokenPool Benchmarks") {
    bench("Performance Comparison") {
        benchmark("TokenPool: 10k alloc/free", 1, 1.0) {
            TokenPool pool(10000);

            std::vector<Fact> facts(10000);
            for (size_t i = 0; i < 10000; ++i) {
                facts[i].id = static_cast<int64_t>(i);
            }

            std::vector<TokenWME*> tokens;
            tokens.reserve(10000);

            for (size_t i = 0; i < 10000; ++i) {
                tokens.push_back(pool.create_token(nullptr, &facts[i]));
            }

            for (auto* token : tokens) {
                pool.destroy_token(token);
            }
        }

        benchmark("TokenArena: 10k alloc", 1, 1.0) {
            TokenArena arena(64 * 1024 * 1024);

            std::vector<Fact> facts(10000);
            for (size_t i = 0; i < 10000; ++i) {
                facts[i].id = static_cast<int64_t>(i);
            }

            for (size_t i = 0; i < 10000; ++i) {
                arena.create_token(nullptr, &facts[i]);
            }

            // Cannot free individual tokens
        }

        benchmark("TokenPool: ASSERT/RETRACT pattern", 1, 1.0) {
            TokenPool pool(1000);

            std::vector<Fact> facts(100);
            for (size_t i = 0; i < 100; ++i) {
                facts[i].id = static_cast<int64_t>(i);
            }

            for (int cycle = 0; cycle < 100; ++cycle) {
                std::vector<TokenWME*> tokens;

                for (size_t i = 0; i < 10; ++i) {
                    tokens.push_back(pool.create_token(nullptr, &facts[i]));
                }

                for (auto* token : tokens) {
                    pool.destroy_token(token);
                }
            }
        }

        benchmark("TokenArena: ASSERT/RETRACT pattern", 1, 1.0) {
            TokenArena arena(64 * 1024 * 1024);

            std::vector<Fact> facts(100);
            for (size_t i = 0; i < 100; ++i) {
                facts[i].id = static_cast<int64_t>(i);
            }

            for (int cycle = 0; cycle < 100; ++cycle) {
                for (size_t i = 0; i < 10; ++i) {
                    arena.create_token(nullptr, &facts[i]);
                }

                // Cannot free - memory leaks
            }
        }

        benchmark("TokenPool: random depth tokens", 1, 1.0) {
            TokenPool pool(10000);

            std::vector<Fact> facts(1000);
            for (size_t i = 0; i < 1000; ++i) {
                facts[i].id = static_cast<int64_t>(i);
            }

            std::mt19937 rng(42);
            std::uniform_int_distribution<size_t> dist(0, 999);

            std::vector<TokenWME*> tokens;
            tokens.push_back(pool.create_token(nullptr, &facts[0]));

            for (int i = 1; i < 10000; ++i) {
                size_t parent_idx = dist(rng) % tokens.size();
                TokenWME* parent = tokens[parent_idx];
                size_t fact_idx = dist(rng);

                tokens.push_back(pool.create_token(parent, &facts[fact_idx]));
            }

            for (auto* token : tokens) {
                pool.destroy_token(token);
            }
        }

        benchmark("TokenArena: random depth tokens", 1, 1.0) {
            TokenArena arena(64 * 1024 * 1024);

            std::vector<Fact> facts(1000);
            for (size_t i = 0; i < 1000; ++i) {
                facts[i].id = static_cast<int64_t>(i);
            }

            std::mt19937 rng(42);
            std::uniform_int_distribution<size_t> dist(0, 999);

            std::vector<TokenWME*> tokens;
            tokens.push_back(arena.create_token(nullptr, &facts[0]));

            for (int i = 1; i < 10000; ++i) {
                size_t parent_idx = dist(rng) % tokens.size();
                TokenWME* parent = tokens[parent_idx];
                size_t fact_idx = dist(rng);

                tokens.push_back(arena.create_token(parent, &facts[fact_idx]));
            }
        }

        benchmark("TokenPool: statistics overhead", 1, 1.0) {
            TokenPool pool(10000);

            Fact fact;
            fact.id = 1;

            for (int i = 0; i < 10000; ++i) {
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
