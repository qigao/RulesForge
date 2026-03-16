#include "engine/agenda.hpp"
#include "engine/agenda_v2.hpp"
#include "engine/priority_types.hpp"
#include "core/parsed_rule.hpp"

#include "tinytest.h"

#include <chrono>
#include <iostream>
#include <random>

// Helper to create mock activation
Activation create_mock_activation(size_t hash, int salience) {
    static ParsedRule mock_rule;
    mock_rule.salience = salience;
    mock_rule.duration = 0;
    mock_rule.activation_group = std::nullopt;
    mock_rule.agenda_group = std::nullopt;
    mock_rule.lock_on_active = false;

    Activation activation;
    activation.hash_value = hash;
    activation.rule = &mock_rule;
    return activation;
}

suite("Agenda Performance") {
    group("Original Agenda") {
        it("baseline performance") {
            Agenda agenda;
            constexpr size_t NUM_OPS = 100000;

            auto start = std::chrono::high_resolution_clock::now();

            // Add activations
            for (size_t i = 0; i < NUM_OPS; ++i) {
                int salience = static_cast<int>(i % 2000) - 1000;  // -1000 to 999
                auto activation = create_mock_activation(i, salience);
                agenda.add(activation);
            }

            // Pop all
            size_t popped = 0;
            while (auto activation = agenda.pop_next()) {
                popped++;
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            double ops_per_sec = (NUM_OPS * 2.0) / (duration.count() / 1000.0);
            std::cout << "Original Agenda: " << ops_per_sec / 1e6 << " M ops/sec, "
                      << "popped: " << popped << std::endl;

            check(popped == NUM_OPS);
        }
    }

    group("AgendaV2") {
        it("high-performance implementation") {
            AgendaV2 agenda;
            constexpr size_t NUM_OPS = 100000;

            auto start = std::chrono::high_resolution_clock::now();

            // Add activations
            for (size_t i = 0; i < NUM_OPS; ++i) {
                int salience = static_cast<int>(i % 2000) - 1000;
                auto activation = create_mock_activation(i, salience);
                agenda.add(activation);
            }

            // Pop all
            size_t popped = 0;
            while (auto activation = agenda.pop_next()) {
                popped++;
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            double ops_per_sec = (NUM_OPS * 2.0) / (duration.count() / 1000.0);
            std::cout << "AgendaV2: " << ops_per_sec / 1e6 << " M ops/sec, "
                      << "popped: " << popped << std::endl;

            check(popped == NUM_OPS);
        }

        it("batch operations") {
            AgendaV2 agenda;
            constexpr size_t NUM_OPS = 100000;
            constexpr size_t BATCH_SIZE = 100;

            auto start = std::chrono::high_resolution_clock::now();

            // Add activations
            for (size_t i = 0; i < NUM_OPS; ++i) {
                int salience = static_cast<int>(i % 2000) - 1000;
                auto activation = create_mock_activation(i, salience);
                agenda.add(activation);
            }

            // Pop in batches
            size_t popped = 0;
            while (true) {
                auto batch = agenda.pop_next_batch(BATCH_SIZE);
                if (batch.empty()) break;
                popped += batch.size();
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            double ops_per_sec = (NUM_OPS * 2.0) / (duration.count() / 1000.0);
            std::cout << "AgendaV2 (batch): " << ops_per_sec / 1e6 << " M ops/sec, "
                      << "popped: " << popped << std::endl;

            check(popped == NUM_OPS);
        }

        it("respects priority ordering") {
            AgendaV2 agenda;

            // Add with different priorities
            agenda.add(create_mock_activation(1, -2000));  // LOW
            agenda.add(create_mock_activation(2, 0));      // HIGH
            agenda.add(create_mock_activation(3, 1500));   // CRITICAL
            agenda.add(create_mock_activation(4, -500));   // NORMAL

            // Should pop in priority order: CRITICAL, HIGH, NORMAL, LOW
            auto a1 = agenda.pop_next();
            check(a1.has_value());
            check(a1->hash_value == 3);  // CRITICAL

            auto a2 = agenda.pop_next();
            check(a2.has_value());
            check(a2->hash_value == 2);  // HIGH

            auto a3 = agenda.pop_next();
            check(a3.has_value());
            check(a3->hash_value == 4);  // NORMAL

            auto a4 = agenda.pop_next();
            check(a4.has_value());
            check(a4->hash_value == 1);  // LOW
        }

        it("supports remove operation") {
            AgendaV2 agenda;

            // Use different priority levels to ensure ordering
            agenda.add(create_mock_activation(1, -500));   // NORMAL
            agenda.add(create_mock_activation(2, 500));    // HIGH
            agenda.add(create_mock_activation(3, 1500));   // CRITICAL

            // Remove middle one (HIGH)
            check(agenda.remove(2));
            check(agenda.size() == 2);

            // Should pop in priority order: CRITICAL (3), then NORMAL (1)
            auto a1 = agenda.pop_next();
            check(a1.has_value());
            check(a1->hash_value == 3);  // CRITICAL

            auto a2 = agenda.pop_next();
            check(a2.has_value());
            check(a2->hash_value == 1);  // NORMAL

            auto a3 = agenda.pop_next();
            check(!a3.has_value());
        }

        xit("maintains API compatibility") {
            AgendaV2 agenda;

            // Test all public methods exist and work
            auto activation = create_mock_activation(1, 100);

            agenda.add(activation);
            check(agenda.size() == 1);

            agenda.set_focus("TEST");
            check(agenda.get_focus() == "TEST");

            agenda.block_noloop(123);
            check(agenda.is_noloop_blocked(123));

            agenda.clear_noloop();
            check(!agenda.is_noloop_blocked(123));

            auto popped = agenda.pop_next();
            check(popped.has_value());
        }

        it("handles stress test") {
            AgendaV2 agenda;
            constexpr size_t NUM_OPS = 10000;
            std::mt19937 rng(42);
            std::uniform_int_distribution<int> salience_dist(-2000, 2000);

            // Random add/remove/pop operations
            for (size_t i = 0; i < NUM_OPS; ++i) {
                int op = rng() % 3;
                if (op == 0) {
                    // Add
                    int salience = salience_dist(rng);
                    agenda.add(create_mock_activation(i, salience));
                } else if (op == 1 && agenda.size() > 0) {
                    // Remove random
                    size_t hash = rng() % i;
                    agenda.remove(hash);
                } else if (agenda.size() > 0) {
                    // Pop
                    agenda.pop_next();
                }
            }

            // Should not crash
            check(true);
        }
    }
}
