#include "tinytest.h"
#include "pubcxx/simple_xxhash.hpp"

#include <string>
#include <vector>

suite("XXHasher") {
    it("computes xxh3_64_mem") {
        std::vector<char> data = {'a', 'b', 'c', 'd'};
        size_t len = data.size();
        uint64_t hash = XXHash::xxh3_64_sum_buf(data.data(), len);
        check(hash != 0);
    }

    it("computes xxh3_64_mem_str") {
        std::vector<char> data = {'a', 'b', 'c', 'd'};
        size_t len = data.size();
        std::string hash_str = XXHash::xxh3_64_sum_buf_str(data.data(), len);
        check_size_eq(hash_str.length(), 16);
    }

    it("computes xxh3_128_mem") {
        std::vector<char> data = {'a', 'b', 'c', 'd'};
        size_t len = data.size();
        XXH128_hash_t hash = XXHash::xxh3_128_sum_buf(data.data(), len);
        check(hash.high64 != 0);
        check(hash.low64 != 0);
    }

    it("computes xxh3_128_mem_str") {
        std::vector<char> data = {'a', 'b', 'c', 'd'};
        size_t const len = data.size();
        auto const hash_str = XXHash::xxh3_128_sum_str(data.data(), len);
        check_size_eq(hash_str.length(), 32);
    }
}
