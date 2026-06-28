#pragma once

#include "core/rfl_strings.hpp"
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <map>
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
