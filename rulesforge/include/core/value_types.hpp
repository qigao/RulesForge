#pragma once

#include "core/rfl_strings.hpp"
#include <chrono>
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

struct TypedList;
struct ValueSet;
struct ValueMap;

using ConstraintValue =
    std::variant<std::string, int64_t, double, FactList, NilValue, std::shared_ptr<TypedList>,
                 std::shared_ptr<ValueSet>, std::shared_ptr<ValueMap>>;

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
    std::is_same_v<T, std::shared_ptr<ValueMap>>;

/**
 * @brief 编译期检测 T 是否为 getFieldAs 支持的安全提取类型。
 *
 * 除 ConstraintValue 的直接成员外，还包含 bool（从 int64_t 0/1 转换）。
 */
template <typename T>
concept ConstraintValueExtractable =
    ConstraintValueMember<T> || std::is_same_v<T, bool>;

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
