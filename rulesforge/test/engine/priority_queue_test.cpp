#include "engine/priority_disruptor.hpp"
#include "engine/priority_ring_buffer.hpp"
#include "engine/priority_types.hpp"

#include "tinytest.h"

#include <chrono>
#include <iostream>
#include <thread>

suite("Priority Queue") {
    group("PriorityRingBuffer") {
        it("enqueues and dequeues in priority order") {
            PriorityRingBuffer queue;

            // Enqueue with different priorities
            check(queue.try_enqueue(Priority::LOW, 100));
            check(queue.try_enqueue(Priority::HIGH, 200));
            check(queue.try_enqueue(Priority::CRITICAL, 300));
            check(queue.try_enqueue(Priority::NORMAL, 400));

            // Should dequeue in priority order: CRITICAL, HIGH, NORMAL, LOW
            auto hash1 = queue.try_dequeue();
            check(hash1.has_value());
            check(*hash1 == 300);  // CRITICAL

            auto hash2 = queue.try_dequeue();
            check(hash2.has_value());
            check(*hash2 == 200);  // HIGH

            auto hash3 = queue.try_dequeue();
            check(hash3.has_value());
            check(*hash3 == 400);  // NORMAL

            auto hash4 = queue.try_dequeue();
            check(hash4.has_value());
            check(*hash4 == 100);  // LOW

            // Should be empty now
            auto hash5 = queue.try_dequeue();
            check(!hash5.has_value());
        }

        it("supports batch dequeue") {
            PriorityRingBuffer queue;

            // Enqueue multiple items
            for (size_t i = 0; i < 10; ++i) {
                queue.try_enqueue(Priority::HIGH, i);
            }
            for (size_t i = 10; i < 20; ++i) {
                queue.try_enqueue(Priority::LOW, i);
            }

            // Batch dequeue should get HIGH priority first
            auto batch = queue.try_dequeue_batch(15);
            check(batch.size() == 15);

            // First 10 should be HIGH priority (hashes 0-9)
            for (size_t i = 0; i < 10; ++i) {
                check(batch[i] == i);
            }

            // Next 5 should be LOW priority (hashes 10-14)
            for (size_t i = 10; i < 15; ++i) {
                check(batch[i] == i);
            }
        }

        it("handles empty queue") {
            PriorityRingBuffer queue;

            auto hash = queue.try_dequeue();
            check(!hash.has_value());

            check(queue.empty());
        }
    }

    group("PriorityDisruptor") {
        it("enqueues and dequeues in priority order") {
            PriorityDisruptor queue;

            // Enqueue with different priorities
            check(queue.try_enqueue(Priority::LOW, 100));
            check(queue.try_enqueue(Priority::HIGH, 200));
            check(queue.try_enqueue(Priority::CRITICAL, 300));

            // Should dequeue in priority order
            auto hash1 = queue.try_dequeue();
            check(hash1.has_value());
            check(*hash1 == 300);  // CRITICAL

            auto hash2 = queue.try_dequeue();
            check(hash2.has_value());
            check(*hash2 == 200);  // HIGH

            auto hash3 = queue.try_dequeue();
            check(hash3.has_value());
            check(*hash3 == 100);  // LOW
        }

        it("supports multi-threaded producers") {
            PriorityDisruptor queue;
            constexpr size_t NUM_PRODUCERS = 2;
            constexpr size_t NUM_OPS_PER_PRODUCER = 100;

            std::atomic<size_t> consumed{0};
            std::atomic<bool> consumer_running{true};

            // Consumer thread with its own sequence tracking
            std::thread consumer_thread([&]() {
                while (consumer_running || consumed < NUM_PRODUCERS * NUM_OPS_PER_PRODUCER) {
                    auto hash = queue.try_dequeue();
                    if (hash) {
                        consumed++;
                    } else {
                        std::this_thread::sleep_for(std::chrono::microseconds(10));
                    }
                }
            });

            // Producer threads
            std::vector<std::thread> producers;
            for (size_t t = 0; t < NUM_PRODUCERS; ++t) {
                producers.emplace_back([&queue, t]() {
                    for (size_t i = 0; i < NUM_OPS_PER_PRODUCER; ++i) {
                        Priority p = static_cast<Priority>(i % 4);
                        queue.enqueue_blocking(p, t * NUM_OPS_PER_PRODUCER + i);
                    }
                });
            }

            // Wait for producers
            for (auto& t : producers) {
                t.join();
            }

            // Wait for consumer to finish (with timeout)
            auto start = std::chrono::steady_clock::now();
            while (consumed < NUM_PRODUCERS * NUM_OPS_PER_PRODUCER) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - start).count() > 5) {
                    std::cout << "Timeout! Consumed: " << consumed << " / "
                              << NUM_PRODUCERS * NUM_OPS_PER_PRODUCER << std::endl;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            consumer_running = false;
            consumer_thread.join();

            check(consumed == NUM_PRODUCERS * NUM_OPS_PER_PRODUCER);
        }
    }

    group("Priority Mapping") {
        it("maps salience to priority correctly") {
            check(salience_to_priority(2000) == Priority::CRITICAL);
            check(salience_to_priority(1000) == Priority::CRITICAL);
            check(salience_to_priority(500) == Priority::HIGH);
            check(salience_to_priority(0) == Priority::HIGH);
            check(salience_to_priority(-500) == Priority::NORMAL);
            check(salience_to_priority(-1000) == Priority::NORMAL);
            check(salience_to_priority(-2000) == Priority::LOW);
        }
    }

    group("Performance") {
        it("PriorityRingBuffer achieves high throughput") {
            PriorityRingBuffer queue;
            constexpr size_t NUM_OPS = 10000;

            auto start = std::chrono::high_resolution_clock::now();

            // Enqueue
            for (size_t i = 0; i < NUM_OPS; ++i) {
                Priority p = static_cast<Priority>(i % 4);
                while (!queue.try_enqueue(p, i)) {
                    // Retry on full
                }
            }

            // Dequeue
            for (size_t i = 0; i < NUM_OPS; ++i) {
                auto hash = queue.try_dequeue();
                while (!hash) {
                    hash = queue.try_dequeue();
                }
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            double ops_per_sec = (NUM_OPS * 2.0) / (duration.count() / 1000.0);
            std::cout << "PriorityRingBuffer: " << ops_per_sec / 1e6 << " M ops/sec" << std::endl;

            // Should be > 5M ops/sec on modern hardware
            check(ops_per_sec > 5e6);
        }

        it("PriorityDisruptor achieves high throughput") {
            PriorityDisruptor queue;

            constexpr size_t NUM_OPS = 10000;

            auto start = std::chrono::high_resolution_clock::now();

            // Enqueue
            for (size_t i = 0; i < NUM_OPS; ++i) {
                Priority p = static_cast<Priority>(i % 4);
                while (!queue.try_enqueue(p, i)) {
                    // Retry on full
                }
            }

            // Dequeue
            for (size_t i = 0; i < NUM_OPS; ++i) {
                auto hash = queue.try_dequeue();
                while (!hash) {
                    hash = queue.try_dequeue();
                }
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            double ops_per_sec = (NUM_OPS * 2.0) / (duration.count() / 1000.0);
            std::cout << "PriorityDisruptor: " << ops_per_sec / 1e6 << " M ops/sec" << std::endl;

            // Should be > 3M ops/sec (slightly slower than SPSC due to MPMC overhead)
            check(ops_per_sec > 3e6);
        }
    }
}
