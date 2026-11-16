#include <tinytest.hpp>
#include <simd.h>
#include <core/vstr.h>
#include <array>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace {
bool sdk_simd_equal(vstr a, vstr b) {
    if (a.len != b.len) return false;
    auto block = [](void const* left, void const* right) {
        v128 x, y, mask;
        uint32_t bits = 0;
        simd_v128_load(&x, left);
        simd_v128_load(&y, right);
        if (!simd_compare(&simd_u8x16, SALTS_SIMD_COMPARE_EQ, &mask, &x, &y) ||
            !simd_reduce(&simd_u8x16, SALTS_SIMD_REDUCE_BITMASK, &mask, &bits))
            throw std::logic_error("SDK SIMD byte comparison failed");
        return bits == 0xffff;
    };
    size_t offset = 0;
    for (; a.len - offset >= 16; offset += 16)
        if (!block(a.data + offset, b.data + offset)) return false;
    if (offset != a.len) {
        std::array<char, 16> x{}, y{};
        std::memcpy(x.data(), a.data + offset, a.len - offset);
        std::memcpy(y.data(), b.data + offset, b.len - offset);
        return block(x.data(), y.data());
    }
    return true;
}
} // namespace

suite("Released SDK text comparison") {
    bench("Equal or last-byte-mismatch text at representative lengths") {
        constexpr size_t pairs = 10000;
        std::array<char, 4096> left{}, right{};
        left.fill('x');
        right.fill('x');
        for (size_t length : {size_t{4}, size_t{16}, size_t{64}, size_t{512}, size_t{4096}}) {
            right = left;
            auto a = vstr_from_buf(left.data(), length), b = vstr_from_buf(right.data(), length);
            // Flip the final byte for half the probes. A compiler fence keeps
            // the input observable for each comparison in all three variants.
            auto run = [&](auto compare) {
                size_t equal = 0;
                for (size_t i = 0; i < pairs; ++i) {
                    right[length - 1] = static_cast<char>('x' + (i & 1));
                    std::atomic_signal_fence(std::memory_order_seq_cst);
                    equal += compare();
                }
                check_equal(equal, pairs / 2);
            };
            std::string label = "string_view length=" + std::to_string(length);
            benchmark_ops(label.c_str(), 5, pairs) {
                run([&] { return std::string_view(a.data, a.len) == std::string_view(b.data, b.len); });
            }
            label = "Base vstr length=" + std::to_string(length);
            benchmark_ops(label.c_str(), 5, pairs) { run([&] { return vstr_eq(a, b); }); }
            label = "Salts SIMD length=" + std::to_string(length);
            benchmark_ops(label.c_str(), 5, pairs) { run([&] { return sdk_simd_equal(a, b); }); }
        }
    }
}
