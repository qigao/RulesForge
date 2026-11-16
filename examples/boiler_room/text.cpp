#include "text.hpp"
#include <array>
#include <cstring>
#include <simd.h>
#include <stdexcept>

namespace boiler_room {
namespace {
bool equal_block(void const *left, void const *right) {
  v128 a, b, mask;
  uint32_t bits = 0;
  simd_v128_load(&a, left);
  simd_v128_load(&b, right);
  if (!simd_compare(&simd_u8x16, SALTS_SIMD_COMPARE_EQ, &mask, &a, &b) ||
      !simd_reduce(&simd_u8x16, SALTS_SIMD_REDUCE_BITMASK, &mask, &bits))
    throw std::logic_error("Salts SIMD byte comparison failed");
  return bits == 0xffff;
}
} // namespace
bool payload_equal(vstr left, vstr right) {
  if (!vstr_is_valid(left) || !vstr_is_valid(right))
    throw std::invalid_argument("Invalid payload view");
  if (left.len != right.len)
    return false;
  size_t offset = 0;
  for (; left.len - offset >= 16; offset += 16)
    if (!equal_block(left.data + offset, right.data + offset))
      return false;
  if (offset != left.len) {
    // Zero padding handles the final partial vector without reading past input.
    std::array<char, 16> a{}, b{};
    std::memcpy(a.data(), left.data + offset, left.len - offset);
    std::memcpy(b.data(), right.data + offset, right.len - offset);
    return equal_block(a.data(), b.data());
  }
  return true;
}
} // namespace boiler_room
