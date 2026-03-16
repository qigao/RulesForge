#include "data/token_pool.hpp"
#include "data/token_arena.hpp"
#include "core/token.hpp"
#include "core/fact.hpp"

#include "tinytest.h"

#include <vector>
#include <random>

using namespace rulesforge;

suite("TokenPool vs TokenArena") {
    group("Correctness") {
        it("TokenPool creates and destroys tokens") {
            TokenPool pool(10);

            Fact fact1;
            fact1.id = 1;
            Fact fact2;
            fact2.id = 2;

            TokenWME* token1 = pool.create_token(nullptr, &fact1);
            check(token1 != nullptr);
            check(token1->fact == &fact1);
            check(token1->depth == 1);

            TokenWME* token2 = pool.create_token(token1, &fact2);
            check(token2 != nullptr);
            check(token2->fact == &fact2);
            check(token2->depth == 2);
            check(token2->parent == token1);

            // allocated_count includes root token
            check(pool.allocated_count() == 3);  // root + token1 + token2

            pool.destroy_token(token2);
            check(pool.allocated_count() == 2);  // root + token1

            pool.destroy_token(token1);
            check(pool.allocated_count() == 1);  // root only
        }

        it("TokenPool reuses freed tokens") {
            TokenPool pool(1);

            Fact fact;
            fact.id = 1;

            TokenWME* token1 = pool.create_token(nullptr, &fact);
            void* addr1 = token1;

            pool.destroy_token(token1);

            TokenWME* token2 = pool.create_token(nullptr, &fact);
            void* addr2 = token2;

            // Should reuse the same memory
            check(addr1 == addr2);

            pool.destroy_token(token2);
        }

        it("TokenArena cannot free individual tokens") {
            TokenArena arena(1024);

            Fact fact1;
            fact1.id = 1;
            Fact fact2;
            fact2.id = 2;

            TokenWME* token1 = arena.create_token(nullptr, &fact1);
            TokenWME* token2 = arena.create_token(token1, &fact2);

            size_t usage_before = arena.memory_usage();

            // TokenArena has no destroy_token() method
            // Memory stays allocated until reset()

            check(usage_before > 0);

            arena.reset();
            size_t usage_after = arena.memory_usage();

            // After reset, memory is reclaimed
            check(usage_after < usage_before);
        }
    }

    group("Memory Leak Simulation") {
        it("TokenPool handles ASSERT/RETRACT cycles") {
            TokenPool pool(100);

            std::vector<Fact> facts(100);
            for (size_t i = 0; i < 100; ++i) {
                facts[i].id = static_cast<int64_t>(i);
            }

            // Simulate 1000 ASSERT/RETRACT cycles
            for (int cycle = 0; cycle < 1000; ++cycle) {
                std::vector<TokenWME*> tokens;

                // ASSERT phase
                for (size_t i = 0; i < 10; ++i) {
                    TokenWME* token = pool.create_token(nullptr, &facts[i]);
                    tokens.push_back(token);
                }

                // RETRACT phase
                for (auto* token : tokens) {
                    pool.destroy_token(token);
                }
            }

            // Memory should be stable (no leak)
            // allocated_count includes root token
            check(pool.allocated_count() == 1);  // Only root remains
            size_t final_capacity = pool.capacity();

            // Capacity should stabilize (not grow indefinitely)
            check(final_capacity < 200);  // Should not exceed 2x initial
        }

        it("TokenArena leaks memory in ASSERT/RETRACT cycles") {
            TokenArena arena(1024);

            std::vector<Fact> facts(100);
            for (size_t i = 0; i < 100; ++i) {
                facts[i].id = static_cast<int64_t>(i);
            }

            size_t initial_usage = arena.memory_usage();

            // Simulate 100 ASSERT/RETRACT cycles
            for (int cycle = 0; cycle < 100; ++cycle) {
                // ASSERT phase
                for (size_t i = 0; i < 10; ++i) {
                    arena.create_token(nullptr, &facts[i]);
                }

                // RETRACT phase - but memory cannot be freed!
                // In real code, tokens are removed from memory sets
                // but arena memory stays allocated
            }

            size_t final_usage = arena.memory_usage();

            // Memory keeps growing (leak)
            check(final_usage > initial_usage);
            check(final_usage >= initial_usage + 100 * 10 * sizeof(TokenWME));
        }
    }

    bench("Performance Comparison") {
        benchmark("TokenPool: 10k alloc/free", 1) {
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

        benchmark("TokenArena: 10k alloc", 1) {
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

        benchmark("TokenPool: ASSERT/RETRACT pattern", 1) {
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

        benchmark("TokenArena: ASSERT/RETRACT pattern", 1) {
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

        benchmark("TokenPool: random depth tokens", 1) {
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

        benchmark("TokenArena: random depth tokens", 1) {
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

        benchmark("TokenPool: statistics overhead", 1) {
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
