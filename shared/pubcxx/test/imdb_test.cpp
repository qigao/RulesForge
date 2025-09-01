#include "pubcxx/imdb.hpp"

#include <algorithm>   // For std::find (used in iteration tests)
#include <catch2/catch_all.hpp>
#include <future>   // For thread safety tests
#include <iostream>
#include <numeric>   // For std::iota, std::accumulate etc. (optional)
#include <string>
#include <thread>    // For thread safety tests
#include <utility>   // For std::pair, std::make_pair
#include <vector>
using namespace simple_db;

template <typename K, typename V>
using TestDb = InMemoryDb<K, V>;

TEST_CASE("InMemoryDb Construction and Basic Operations (Shared Mutex)", "[keyvalue][basic][shared_mutex]") {
    TestDb<int, std::string> db;

    SECTION("Database is initially empty") {
        REQUIRE(db.Size() == 0);
        REQUIRE(!db.Exists(1));
        REQUIRE(!db.Get(1).has_value());
    }

    SECTION("Put and Get operations") {
        db.Put(1, "one");
        REQUIRE(db.Size() == 1);
        auto val = db.Get(1);
        REQUIRE(val.has_value());
        REQUIRE(*val == "one");   // Use *val or val.value()

        db.Put(2, "two");
        REQUIRE(db.Size() == 2);
        auto val2 = db.Get(2);
        REQUIRE(val2.has_value());
        REQUIRE(*val2 == "two");

        // Test update
        db.Put(1, "updated");
        auto updatedVal = db.Get(1);
        REQUIRE(updatedVal.has_value());
        REQUIRE(*updatedVal == "updated");
        REQUIRE(db.Size() == 2);   // Size should not change on update
    }

    SECTION("Get returns std::nullopt for non-existent key") { REQUIRE(!db.Get(99).has_value()); }

    SECTION("Exists checks for key existence") {
        db.Put(42, "The Answer");
        REQUIRE(db.Exists(42));
        REQUIRE(!db.Exists(99));
    }

    SECTION("Delete removes a key-value pair") {
        db.Put(123, "to be deleted");
        REQUIRE(db.Size() == 1);
        REQUIRE(db.Exists(123));
        REQUIRE(db.Delete(123));
        REQUIRE(!db.Exists(123));
        REQUIRE(db.Size() == 0);

        // Try deleting again
        REQUIRE(!db.Delete(123));   // Should return false now
    }

    SECTION("Delete returns false if key doesn't exist") { REQUIRE(!db.Delete(999)); }

    SECTION("Clear removes all key-value pairs") {
        db.Put(1, "one");
        db.Put(2, "two");
        REQUIRE(db.Size() == 2);
        db.Clear();
        REQUIRE(db.Size() == 0);
        REQUIRE(!db.Exists(1));
        REQUIRE(!db.Exists(2));
    }

    SECTION("Handling empty values") {
        db.Put(1, "");
        REQUIRE(db.Size() == 1);
        REQUIRE(db.Get(1).has_value());
        REQUIRE(*db.Get(1) == "");

        db.Put(2, std::string{});   // Test empty string explicitly
        REQUIRE(db.Size() == 2);
        REQUIRE(db.Get(2).has_value());
        REQUIRE(*db.Get(2) == "");

        db.Delete(1);
        REQUIRE(!db.Exists(1));
    }
}

TEST_CASE("InMemoryDb ModifyValue (Shared Mutex)", "[keyvalue][modify][shared_mutex]") {
    TestDb<int, int> int_db;
    int_db.Put(1, 10);

    SECTION("ModifyValue modifies existing value (int)") {
        bool modified = int_db.ModifyValue(1, [](int& val) { val *= 2; });
        REQUIRE(modified);
        REQUIRE(int_db.Get(1).value() == 20);
    }

    SECTION("ModifyValue returns false if key doesn't exist (int)") {
        bool modified = int_db.ModifyValue(99, [](int& val) { val *= 2; });
        REQUIRE(!modified);            // Should return false
        REQUIRE(!int_db.Exists(99));   // Ensure no new key was created
    }

    TestDb<std::string, std::vector<int>> vec_int_db;
    vec_int_db.Put("list", std::vector<int>{1, 2, 3});
    TestDb<std::string, std::vector<int>> vec_int_db_local;
    SECTION("ModifyValue modifies existing vector") {
        bool modified = vec_int_db.ModifyValue("list", [](std::vector<int>& vec) {
            vec.push_back(4);
            vec[0] = 100;
        });
        REQUIRE(modified);
        auto val_opt = vec_int_db.Get("list");
        REQUIRE(val_opt.has_value());
        REQUIRE(val_opt.value() == std::vector<int>{100, 2, 3, 4});
    }

    SECTION("ModifyValue returns false if key doesn't exist (vector)") {
        bool modified = vec_int_db.ModifyValue("non_existent", [](std::vector<int>& vec) { vec.push_back(5); });
        REQUIRE(!modified);
        REQUIRE(!vec_int_db.Exists("non_existent"));   // Ensure no new key was created
    }

    SECTION("ModifyValue on existing empty container - Isolated Instance") {
        // Use this local variable exclusively within the section
        TestDb<std::string, std::vector<int>> vec_int_db_local;

        vec_int_db_local.Put("empty_list", std::vector<int>{});
        REQUIRE(vec_int_db_local.Size() == 1);   // Checks vec_int_db_local - Should pass

        bool modified = vec_int_db_local.ModifyValue("empty_list", [](std::vector<int>& vec) { vec.push_back(99); });
        REQUIRE(modified);

        // Check using the local variable
        auto val_opt = vec_int_db_local.Get("empty_list");   // <-- Getting from vec_int_db_local
        REQUIRE(val_opt.has_value());
        REQUIRE(val_opt.value() == std::vector<int>{99});

        // Check size using the local variable
        REQUIRE(vec_int_db_local.Size() == 1);   // <-- Checking size of vec_int_db_local - Should pass (still size 1)
    }
}

TEST_CASE("InMemoryDb Append (Shared Mutex)", "[keyvalue][append][shared_mutex]") {
    // Testing append for std::vector<std::string>
    TestDb<int, std::vector<std::string>> vec_string_db;

    SECTION("Append adds element to existing vector") {
        vec_string_db.Put(1, std::vector<std::string>{"a", "b"});
        bool appended = vec_string_db.Append(1, std::string("c"));
        REQUIRE(appended);
        auto val = vec_string_db.Get(1);
        REQUIRE(val.has_value());
        REQUIRE(val.value().size() == 3);
        REQUIRE(val.value()[2] == "c");
        REQUIRE(val.value() == std::vector<std::string>{"a", "b", "c"});   // Check full content
        REQUIRE(vec_string_db.Size() == 1);
    }

    SECTION("Append adds element to existing empty vector") {
        vec_string_db.Put(2, std::vector<std::string>{});   // Start with empty vector
        REQUIRE(vec_string_db.Size() == 1);
        bool appended = vec_string_db.Append(2, std::string("first"));
        REQUIRE(appended);
        auto val = vec_string_db.Get(2);
        REQUIRE(val.has_value());
        REQUIRE(val.value().size() == 1);
        REQUIRE(val.value()[0] == "first");
        REQUIRE(val.value() == std::vector<std::string>{"first"});
        REQUIRE(vec_string_db.Size() == 1);
    }

    SECTION("Append returns false if key doesn't exist") {
        bool appended = vec_string_db.Append(99, "z");
        REQUIRE(!appended);
        REQUIRE(!vec_string_db.Exists(99));   // Ensure no new key was created
    }

    // Testing append for std::vector<uint8_t>
    TestDb<int, std::vector<uint8_t>> vec_byte_db;
    vec_byte_db.Put(1, std::vector<uint8_t>{10, 20});
    REQUIRE(vec_byte_db.Size() == 1);

    SECTION("Append single byte to existing vector<uint8_t>") {
        bool appended = vec_byte_db.Append(1, (uint8_t)30);
        REQUIRE(appended);
        auto val = vec_byte_db.Get(1);
        REQUIRE(val.has_value());
        REQUIRE(val.value() == std::vector<uint8_t>{10, 20, 30});
        REQUIRE(vec_byte_db.Size() == 1);
    }
    SECTION("Append single byte to existing empty vector<uint8_t>") {
        vec_byte_db.Put(2, std::vector<uint8_t>{});
        REQUIRE(vec_byte_db.Size() == 2);
        bool appended = vec_byte_db.Append(2, (uint8_t)50);
        REQUIRE(appended);
        auto val = vec_byte_db.Get(2);
        REQUIRE(val.has_value());
        REQUIRE(val.value() == std::vector<uint8_t>{50});
        REQUIRE(vec_byte_db.Size() == 2);
    }

    SECTION("Append byte returns false if key doesn't exist") {
        bool appended = vec_byte_db.Append(99, (uint8_t)100);
        REQUIRE(!appended);
        REQUIRE(!vec_byte_db.Exists(99));   // Ensure no new key was created
    }
}

// --- Thread Safety Tests (Shared Mutex Specific) ---
// These tests are crucial for verifying the shared_mutex implementation.
// Running with thread sanitizer (-fsanitize=thread) is highly recommended.

TEST_CASE("InMemoryDb Thread Safety (Shared Mutex)", "[keyvalue][threadsafe][shared_mutex]") {
    // Use string keys and int values for simplicity
    TestDb<std::string, int> db;
    int const num_threads = 16;               // Use more threads to increase concurrency chance
    int const operations_per_thread = 5000;   // More operations

    SECTION("Concurrent Put operations (Exclusive Write Lock)") {
        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&db, i, operations_per_thread]() {
                for (int j = 0; j < operations_per_thread; ++j) {
                    std::string key = "put_thread_" + std::to_string(i) + "_key_" + std::to_string(j);
                    db.Put(key, i * 100000 + j);   // Larger value range
                }
            });
        }

        for (auto& t : threads) { t.join(); }

        REQUIRE(db.Size() == num_threads * operations_per_thread);

        // Verify some values
        for (int i = 0; i < num_threads; ++i) {
            for (int j = 0; j < operations_per_thread; ++j) {
                std::string key = "put_thread_" + std::to_string(i) + "_key_" + std::to_string(j);
                auto val = db.Get(key);
                REQUIRE(val.has_value());
                REQUIRE(*val == i * 100000 + j);
            }
        }
    }

    SECTION("Concurrent Get and Exists operations (Shared Read Lock)") {
        // Pre-populate the database
        constexpr int num_initial_keys = 10000;
        for (int i = 0; i < num_initial_keys; ++i) { db.Put("read_key_" + std::to_string(i), i); }
        REQUIRE(db.Size() == num_initial_keys);

        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&db, i, num_initial_keys, operations_per_thread]() {
                for (int j = 0; j < operations_per_thread; ++j) {
                    int key_idx = (i * operations_per_thread + j) % num_initial_keys;
                    std::string key = "read_key_" + std::to_string(key_idx);

                    // Perform mostly reads
                    if (j % 10 != 0) {   // 90% Get, 10% Exists
                        db.Get(key);     // No crash expected, value might be predictable if no writes
                    } else {
                        db.Exists(key);   // Should always be true for these keys
                    }

                    // Also try getting a key that doesn't exist
                    db.Get("non_existent_key");
                    db.Exists("non_existent_key");
                }
            });
        }

        for (auto& t : threads) { t.join(); }

        // Size should be unchanged as only reads were attempted
        REQUIRE(db.Size() == num_initial_keys);

        SUCCEED("Concurrent Get and Exists operations completed without crashing.");
    }

    SECTION("Mixed Concurrent Operations (Put, Get, Delete, Exists)") {
        // Start with some initial data
        constexpr int num_initial_keys = 100;
        for (int i = 0; i < num_initial_keys; ++i) { db.Put("initial_mixed_" + std::to_string(i), i); }
        REQUIRE(db.Size() == num_initial_keys);

        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&db, i, num_initial_keys, operations_per_thread]() {
                for (int j = 0; j < operations_per_thread; ++j) {
                    // Mix operations: 40% Get/Exists (Read), 40% Put (Write/Update), 20% Delete (Write)
                    int op_type = (i * operations_per_thread + j) % 10;   // More fine-grained mix

                    std::string key;
                    if (op_type < 8) {   // Target a mix of initial and new keys
                        key =
                            ((i + j) % 2 == 0)
                                ? ("initial_mixed_" +
                                   std::to_string((i * j) % num_initial_keys))   // Target initial keys
                                : ("mixed_thread_" + std::to_string(i) + "_" + std::to_string(j));   // Target new keys
                    } else {   // Target only new keys for deletes
                        key = "mixed_thread_" + std::to_string(i) + "_" + std::to_string(j);
                    }

                    if (op_type < 4) {   // Get/Exists (Shared Lock)
                        if (op_type % 2 == 0)
                            db.Get(key);
                        else
                            db.Exists(key);
                    } else if (op_type < 8) {   // Put (Exclusive Lock)
                        db.Put(key, i + j + 1000000);
                    } else {   // Delete (Exclusive Lock)
                        db.Delete(key);
                    }
                }
            });
        }

        for (auto& t : threads) { t.join(); }

        // Final size and content are hard to predict exactly due to concurrent deletes and puts.
        // The main goal here is to verify it runs without crashes or obvious race conditions.
        std::cout << "\nMixed concurrent ops finished. Final DB size: " << db.Size() << std::endl;
        SUCCEED("Mixed concurrent operations completed without crashing.");
    }

    SECTION("Concurrent ModifyValue and Append (Exclusive Write Lock)") {
        TestDb<std::string, std::vector<int>> list_db;
        list_db.Put("shared_list", std::vector<int>{1, 2, 3});
        list_db.Put("other_list", std::vector<int>{10, 20});   // Another list not targeted by threads

        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&list_db, i, operations_per_thread]() {
                for (int j = 0; j < operations_per_thread; ++j) {
                    // Mix ModifyValue and Append operations on the same key
                    if ((i + j) % 2 == 0) {   // ModifyValue (Exclusive Lock)
                        list_db.ModifyValue("shared_list", [&](std::vector<int>& vec) {
                            // Perform a simple modification that doesn't rely on previous state
                            // to avoid complex state verification afterwards, focus on thread-safety
                            // of the ModifyValue call itself.
                            if (!vec.empty())
                                vec[0] = (vec[0] + 1) % 100;   // Example safe modification
                            else
                                vec.push_back(1);   // Add if empty
                        });
                    } else {                                             // Append (Calls ModifyValue, Exclusive Lock)
                        list_db.Append("shared_list", i * 100000 + j);   // Append a unique value
                    }
                }
            });
        }

        for (auto& t : threads) { t.join(); }

        auto final_list_opt = list_db.Get("shared_list");
        REQUIRE(final_list_opt.has_value());

        // The exact contents are hard to predict due to concurrent writes.
        // We can verify the size and that the operations didn't crash.
        // The size check below is a loose bound.
        // A more precise test would count the exact number of appends and factor in initial size.
        // Initial size: 3
        // Number of Appends is roughly operations_per_thread * num_threads / 2
        // Final size is roughly 3 + operations_per_thread * num_threads / 2
        // Using a lower bound check:
        size_t min_expected_appends =
            (size_t)(operations_per_thread * num_threads * 0.4);   // ~40% of total ops are appends
        REQUIRE(final_list_opt->size() >= 3 + min_expected_appends);

        // Verify the 'other_list' is unchanged
        auto other_list_opt = list_db.Get("other_list");
        REQUIRE(other_list_opt.has_value());
        REQUIRE(other_list_opt.value() == std::vector<int>{10, 20});

        std::cout << "\nConcurrent modify/append finished. Final shared_list size: " << final_list_opt->size()
                  << std::endl;
        SUCCEED("Concurrent modify/append operations completed without crashing.");
    }
}

// Test for Iterators (Single-threaded only, with warning)
TEST_CASE("InMemoryDb Iteration (Single Thread)", "[keyvalue][iterator][shared_mutex]") {
    TestDb<int, std::string> db;
    db.Put(1, "one");
    db.Put(2, "two");
    db.Put(3, "three");

    SECTION("Basic single-threaded iteration using range-based for") {
        std::vector<std::pair<int, std::string>> elements;
        for (auto const& pair : db) {   // Uses cbegin/cend
            elements.push_back(pair);
        }
        REQUIRE(elements.size() == 3);
        // Check contents (order isn't guaranteed in unordered_map)
        REQUIRE(std::find(elements.begin(), elements.end(), std::make_pair(1, std::string("one"))) != elements.end());
        REQUIRE(std::find(elements.begin(), elements.end(), std::make_pair(2, std::string("two"))) != elements.end());
        REQUIRE(std::find(elements.begin(), elements.end(), std::make_pair(3, std::string("three"))) != elements.end());
    }

    SECTION("Basic single-threaded iteration using explicit iterators") {
        std::vector<std::pair<int, std::string>> elements;
        for (auto it = db.begin(); it != db.end(); ++it) {   // Uses begin/end
            elements.push_back(*it);
        }
        REQUIRE(elements.size() == 3);
        // Check contents (order isn't guaranteed in unordered_map)
        REQUIRE(std::find(elements.begin(), elements.end(), std::make_pair(1, std::string("one"))) != elements.end());
        REQUIRE(std::find(elements.begin(), elements.end(), std::make_pair(2, std::string("two"))) != elements.end());
        REQUIRE(std::find(elements.begin(), elements.end(), std::make_pair(3, std::string("three"))) != elements.end());
    }

    // No concurrent iteration tests here because the current iterator design
    // is explicitly marked as unsafe for concurrent modification.
}
