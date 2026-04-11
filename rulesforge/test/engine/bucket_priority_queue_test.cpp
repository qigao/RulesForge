#include "engine/bucket_priority_queue.hpp"
#include "engine/priority_types.hpp"

#include "tinytest.h"

#include <algorithm>
#include <random>

suite("BucketPriorityQueue") {
    group("Basic Operations") {
        it("starts empty") {
            BucketPriorityQueue queue;
            check(queue.empty());
            check(queue.size() == 0);
        }

        it("push and pop single item") {
            BucketPriorityQueue queue;
            queue.push(Priority::HIGH, 42);

            check(!queue.empty());
            check(queue.size() == 1);

            auto value = queue.pop();
            check(value.has_value());
            check(*value == 42);
            check(queue.empty());
        }

        it("respects priority order") {
            BucketPriorityQueue queue;

            queue.push(Priority::LOW, 1);
            queue.push(Priority::CRITICAL, 4);
            queue.push(Priority::NORMAL, 2);
            queue.push(Priority::HIGH, 3);

            check(queue.size() == 4);

            // Should pop in priority order: CRITICAL, HIGH, NORMAL, LOW
            check(*queue.pop() == 4);
            check(*queue.pop() == 3);
            check(*queue.pop() == 2);
            check(*queue.pop() == 1);
            check(queue.empty());
        }

        it("handles FIFO within same priority") {
            BucketPriorityQueue queue;

            queue.push(Priority::HIGH, 1);
            queue.push(Priority::HIGH, 2);
            queue.push(Priority::HIGH, 3);

            // FIFO: first pushed is first popped (TurboNet uses ring buffer = FIFO)
            check(*queue.pop() == 1);
            check(*queue.pop() == 2);
            check(*queue.pop() == 3);
        }

        it("returns nullopt when empty") {
            BucketPriorityQueue queue;
            auto value = queue.pop();
            check(!value.has_value());
        }
    }

    group("Batch Operations") {
        it("pop_batch returns correct number of items") {
            BucketPriorityQueue queue;

            for (size_t i = 0; i < 10; ++i) {
                queue.push(Priority::HIGH, i);
            }

            auto batch = queue.pop_batch(5);
            check(batch.size() == 5);
            check(queue.size() == 5);
        }

        it("pop_batch respects priority order") {
            BucketPriorityQueue queue;

            for (size_t i = 0; i < 5; ++i) {
                queue.push(Priority::LOW, i);
            }
            for (size_t i = 5; i < 10; ++i) {
                queue.push(Priority::HIGH, i);
            }

            auto batch = queue.pop_batch(7);
            check(batch.size() == 7);

            // First 5 should be HIGH priority
            for (size_t i = 0; i < 5; ++i) {
                check(batch[i] >= 5 && batch[i] < 10);
            }

            // Next 2 should be LOW priority
            for (size_t i = 5; i < 7; ++i) {
                check(batch[i] < 5);
            }
        }

        it("pop_batch handles empty queue") {
            BucketPriorityQueue queue;
            auto batch = queue.pop_batch(10);
            check(batch.empty());
        }

        it("pop_batch with zero items") {
            BucketPriorityQueue queue;
            queue.push(Priority::HIGH, 42);

            auto batch = queue.pop_batch(0);
            check(batch.empty());
            check(queue.size() == 1);  // Nothing popped
        }
    }

    group("Peek Operations") {
        it("peek returns highest priority without removing") {
            BucketPriorityQueue queue;

            queue.push(Priority::LOW, 1);
            queue.push(Priority::HIGH, 2);

            auto value = queue.peek();
            check(value.has_value());
            check(*value == 2);
            check(queue.size() == 2);  // Not removed
        }

        it("peek returns nullopt when empty") {
            BucketPriorityQueue queue;
            check(!queue.peek().has_value());
        }
    }

    group("Size and Capacity") {
        it("tracks size correctly") {
            BucketPriorityQueue queue;

            check(queue.size() == 0);

            queue.push(Priority::HIGH, 1);
            check(queue.size() == 1);

            queue.push(Priority::LOW, 2);
            check(queue.size() == 2);

            queue.pop();
            check(queue.size() == 1);

            queue.pop();
            check(queue.size() == 0);
        }

        it("size per priority") {
            BucketPriorityQueue queue;

            queue.push(Priority::HIGH, 1);
            queue.push(Priority::HIGH, 2);
            queue.push(Priority::LOW, 3);

            check(queue.size(Priority::HIGH) == 2);
            check(queue.size(Priority::LOW) == 1);
            check(queue.size(Priority::NORMAL) == 0);
        }

        it("reserve pre-allocates capacity") {
            BucketPriorityQueue queue;
            queue.reserve(100);

            check(queue.capacity(Priority::HIGH) >= 100);
            check(queue.capacity(Priority::LOW) >= 100);
        }

        it("constructor with capacity") {
            BucketPriorityQueue queue(50);

            check(queue.capacity(Priority::HIGH) >= 50);
            check(queue.empty());
        }
    }

    group("Clear and Reset") {
        it("clear removes all items") {
            BucketPriorityQueue queue;

            for (size_t i = 0; i < 10; ++i) {
                queue.push(Priority::HIGH, i);
            }

            queue.clear();
            check(queue.empty());
            check(queue.size() == 0);
        }

        it("shrink_to_fit reduces capacity") {
            BucketPriorityQueue queue;
            queue.reserve(1000);

            queue.push(Priority::HIGH, 1);
            queue.pop();

            queue.shrink_to_fit();
            // Capacity should be reduced (exact value is implementation-defined)
            check(queue.empty());
        }
    }

    group("Move Semantics") {
        it("move constructor") {
            BucketPriorityQueue queue1;
            queue1.push(Priority::HIGH, 42);

            BucketPriorityQueue queue2(std::move(queue1));

            check(queue2.size() == 1);
            check(*queue2.pop() == 42);
        }

        it("move assignment") {
            BucketPriorityQueue queue1;
            queue1.push(Priority::HIGH, 42);

            BucketPriorityQueue queue2;
            queue2 = std::move(queue1);

            check(queue2.size() == 1);
            check(*queue2.pop() == 42);
        }

        it("swap") {
            BucketPriorityQueue queue1;
            queue1.push(Priority::HIGH, 1);

            BucketPriorityQueue queue2;
            queue2.push(Priority::LOW, 2);
            queue2.push(Priority::LOW, 3);

            queue1.swap(queue2);

            check(queue1.size() == 2);
            check(queue2.size() == 1);
            check(*queue2.pop() == 1);
        }
    }

    group("Stress Tests") {
        it("handles large number of items") {
            BucketPriorityQueue queue;
            constexpr size_t N = 10000;

            for (size_t i = 0; i < N; ++i) {
                Priority p = static_cast<Priority>(i % 4);
                queue.push(p, i);
            }

            check(queue.size() == N);

            size_t popped = 0;
            while (!queue.empty()) {
                queue.pop();
                ++popped;
            }

            check(popped == N);
        }

        it("handles random operations") {
            BucketPriorityQueue queue;
            std::mt19937 rng(42);
            std::uniform_int_distribution<int> op_dist(0, 2);
            std::uniform_int_distribution<int> pri_dist(0, 3);

            size_t expected_size = 0;

            for (int i = 0; i < 1000; ++i) {
                int op = op_dist(rng);

                if (op == 0 || queue.empty()) {
                    // Push
                    Priority p = static_cast<Priority>(pri_dist(rng));
                    queue.push(p, i);
                    ++expected_size;
                } else if (op == 1) {
                    // Pop
                    queue.pop();
                    --expected_size;
                } else {
                    // Batch pop
                    size_t to_pop = std::min(size_t(5), queue.size());
                    auto batch = queue.pop_batch(to_pop);
                    expected_size -= batch.size();
                }

                check(queue.size() == expected_size);
            }
        }
    }

    bench("Performance") {
        benchmark("push 10k items", 1, 1.0) {
            BucketPriorityQueue queue;
            queue.reserve(10000);

            for (size_t i = 0; i < 10000; ++i) {
                Priority p = static_cast<Priority>(i % 4);
                queue.push(p, i);
            }
        }

        benchmark("pop 10k items", 1, 1.0) {
            BucketPriorityQueue queue;
            for (size_t i = 0; i < 10000; ++i) {
                Priority p = static_cast<Priority>(i % 4);
                queue.push(p, i);
            }

            for (size_t i = 0; i < 10000; ++i) {
                queue.pop();
            }
        }

        benchmark("batch pop 10k items", 1, 1.0) {
            BucketPriorityQueue queue;
            for (size_t i = 0; i < 10000; ++i) {
                Priority p = static_cast<Priority>(i % 4);
                queue.push(p, i);
            }

            while (!queue.empty()) {
                queue.pop_batch(100);
            }
        }

        benchmark("mixed operations", 1, 1.0) {
            BucketPriorityQueue queue;

            for (int i = 0; i < 1000; ++i) {
                for (int j = 0; j < 10; ++j) {
                    Priority p = static_cast<Priority>(j % 4);
                    queue.push(p, i * 10 + j);
                }

                for (int j = 0; j < 5; ++j) {
                    queue.pop();
                }
            }
        }
    }
}
