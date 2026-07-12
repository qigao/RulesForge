#include "core/rfl_strings.hpp"

#include "tinytest.h"

#include <array>
#include <string_view>
#include <thread>
#include <vector>

using namespace rulesforge;

suite("RFL Strings") {
    group("StringInterner") {
        it("returns one stable view for equal strings") {
            auto first = StringInterner::instance().intern("shared-field");
            auto second = StringInterner::instance().intern(std::string_view("shared-field"));

            check_str_eq(first.data(), second.data());
            check_ptr_eq(first.data(), second.data());
        }

        it("supports concurrent interning") {
            constexpr size_t kThreadCount = 4;
            constexpr size_t kIterations = 1000;
            std::array<std::string_view, kThreadCount> results{};
            std::vector<std::thread> threads;
            threads.reserve(kThreadCount);

            for (size_t thread_index = 0; thread_index < kThreadCount; ++thread_index) {
                threads.emplace_back([thread_index, &results] {
                    for (size_t iteration = 0; iteration < kIterations; ++iteration) {
                        results[thread_index] =
                            StringInterner::instance().intern("concurrent-field");
                    }
                });
            }
            for (auto& thread : threads) {
                thread.join();
            }

            for (size_t index = 1; index < results.size(); ++index) {
                check_ptr_eq(results[index].data(), results[0].data());
            }
        }
    }

    group("InternedKeyMap") {
        it("keeps fields sorted and updates existing values") {
            InternedKeyMap<int> fields;
            fields.reserve(3);
            fields["zeta"] = 1;
            fields["alpha"] = 2;
            fields["middle"] = 3;
            fields["alpha"] = 4;

            check_size_eq(fields.size(), 3);
            check_int_eq(fields.find("alpha")->second, 4);

            auto it = fields.begin();
            check_str_eq(it++->first.data(), "alpha");
            check_str_eq(it++->first.data(), "middle");
            check_str_eq(it->first.data(), "zeta");
        }
    }
}
