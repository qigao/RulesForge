#include "core/rfl_strings.hpp"
#include "core/value_types.hpp"

#include "tinytest.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

using namespace rulesforge;

suite("String Map Benchmarks") {
    bench("Four-field fact maps") {
        constexpr size_t kMapCount = 10000;
        constexpr size_t kFieldCount = 4;
        constexpr double kOperations = static_cast<double>(kMapCount * kFieldCount);

        std::array<std::string, kFieldCount> owned_keys = {"id", "value", "category", "status"};
        std::array<InternedString, kFieldCount> interned_keys = {
            InternedString(StringInterner::instance().intern(owned_keys[0])),
            InternedString(StringInterner::instance().intern(owned_keys[1])),
            InternedString(StringInterner::instance().intern(owned_keys[2])),
            InternedString(StringInterner::instance().intern(owned_keys[3]))};

        benchmark("InternedKeyMap build 10K x 4", 3, kOperations) {
            std::vector<InternedKeyMap<ConstraintValue>> maps;
            maps.reserve(kMapCount);
            for (size_t map_index = 0; map_index < kMapCount; ++map_index) {
                auto& map = maps.emplace_back();
                map.reserve(kFieldCount);
                for (size_t field_index = 0; field_index < kFieldCount; ++field_index) {
                    map[interned_keys[field_index]] = static_cast<int64_t>(field_index);
                }
            }
        }

        benchmark("unordered_map build 10K x 4", 3, kOperations) {
            std::vector<std::unordered_map<std::string, ConstraintValue>> maps;
            maps.reserve(kMapCount);
            for (size_t map_index = 0; map_index < kMapCount; ++map_index) {
                auto& map = maps.emplace_back();
                map.reserve(kFieldCount);
                for (size_t field_index = 0; field_index < kFieldCount; ++field_index) {
                    map[owned_keys[field_index]] = static_cast<int64_t>(field_index);
                }
            }
        }

        std::vector<InternedKeyMap<ConstraintValue>> interned_maps(kMapCount);
        std::vector<std::unordered_map<std::string, ConstraintValue>> owned_maps(kMapCount);
        for (size_t map_index = 0; map_index < kMapCount; ++map_index) {
            interned_maps[map_index].reserve(kFieldCount);
            owned_maps[map_index].reserve(kFieldCount);
            for (size_t field_index = 0; field_index < kFieldCount; ++field_index) {
                interned_maps[map_index][interned_keys[field_index]] =
                    static_cast<int64_t>(field_index);
                owned_maps[map_index][owned_keys[field_index]] = static_cast<int64_t>(field_index);
            }
        }

        benchmark("InternedKeyMap lookup 10K x 4", 5, kOperations) {
            size_t found = 0;
            for (auto const& map : interned_maps) {
                for (auto const& key : owned_keys) {
                    found += map.find(key) != map.end();
                }
            }
            check_equal(found, kMapCount * kFieldCount);
        }

        benchmark("unordered_map lookup 10K x 4", 5, kOperations) {
            size_t found = 0;
            for (auto const& map : owned_maps) {
                for (auto const& key : owned_keys) {
                    found += map.find(key) != map.end();
                }
            }
            check_equal(found, kMapCount * kFieldCount);
        }
    }
}
