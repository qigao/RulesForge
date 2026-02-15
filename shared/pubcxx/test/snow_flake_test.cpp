#include "tinytest.h"
#include "pubcxx/snow_flake.hpp"

#include <chrono>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

struct SnowflakeParts {
    int64_t timestamp;
    int64_t datacenter_id;
    int64_t worker_id;
    int64_t sequence;
};

static SnowflakeParts extract_parts(int64_t id) {
    SnowflakeParts parts;
    parts.sequence = id & ((1 << 12L) - 1);
    parts.worker_id = (id >> 12L) & ((1 << 5L) - 1);
    parts.datacenter_id = (id >> (12L + 5L)) & ((1 << 5L) - 1);
    parts.timestamp = (id >> (12L + 5L + 5L)) + 1735689600000LL;
    return parts;
}

suite("Snowflake ID Generation") {
    group("Initialization") {
        it("initializes with valid IDs") {
            snowflake generator;
            CHECK_NOTHROW(generator.init(0, 0));
            CHECK_NOTHROW(generator.init(31, 31));
        }

        it("throws on invalid worker ID") {
            snowflake generator;
            REQUIRE_THROWS_AS(generator.init(-1, 0), std::runtime_error);
            REQUIRE_THROWS_AS(generator.init(32, 0), std::runtime_error);
        }

        it("throws on invalid datacenter ID") {
            snowflake generator;
            REQUIRE_THROWS_AS(generator.init(0, -1), std::runtime_error);
            REQUIRE_THROWS_AS(generator.init(0, 32), std::runtime_error);
        }
    }

    group("ID Generation") {
        it("generates unique IDs") {
            snowflake generator;
            generator.init(1, 1);
            std::set<int64_t> ids;
            int const num_ids = 1000;
            for (int i = 0; i < num_ids; ++i) { ids.insert(generator.nextid()); }
            check_size_eq(ids.size(), num_ids);
        }

        it("generates increasing IDs") {
            snowflake generator;
            generator.init(2, 2);
            int64_t last_id = 0;
            for (int i = 0; i < 100; ++i) {
                int64_t current_id = generator.nextid();
                if (i > 0) {
                    check_int_gt(current_id, last_id);
                }
                last_id = current_id;
            }
        }

        it("encodes correct components") {
            snowflake generator;
            int64_t worker_id = 5;
            int64_t datacenter_id = 10;
            generator.init(worker_id, datacenter_id);

            int64_t id = generator.nextid();
            SnowflakeParts parts = extract_parts(id);

            check_long_eq(parts.worker_id, worker_id);
            check_long_eq(parts.datacenter_id, datacenter_id);

            int64_t current_time_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            check_int_ge(parts.timestamp, 1735689600000LL);
            check_int_le(parts.timestamp, current_time_ms + 1000);
        }
    }

    group("Thread Safety") {
        it("generates unique IDs across threads") {
            snowflake generator;
            generator.init(7, 7);
            std::vector<std::thread> threads;
            std::set<int64_t> all_ids;
            std::mutex set_mutex;
            int const ids_per_thread = 500;
            int const num_threads = 10;

            for (int i = 0; i < num_threads; ++i) {
                threads.emplace_back([&]() {
                    std::vector<int64_t> local_ids;
                    local_ids.reserve(ids_per_thread);
                    for (int j = 0; j < ids_per_thread; ++j) { local_ids.push_back(generator.nextid()); }
                    std::lock_guard<std::mutex> lock(set_mutex);
                    all_ids.insert(local_ids.begin(), local_ids.end());
                });
            }

            for (auto& t : threads) { t.join(); }
            check_size_eq(all_ids.size(), ids_per_thread * num_threads);
        }
    }
}

suite("timestampToIso8601") {
    group("Absolute timestamp") {
        it("converts known timestamps") {
            check_str_eq(timestampToIso8601(1735689600000ULL, false).c_str(), "2025-01-01T00:00:00.000Z");
            check_str_eq(timestampToIso8601(1735689601123ULL, false).c_str(), "2025-01-01T00:00:01.123Z");
            check_str_eq(timestampToIso8601(1698316245500ULL, false).c_str(), "2023-10-26T10:30:45.500Z");
        }
    }

    group("Snowflake ID") {
        it("converts snowflake IDs") {
            uint64_t id1 = (0ULL << 22) | (1ULL << 17) | (1ULL << 12) | 1ULL;
            check_str_eq(timestampToIso8601(id1, true).c_str(), "2025-01-01T00:00:00.000Z");

            uint64_t id2 = (1234ULL << 22) | (5ULL << 17) | (10ULL << 12) | 100ULL;
            check_str_eq(timestampToIso8601(id2, true).c_str(), "2025-01-01T00:00:01.234Z");

            uint64_t id3 = (3723456ULL << 22) | (15ULL << 17) | (31ULL << 12) | 1024ULL;
            check_str_eq(timestampToIso8601(id3, true).c_str(), "2025-01-01T01:02:03.456Z");
        }
    }

    group("Edge cases") {
        it("handles zero and boundary values") {
            check_str_eq(timestampToIso8601(0, false).c_str(), "1970-01-01T00:00:00.000Z");
            check_str_eq(timestampToIso8601(0, true).c_str(), "2025-01-01T00:00:00.000Z");
            check_str_eq(timestampToIso8601(1735689600000ULL, false).c_str(), "2025-01-01T00:00:00.000Z");
            check_str_eq(timestampToIso8601(1735689600999ULL, false).c_str(), "2025-01-01T00:00:00.999Z");
        }
    }
}
