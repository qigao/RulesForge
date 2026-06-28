/**
 * @file rfl_value_bridge.cpp
 * @brief C ABI value bridge implementation
 */

#include "rfl_value_bridge.h"
#include "core/fact.hpp"
#include "core/value_types.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <map>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

enum class ValueKind {
    Object,
    List,
    Set,
    Map,
};

} // namespace

/**
 * @brief Value implementation (C++ wrapper)
 */
struct Value {
    explicit Value(ValueKind kind_in = ValueKind::Object) : kind(kind_in) {}

    ValueKind kind = ValueKind::Object;
    std::map<std::string, ConstraintValue> fields;
    std::vector<ConstraintValue> items;
    std::set<ConstraintValue, ConstraintValueCompare> set_items;
    std::map<ConstraintValue, ConstraintValue, ConstraintValueCompare> map_entries;
};

namespace {

std::shared_ptr<ValueMap> object_to_map(Value const& value) {
    auto map = std::make_shared<ValueMap>();
    for (auto const& [key, field_value] : value.fields) {
        map->entries.emplace(std::string(key), field_value);
    }
    return map;
}

ConstraintValue to_constraint_value(Value const& value) {
    switch (value.kind) {
    case ValueKind::Object:
        return object_to_map(value);
    case ValueKind::List:
        return make_typed_list(value.items);
    case ValueKind::Set: {
        auto set = std::make_shared<ValueSet>();
        set->values = value.set_items;
        return set;
    }
    case ValueKind::Map: {
        auto map = std::make_shared<ValueMap>();
        map->entries = value.map_entries;
        return map;
    }
    }
    return NilValue{};
}

Value* create_value(ValueKind kind) {
    try {
        return new Value(kind);
    } catch (...) {
        return nullptr;
    }
}

void set_object_field(Value* obj, const char* name, ConstraintValue value) {
    if (!obj || !name || obj->kind != ValueKind::Object) return;
    obj->fields[name] = std::move(value);
}

void add_list_item(Value* list, ConstraintValue value) {
    if (!list || list->kind != ValueKind::List) return;
    list->items.push_back(std::move(value));
}

void add_set_item(Value* set, ConstraintValue value) {
    if (!set || set->kind != ValueKind::Set) return;
    set->set_items.insert(std::move(value));
}

void add_map_entry(Value* map, const char* key, ConstraintValue value) {
    if (!map || !key || map->kind != ValueKind::Map) return;
    map->map_entries[std::string(key)] = std::move(value);
}

Value* value_from_map(std::shared_ptr<ValueMap> const& map) {
    if (!map) return nullptr;
    auto* result = create_value(ValueKind::Object);
    if (!result) return nullptr;
    for (auto const& [key, value] : map->entries) {
        if (auto const* str_key = std::get_if<std::string>(&key)) {
            result->fields[*str_key] = value;
        }
    }
    if (result->fields.empty()) {
        delete result;
        return nullptr;
    }
    return result;
}

} // namespace

extern "C" {

Value* value_create_object(void) {
    return create_value(ValueKind::Object);
}

void value_free(Value* obj) {
    delete obj;
}

void value_set_field_int(Value* obj, const char* name, int64_t val) {
    value_set_field_int64(obj, name, val);
}

void value_set_field_int32(Value* obj, const char* name, int32_t val) {
    set_object_field(obj, name, static_cast<int64_t>(val));
}

void value_set_field_int64(Value* obj, const char* name, int64_t val) {
    set_object_field(obj, name, val);
}

void value_set_field_double(Value* obj, const char* name, double val) {
    set_object_field(obj, name, val);
}

void value_set_field_string(Value* obj, const char* name, const char* val) {
    if (!obj || !name || !val) return;
    set_object_field(obj, name, std::string(val));
}

void value_set_field_bytes(Value* obj, const char* name, const uint8_t* data, size_t len) {
    if (!obj || !name || !data) return;
    auto bytes = std::make_shared<TypedList>();
    bytes->values.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        bytes->values.emplace_back(static_cast<int64_t>(data[i]));
    }
    set_object_field(obj, name, bytes);
}

void value_set_field_object(Value* obj, const char* name, Value* nested) {
    if (!obj || !name || !nested) return;
    set_object_field(obj, name, to_constraint_value(*nested));
    delete nested;
}

Value* value_create_list(void) {
    return create_value(ValueKind::List);
}

void value_add_list_item_int(Value* list, int32_t val) {
    add_list_item(list, static_cast<int64_t>(val));
}

void value_add_list_item_int64(Value* list, int64_t val) {
    add_list_item(list, val);
}

void value_add_list_item_double(Value* list, double val) {
    add_list_item(list, val);
}

void value_add_list_item_string(Value* list, const char* val) {
    if (!val) return;
    add_list_item(list, std::string(val));
}

void value_add_list_item_object(Value* list, Value* obj) {
    if (!list || !obj) return;
    add_list_item(list, to_constraint_value(*obj));
    delete obj;
}

void value_set_field_list(Value* obj, const char* name, Value* list) {
    if (!obj || !name || !list) return;
    set_object_field(obj, name, to_constraint_value(*list));
    delete list;
}

Value* value_create_set(void) {
    return create_value(ValueKind::Set);
}

void value_add_set_item_int(Value* set, int32_t val) {
    add_set_item(set, static_cast<int64_t>(val));
}

void value_add_set_item_double(Value* set, double val) {
    add_set_item(set, val);
}

void value_add_set_item_string(Value* set, const char* val) {
    if (!val) return;
    add_set_item(set, std::string(val));
}

void value_set_field_set(Value* obj, const char* name, Value* set) {
    if (!obj || !name || !set) return;
    set_object_field(obj, name, to_constraint_value(*set));
    delete set;
}

Value* value_create_map(void) {
    return create_value(ValueKind::Map);
}

void value_add_map_entry_string_string(Value* map, const char* key, const char* val) {
    if (!val) return;
    add_map_entry(map, key, std::string(val));
}

void value_add_map_entry_string_int(Value* map, const char* key, int32_t val) {
    add_map_entry(map, key, static_cast<int64_t>(val));
}

void value_add_map_entry_string_double(Value* map, const char* key, double val) {
    add_map_entry(map, key, val);
}

void value_set_field_map(Value* obj, const char* name, Value* map) {
    if (!obj || !name || !map) return;
    set_object_field(obj, name, to_constraint_value(*map));
    delete map;
}

int64_t value_get_field_int(const Value* obj, const char* name) {
    if (!obj || !name) return 0;

    auto it = obj->fields.find(name);
    if (it == obj->fields.end()) return 0;

    if (auto* val = std::get_if<int64_t>(&it->second)) {
        return *val;
    }
    return 0;
}

double value_get_field_double(const Value* obj, const char* name) {
    if (!obj || !name) return 0.0;

    auto it = obj->fields.find(name);
    if (it == obj->fields.end()) return 0.0;

    if (auto* val = std::get_if<double>(&it->second)) {
        return *val;
    }
    return 0.0;
}

const char* value_get_field_string(const Value* obj, const char* name) {
    if (!obj || !name) return "";

    auto it = obj->fields.find(name);
    if (it == obj->fields.end()) return "";

    if (auto* val = std::get_if<std::string>(&it->second)) {
        return val->c_str();
    }
    return "";
}

Value* value_get_field_object(const Value* obj, const char* name) {
    if (!obj || !name) return nullptr;

    auto it = obj->fields.find(name);
    if (it == obj->fields.end()) return nullptr;
    if (auto const* map = std::get_if<std::shared_ptr<ValueMap>>(&it->second)) {
        return value_from_map(*map);
    }
    return nullptr;
}

Fact* value_to_fact(Value* obj, const char* type_name) {
    if (!obj || !type_name || obj->kind != ValueKind::Object) return nullptr;
    try {
        auto* fact = new Fact();
        fact->type = type_name;
        for (auto const& [key, value] : obj->fields) {
            fact->fields[key] = value;
        }
        return fact;
    } catch (...) {
        return nullptr;
    }
}

void value_free_fact(Fact* fact) {
    delete fact;
}

} // extern "C"
