#include "engine/agenda.hpp"
#include "engine/agenda_v2.hpp"
#include "engine/bucket_priority_queue.hpp"
#include "engine/priority_types.hpp"
#include "core/parsed_rule.hpp"

#include "tinytest.h"

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

suite("Agenda Benchmarks") {
    bench("Original Agenda") {
        benchmark("add 10k operations", 1, 1.0) {
            Agenda agenda;
            for (size_t i = 0; i < 10000; ++i) {
                int salience = static_cast<int>(i % 2000) - 1000;
                agenda.add(create_mock_activation(i, salience));
            }
        }

        benchmark("pop 10k operations", 1, 1.0) {
            Agenda agenda;
            for (size_t i = 0; i < 10000; ++i) {
                int salience = static_cast<int>(i % 2000) - 1000;
                agenda.add(create_mock_activation(i, salience));
            }
            for (size_t i = 0; i < 10000; ++i) {
                agenda.pop_next();
            }
        }

        benchmark("mixed 1k add+pop", 1, 1.0) {
            Agenda agenda;
            for (int j = 0; j < 1000; ++j) {
                for (int i = 0; i < 10; ++i) {
                    int salience = static_cast<int>((j * 10 + i) % 2000) - 1000;
                    agenda.add(create_mock_activation(j * 10 + i, salience));
                }
                for (int i = 0; i < 5; ++i) {
                    agenda.pop_next();
                }
            }
        }
    }

    bench("AgendaV2") {
        benchmark("add 10k operations", 1, 1.0) {
            AgendaV2 agenda;
            for (size_t i = 0; i < 10000; ++i) {
                int salience = static_cast<int>(i % 2000) - 1000;
                agenda.add(create_mock_activation(i, salience));
            }
        }

        benchmark("pop 10k operations", 1, 1.0) {
            AgendaV2 agenda;
            for (size_t i = 0; i < 10000; ++i) {
                int salience = static_cast<int>(i % 2000) - 1000;
                agenda.add(create_mock_activation(i, salience));
            }
            for (size_t i = 0; i < 10000; ++i) {
                agenda.pop_next();
            }
        }

        benchmark("mixed 1k add+pop", 1, 1.0) {
            AgendaV2 agenda;
            for (int j = 0; j < 1000; ++j) {
                for (int i = 0; i < 10; ++i) {
                    int salience = static_cast<int>((j * 10 + i) % 2000) - 1000;
                    agenda.add(create_mock_activation(j * 10 + i, salience));
                }
                for (int i = 0; i < 5; ++i) {
                    agenda.pop_next();
                }
            }
        }

        benchmark("batch pop 10k", 1, 1.0) {
            AgendaV2 agenda;
            for (size_t i = 0; i < 10000; ++i) {
                int salience = static_cast<int>(i % 2000) - 1000;
                agenda.add(create_mock_activation(i, salience));
            }
            for (size_t i = 0; i < 100; ++i) {
                agenda.pop_next_batch(100);
            }
        }
    }

    bench("Priority Queues") {
        benchmark("BucketQueue push 10k", 1, 1.0) {
            BucketPriorityQueue queue;
            for (size_t i = 0; i < 10000; ++i) {
                Priority p = static_cast<Priority>(i % 4);
                queue.push(p, i);
            }
        }

        benchmark("BucketQueue pop 10k", 1, 1.0) {
            BucketPriorityQueue queue;
            for (size_t i = 0; i < 10000; ++i) {
                Priority p = static_cast<Priority>(i % 4);
                queue.push(p, i);
            }
            for (size_t i = 0; i < 10000; ++i) {
                queue.pop();
            }
        }

        benchmark("BucketQueue batch pop", 1, 1.0) {
            BucketPriorityQueue queue;
            for (size_t i = 0; i < 10000; ++i) {
                Priority p = static_cast<Priority>(i % 4);
                queue.push(p, i);
            }
            for (size_t i = 0; i < 100; ++i) {
                queue.pop_batch(100);
            }
        }
    }
}
