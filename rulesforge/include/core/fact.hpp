#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <variant>
#include <chrono>

#include "core/rfl_strings.hpp"


#include "core/value_types.hpp"

struct IndexAccess {
    bool is_integer;
    int64_t int_index;
    std::string str_index;
};

struct PathSegment {
    std::string name;
    bool null_safe;
    std::vector<IndexAccess> indices;
};

struct Fact {
    int64_t id = 0;
    std::string type;
    rulesforge::InternedKeyMap<ConstraintValue> fields;
    std::optional<ConstraintValue> get_field(std::string const& name) const;
    std::optional<ConstraintValue> get_field(std::vector<PathSegment> const& path) const;
};

std::vector<PathSegment> parse_field_path(std::string const& path);
std::optional<ConstraintValue> apply_index(ConstraintValue const& val, IndexAccess const& idx, bool null_safe);
