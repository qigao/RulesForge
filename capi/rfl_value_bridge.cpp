/**
 * @file rfl_value_bridge.cpp
 * @brief C API bridge implementation
 */

#include "rfl_value_bridge.h"
#include "core/value_types.hpp"
#include <cstring>
#include <map>
#include <string>

/**
 * @brief Value implementation (C++ wrapper)
 */
struct Value {
    std::map<std::string, ConstraintValue> fields;
};

extern "C" {

Value* value_create_object(void) {
    try {
        return new Value();
    } catch (...) {
        return nullptr;
    }
}

void value_free(Value* obj) {
    delete obj;
}

void value_set_field_int(Value* obj, const char* name, int64_t val) {
    if (!obj || !name) return;
    obj->fields[name] = val;
}

void value_set_field_double(Value* obj, const char* name, double val) {
    if (!obj || !name) return;
    obj->fields[name] = val;
}

void value_set_field_string(Value* obj, const char* name, const char* val) {
    if (!obj || !name || !val) return;
    obj->fields[name] = std::string(val);
}

void value_set_field_bytes(Value* obj, const char* name, const uint8_t* data, size_t len) {
    if (!obj || !name || !data) return;
    // Store as string for now (TODO: proper bytes support)
    obj->fields[name] = std::string(reinterpret_cast<const char*>(data), len);
}

void value_set_field_object(Value* obj, const char* name, Value* nested) {
    if (!obj || !name || !nested) return;
    // Store nested Value's fields as a map
    // For simplicity, we flatten the structure with dot notation
    for (const auto& [key, value] : nested->fields) {
        std::string nested_key = std::string(name) + "." + key;
        obj->fields[nested_key] = value;
    }
    // Note: nested Value is not owned, caller should free it
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

    // Create a new Value with all fields that start with "name."
    Value* nested = new Value();
    std::string prefix = std::string(name) + ".";

    for (const auto& [key, value] : obj->fields) {
        if (key.find(prefix) == 0) {
            std::string nested_key = key.substr(prefix.length());
            nested->fields[nested_key] = value;
        }
    }

    if (nested->fields.empty()) {
        delete nested;
        return nullptr;
    }

    return nested;
}

Fact* value_to_fact(Value* obj, const char* type_name) {
    // TODO: Implement Value -> Fact conversion
    // Requires FactBuilder integration
    (void)obj;
    (void)type_name;
    return nullptr;
}

} // extern "C"
