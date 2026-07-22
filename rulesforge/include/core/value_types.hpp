#pragma once

#include "core/rfl_strings.hpp"
#include <turbo_uuid.h>

#include <chrono>
#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

struct Fact;

struct FactList {
  std::vector<Fact *> facts;
};

struct NilValue {};

struct BytesValue {
  std::vector<uint8_t> bytes;
  auto operator<=>(BytesValue const &) const = default;
};

struct DateTimeValue {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int millisecond = 0;
  int tz_offset_minutes = 0;
  bool has_timezone = false;
  auto operator<=>(DateTimeValue const &) const = default;
};

struct DateValue {
  int year = 0;
  int month = 0;
  int day = 0;
  auto operator<=>(DateValue const &) const = default;
};

struct TimeValue {
  int hour = 0;
  int minute = 0;
  int second = 0;
  int millisecond = 0;
  auto operator<=>(TimeValue const &) const = default;
};

struct DurationValue {
  int64_t milliseconds = 0;
  auto operator<=>(DurationValue const &) const = default;
};

struct DecimalValue {
  int64_t mantissa = 0;
  int32_t scale = 0;
  auto operator<=>(DecimalValue const &) const = default;
};

struct BigIntValue {
  std::string digits;
  auto operator<=>(BigIntValue const &) const = default;
};

struct MoneyValue {
  DecimalValue amount;
  std::string currency;
  auto operator<=>(MoneyValue const &) const = default;
};

using EnumNumericValue = std::variant<int64_t, uint64_t>;

struct EnumValue {
  std::string type_name;
  EnumNumericValue value;
  std::string item_name;

  friend bool operator==(EnumValue const &a, EnumValue const &b) {
    return a.type_name == b.type_name && a.value == b.value;
  }
  friend bool operator<(EnumValue const &a, EnumValue const &b) {
    if (a.type_name != b.type_name) return a.type_name < b.type_name;
    return a.value < b.value;
  }
};

struct TypedList;
struct ValueSet;
struct ValueMap;

inline bool operator==(turbo_uuid_t const &a, turbo_uuid_t const &b) {
  return turbo_uuid_equal(&a, &b);
}

inline bool operator!=(turbo_uuid_t const &a, turbo_uuid_t const &b) {
  return !(a == b);
}

using ConstraintValue =
    std::variant<std::string, int64_t, double, FactList, NilValue, std::shared_ptr<TypedList>,
                 std::shared_ptr<ValueSet>, std::shared_ptr<ValueMap>, turbo_uuid_t, bool,
                 uint64_t, BytesValue, EnumValue, DateTimeValue, DateValue, TimeValue,
                 DurationValue, DecimalValue, BigIntValue, MoneyValue>;

struct ConstraintValueCompare {
  bool operator()(ConstraintValue const &a, ConstraintValue const &b) const;
};

struct ConstraintValueHasher {
  std::size_t operator()(ConstraintValue const &v) const;
};

struct ConstraintValueEquals {
  bool operator()(ConstraintValue const &a, ConstraintValue const &b) const;
};

struct TypedList {
  std::vector<ConstraintValue> values;
};

struct ValueSet {
  std::set<ConstraintValue, ConstraintValueCompare> values;
};

struct ValueMap {
  std::map<ConstraintValue, ConstraintValue, ConstraintValueCompare> entries;
};

inline ConstraintValue make_typed_list(std::vector<ConstraintValue> vals = {}) {
  return std::make_shared<TypedList>(TypedList{std::move(vals)});
}

inline ConstraintValue make_value_set() { return std::make_shared<ValueSet>(); }

inline ConstraintValue make_value_map() { return std::make_shared<ValueMap>(); }

std::string to_string(ConstraintValue const &val);
std::optional<std::string> scalar_text(ConstraintValue const &val);

bool operator==(FactList const &a, FactList const &b);
bool operator!=(FactList const &a, FactList const &b);
inline bool operator==(NilValue const &, NilValue const &) { return true; }
inline bool operator!=(NilValue const &, NilValue const &) { return false; }
inline bool operator==(TypedList const &a, TypedList const &b) { return a.values == b.values; }
inline bool operator!=(TypedList const &a, TypedList const &b) { return !(a == b); }
inline bool operator==(ValueSet const &a, ValueSet const &b) { return a.values == b.values; }
inline bool operator!=(ValueSet const &a, ValueSet const &b) { return !(a == b); }
inline bool operator==(ValueMap const &a, ValueMap const &b) { return a.entries == b.entries; }
inline bool operator!=(ValueMap const &a, ValueMap const &b) { return !(a == b); }

// ---------------------------------------------------------------------------
// TMP Utilities
// ---------------------------------------------------------------------------

/**
 * @brief 编译期检测 T 是否为 ConstraintValue 的合法成员类型。
 *
 * 单一事实源：修改 ConstraintValue 的 variant 定义时只需在此同步，
 * 所有使用此 concept 的接口约束自动更新。
 *
 * 使用示例（C++20 requires）：
 * @code
 *   template <ConstraintValueMember T>
 *   std::optional<T> getFieldAs(std::string_view field) const;
 * @endcode
 */
template <typename T>
concept ConstraintValueMember =
    std::is_same_v<T, std::string>                 ||
    std::is_same_v<T, int64_t>                     ||
    std::is_same_v<T, double>                      ||
    std::is_same_v<T, FactList>                    ||
    std::is_same_v<T, NilValue>                    ||
    std::is_same_v<T, std::shared_ptr<TypedList>>  ||
    std::is_same_v<T, std::shared_ptr<ValueSet>>   ||
    std::is_same_v<T, std::shared_ptr<ValueMap>>   ||
    std::is_same_v<T, turbo_uuid_t>                ||
    std::is_same_v<T, bool>                        ||
    std::is_same_v<T, uint64_t>                    ||
    std::is_same_v<T, BytesValue>                  ||
    std::is_same_v<T, EnumValue>                   ||
    std::is_same_v<T, DateTimeValue>               ||
    std::is_same_v<T, DateValue>                   ||
    std::is_same_v<T, TimeValue>                   ||
    std::is_same_v<T, DurationValue>               ||
    std::is_same_v<T, DecimalValue>                ||
    std::is_same_v<T, BigIntValue>                 ||
    std::is_same_v<T, MoneyValue>;

/**
 * @brief 编译期检测 T 是否为 getFieldAs 支持的安全提取类型。
 *
 * 当前支持 ConstraintValue 的直接成员类型。
 */
template <typename T>
concept ConstraintValueExtractable = ConstraintValueMember<T>;

/**
 * @brief 变参继承 Mixin（标准 overloaded 惯用法）。
 *
 * 用于以 lambda 列表直接访问 std::variant，消除手写 visitor 样板。
 *
 * 使用示例：
 * @code
 *   std::visit(overloaded{
 *       [](std::string const& s) { ... },
 *       [](int64_t i)            { ... },
 *       [](auto&&)               { ... },  // 兜底分支
 *   }, constraint_value);
 * @endcode
 */
template <typename... Fs>
struct overloaded : Fs... { using Fs::operator()...; };

// C++17 推导指引（CTAD），C++20 中可省略但保留以兼容混合编译单元
template <typename... Fs>
overloaded(Fs...) -> overloaded<Fs...>;
