#include "data/token_pool.hpp"
#include "data/token_arena.hpp"
#include "core/token.hpp"
#include "core/fact.hpp"

#include "tinytest.hpp"

#include <vector>

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

            check(pool.allocated_count() == 3);

            pool.destroy_token(token2);
            check(pool.allocated_count() == 2);

            pool.destroy_token(token1);
            check(pool.allocated_count() == 1);
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

            check(addr1 == addr2);

            pool.destroy_token(token2);
        }

        it("TokenPool reserves requested user capacity plus root") {
            constexpr size_t kUserCapacity = 10;
            TokenPool pool(kUserCapacity);

            check_equal(pool.capacity(), kUserCapacity + 1);
            check_equal(pool.allocated_count(), 1);
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
            check(token1 != nullptr);
            check(token2 != nullptr);
            check(usage_before > 0);

            arena.reset();
            size_t usage_after = arena.memory_usage();

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

            for (int cycle = 0; cycle < 1000; ++cycle) {
                std::vector<TokenWME*> tokens;

                for (size_t i = 0; i < 10; ++i) {
                    tokens.push_back(pool.create_token(nullptr, &facts[i]));
                }

                for (auto* token : tokens) {
                    pool.destroy_token(token);
                }
            }

            check(pool.allocated_count() == 1);
            check(pool.capacity() < 200);
        }

        it("TokenArena leaks memory in ASSERT/RETRACT cycles") {
            TokenArena arena(1024);

            std::vector<Fact> facts(100);
            for (size_t i = 0; i < 100; ++i) {
                facts[i].id = static_cast<int64_t>(i);
            }

            size_t initial_usage = arena.memory_usage();

            for (int cycle = 0; cycle < 100; ++cycle) {
                for (size_t i = 0; i < 10; ++i) {
                    arena.create_token(nullptr, &facts[i]);
                }
            }

            size_t final_usage = arena.memory_usage();

            check(final_usage > initial_usage);
            check(final_usage >= initial_usage + 100 * 10 * sizeof(TokenWME));
        }
    }
}
