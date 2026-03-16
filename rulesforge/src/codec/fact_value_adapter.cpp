#include "codec/fact_value_adapter.hpp"
#include <unordered_map>

namespace rulesforge {

// Thread-local context for mapping Value* to Fact* and TypedList*
thread_local FactValueAdapter* FactValueAdapter::current_adapter_ = nullptr;
thread_local std::unordered_map<Value*, Fact*> value_to_fact_map;
thread_local std::unordered_map<Value*, std::shared_ptr<TypedList>> value_to_list_map;
thread_local std::unordered_map<Value*, std::string> value_to_type_map;  // Track type names
thread_local std::unordered_map<Value*, std::shared_ptr<ValueSet>> value_to_set_map;
thread_local std::unordered_map<Value*, std::shared_ptr<ValueMap>> value_to_map_map;

FactValueAdapter::FactValueAdapter(rulesforge::FactArena& arena)
    : arena_(arena) {
    // Core API
    api_.create_object = create_object_impl;
    api_.set_field_int = set_field_int_impl;
    api_.set_field_double = set_field_double_impl;
    api_.set_field_string = set_field_string_impl;
    api_.set_field_bytes = set_field_bytes_impl;

    // Extended API for complex types
    api_.create_list = create_list_impl;
    api_.add_list_item_int = add_list_item_int_impl;
    api_.add_list_item_double = add_list_item_double_impl;
    api_.add_list_item_string = add_list_item_string_impl;
    api_.add_list_item_object = add_list_item_object_impl;
    api_.set_field_list = set_field_list_impl;
    api_.set_field_object = set_field_object_impl;

    // Extended API for Set
    api_.create_set = create_set_impl;
    api_.add_set_item_int = add_set_item_int_impl;
    api_.add_set_item_double = add_set_item_double_impl;
    api_.add_set_item_string = add_set_item_string_impl;
    api_.set_field_set = set_field_set_impl;

    // Extended API for Map
    api_.create_map = create_map_impl;
    api_.add_map_entry_string_string = add_map_entry_string_string_impl;
    api_.add_map_entry_string_int = add_map_entry_string_int_impl;
    api_.add_map_entry_string_double = add_map_entry_string_double_impl;
    api_.set_field_map = set_field_map_impl;

    current_adapter_ = this;
}

Value* FactValueAdapter::create_object_impl() {
    if (!current_adapter_) return nullptr;

    Fact* fact = current_adapter_->arena_.create_fact();
    current_adapter_->last_fact_ = fact;

    // Track the first (root) fact created
    if (!current_adapter_->root_fact_) {
        current_adapter_->root_fact_ = fact;
    }

    // Cast Fact* to Value* (opaque pointer)
    Value* value = reinterpret_cast<Value*>(fact);
    value_to_fact_map[value] = fact;

    return value;
}

void FactValueAdapter::set_field_int_impl(Value* obj, const char* name, int32_t val) {
    auto it = value_to_fact_map.find(obj);
    if (it == value_to_fact_map.end()) return;

    Fact* fact = it->second;
    fact->fields[name] = static_cast<int64_t>(val);
}

void FactValueAdapter::set_field_double_impl(Value* obj, const char* name, double val) {
    auto it = value_to_fact_map.find(obj);
    if (it == value_to_fact_map.end()) return;

    Fact* fact = it->second;
    fact->fields[name] = val;
}

void FactValueAdapter::set_field_string_impl(Value* obj, const char* name, const char* val) {
    auto it = value_to_fact_map.find(obj);
    if (it == value_to_fact_map.end()) return;

    Fact* fact = it->second;

    // Special handling for __type__ field
    if (strcmp(name, "__type__") == 0) {
        fact->type = val;
        return;
    }

    fact->fields[name] = std::string(val);
}

void FactValueAdapter::set_field_bytes_impl(Value* obj, const char* name, const uint8_t* data, size_t len) {
    auto it = value_to_fact_map.find(obj);
    if (it == value_to_fact_map.end()) return;

    Fact* fact = it->second;
    // Store bytes as string for now (could use custom type later)
    fact->fields[name] = std::string(reinterpret_cast<const char*>(data), len);
}

void FactValueAdapter::clear_maps() {
    value_to_fact_map.clear();
    value_to_list_map.clear();
    value_to_type_map.clear();
    value_to_set_map.clear();
    value_to_map_map.clear();
    root_fact_ = nullptr;  // Reset root fact for next parse
}

// ───── Extended API for complex types ─────

Value* FactValueAdapter::create_list_impl() {
    if (!current_adapter_) return nullptr;

    auto list = std::make_shared<TypedList>();
    Value* value = reinterpret_cast<Value*>(list.get());
    value_to_list_map[value] = list;  // Keep shared_ptr alive
    return value;
}

void FactValueAdapter::add_list_item_int_impl(Value* list, int32_t val) {
    auto it = value_to_list_map.find(list);
    if (it == value_to_list_map.end()) return;

    it->second->values.push_back(static_cast<int64_t>(val));
}

void FactValueAdapter::add_list_item_double_impl(Value* list, double val) {
    auto it = value_to_list_map.find(list);
    if (it == value_to_list_map.end()) return;

    it->second->values.push_back(val);
}

void FactValueAdapter::add_list_item_string_impl(Value* list, const char* val) {
    auto it = value_to_list_map.find(list);
    if (it == value_to_list_map.end()) return;

    it->second->values.push_back(std::string(val));
}

void FactValueAdapter::add_list_item_object_impl(Value* list, Value* obj) {
    auto list_it = value_to_list_map.find(list);
    if (list_it == value_to_list_map.end()) return;

    auto fact_it = value_to_fact_map.find(obj);
    if (fact_it == value_to_fact_map.end()) return;

    // Store Fact* in FactList
    FactList fact_list;
    fact_list.facts.push_back(fact_it->second);
    list_it->second->values.push_back(fact_list);
}

void FactValueAdapter::set_field_list_impl(Value* obj, const char* name, Value* list) {
    auto fact_it = value_to_fact_map.find(obj);
    if (fact_it == value_to_fact_map.end()) return;

    auto list_it = value_to_list_map.find(list);
    if (list_it == value_to_list_map.end()) return;

    Fact* fact = fact_it->second;
    fact->fields[name] = list_it->second;  // Store shared_ptr<TypedList>
}

void FactValueAdapter::set_field_object_impl(Value* obj, const char* name, Value* child) {
    auto parent_it = value_to_fact_map.find(obj);
    if (parent_it == value_to_fact_map.end()) return;

    auto child_it = value_to_fact_map.find(child);
    if (child_it == value_to_fact_map.end()) return;

    Fact* parent = parent_it->second;
    Fact* child_fact = child_it->second;

    // Store nested Fact as FactList with single element
    FactList fact_list;
    fact_list.facts.push_back(child_fact);
    parent->fields[name] = fact_list;
}

// ───── Extended API for Set ─────

Value* FactValueAdapter::create_set_impl() {
    if (!current_adapter_) return nullptr;

    auto value_set = std::make_shared<ValueSet>();
    Value* value = reinterpret_cast<Value*>(value_set.get());
    value_to_set_map[value] = value_set;
    return value;
}

void FactValueAdapter::add_set_item_int_impl(Value* set, int32_t val) {
    auto it = value_to_set_map.find(set);
    if (it == value_to_set_map.end()) return;

    it->second->values.insert(static_cast<int64_t>(val));
}

void FactValueAdapter::add_set_item_double_impl(Value* set, double val) {
    auto it = value_to_set_map.find(set);
    if (it == value_to_set_map.end()) return;

    it->second->values.insert(val);
}

void FactValueAdapter::add_set_item_string_impl(Value* set, const char* val) {
    auto it = value_to_set_map.find(set);
    if (it == value_to_set_map.end()) return;

    it->second->values.insert(std::string(val));
}

void FactValueAdapter::set_field_set_impl(Value* obj, const char* name, Value* set) {
    auto fact_it = value_to_fact_map.find(obj);
    if (fact_it == value_to_fact_map.end()) return;

    auto set_it = value_to_set_map.find(set);
    if (set_it == value_to_set_map.end()) return;

    Fact* fact = fact_it->second;
    fact->fields[name] = set_it->second;
}

// ───── Extended API for Map ─────

Value* FactValueAdapter::create_map_impl() {
    if (!current_adapter_) return nullptr;

    auto value_map = std::make_shared<ValueMap>();
    Value* value = reinterpret_cast<Value*>(value_map.get());
    value_to_map_map[value] = value_map;
    return value;
}

void FactValueAdapter::add_map_entry_string_string_impl(Value* map, const char* key, const char* val) {
    auto it = value_to_map_map.find(map);
    if (it == value_to_map_map.end()) return;

    it->second->entries[std::string(key)] = std::string(val);
}

void FactValueAdapter::add_map_entry_string_int_impl(Value* map, const char* key, int32_t val) {
    auto it = value_to_map_map.find(map);
    if (it == value_to_map_map.end()) return;

    it->second->entries[std::string(key)] = static_cast<int64_t>(val);
}

void FactValueAdapter::add_map_entry_string_double_impl(Value* map, const char* key, double val) {
    auto it = value_to_map_map.find(map);
    if (it == value_to_map_map.end()) return;

    it->second->entries[std::string(key)] = val;
}

void FactValueAdapter::set_field_map_impl(Value* obj, const char* name, Value* map) {
    auto fact_it = value_to_fact_map.find(obj);
    if (fact_it == value_to_fact_map.end()) return;

    auto map_it = value_to_map_map.find(map);
    if (map_it == value_to_map_map.end()) return;

    Fact* fact = fact_it->second;
    fact->fields[name] = map_it->second;
}

} // namespace rulesforge
