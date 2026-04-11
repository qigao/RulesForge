#include "codec/fact_value_adapter.hpp"

#include <cstring>
#include <new>

namespace rulesforge {

namespace {

constexpr size_t kReservedListItems = 4;

}

thread_local FactValueAdapter* FactValueAdapter::current_adapter_ = nullptr;

FactValueAdapter::ScopedCurrent::ScopedCurrent(FactValueAdapter& adapter)
    : previous_(current_adapter_) {
    current_adapter_ = &adapter;
}

FactValueAdapter::ScopedCurrent::~ScopedCurrent() {
    current_adapter_ = previous_;
}

FactValueAdapter::ScopedParse::ScopedParse(FactValueAdapter& adapter, rulesforge::FactArena& arena)
    : adapter_(adapter)
    , current_(adapter) {
    adapter_.begin_parse(arena);
}

FactValueAdapter::ScopedParse::~ScopedParse() {
    adapter_.end_parse();
}

FactValueAdapter::FactValueAdapter() {
    object_pool_config_t config = {
        .object_size = sizeof(HandleSlot),
        .initial_capacity = 64,
        .max_capacity = 0,
        .zero_on_alloc = false
    };
    handle_pool_ = object_pool_create(&config);
    if (!handle_pool_) {
        throw std::bad_alloc();
    }

    api_.create_object = create_object_impl;
    api_.set_field_int = set_field_int_impl;
    api_.set_field_int64 = set_field_int64_impl;
    api_.set_field_double = set_field_double_impl;
    api_.set_field_string = set_field_string_impl;
    api_.set_field_bytes = set_field_bytes_impl;

    api_.create_list = create_list_impl;
    api_.add_list_item_int = add_list_item_int_impl;
    api_.add_list_item_int64 = add_list_item_int64_impl;
    api_.add_list_item_double = add_list_item_double_impl;
    api_.add_list_item_string = add_list_item_string_impl;
    api_.add_list_item_object = add_list_item_object_impl;
    api_.set_field_list = set_field_list_impl;
    api_.set_field_object = set_field_object_impl;

    api_.create_set = create_set_impl;
    api_.add_set_item_int = add_set_item_int_impl;
    api_.add_set_item_int64 = add_set_item_int64_impl;
    api_.add_set_item_double = add_set_item_double_impl;
    api_.add_set_item_string = add_set_item_string_impl;
    api_.set_field_set = set_field_set_impl;

    api_.create_map = create_map_impl;
    api_.add_map_entry_string_string = add_map_entry_string_string_impl;
    api_.add_map_entry_string_int = add_map_entry_string_int_impl;
    api_.add_map_entry_string_int64 = add_map_entry_string_int64_impl;
    api_.add_map_entry_string_double = add_map_entry_string_double_impl;
    api_.set_field_map = set_field_map_impl;
    api_.destroy_value = destroy_value_impl;
}

FactValueAdapter::~FactValueAdapter() {
    reset_state();
    if (handle_pool_) {
        object_pool_destroy(handle_pool_);
        handle_pool_ = nullptr;
    }
}

Value* FactValueAdapter::register_handle(HandleSlot::Kind kind, void* payload) {
    if (!handle_pool_) {
        return nullptr;
    }

    void* memory = object_pool_alloc(handle_pool_);
    if (!memory) {
        return nullptr;
    }

    auto* slot = static_cast<HandleSlot*>(memory);
    slot->kind = kind;
    slot->payload = payload;

    Value* value = static_cast<Value*>(payload);
    handles_[value] = slot;
    return value;
}

FactValueAdapter::HandleSlot* FactValueAdapter::find_handle(Value* value) {
    auto it = handles_.find(value);
    if (it == handles_.end()) {
        return nullptr;
    }
    return it->second;
}

void FactValueAdapter::release_handle(Value* value) {
    auto it = handles_.find(value);
    if (it == handles_.end()) {
        return;
    }

    object_pool_free(handle_pool_, it->second);
    handles_.erase(it);
}

void FactValueAdapter::reset_state() {
    for (auto& [value, slot] : handles_) {
        (void)value;
        object_pool_free(handle_pool_, slot);
    }
    handles_.clear();
    list_owners_.clear();
    set_owners_.clear();
    map_owners_.clear();
    last_fact_ = nullptr;
    root_fact_ = nullptr;
}

void FactValueAdapter::begin_parse(rulesforge::FactArena& arena) {
    reset_state();
    arena_ = &arena;
}

void FactValueAdapter::end_parse() {
    reset_state();
    arena_ = nullptr;
}

Fact* FactValueAdapter::object_for(Value* value) {
    if (!current_adapter_) return nullptr;
    auto* slot = current_adapter_->find_handle(value);
    if (!slot || slot->kind != HandleSlot::Kind::Object) return nullptr;
    return static_cast<Fact*>(slot->payload);
}

TypedList* FactValueAdapter::list_for(Value* value) {
    if (!current_adapter_) return nullptr;
    auto* slot = current_adapter_->find_handle(value);
    if (!slot || slot->kind != HandleSlot::Kind::List) return nullptr;
    return static_cast<TypedList*>(slot->payload);
}

ValueSet* FactValueAdapter::set_for(Value* value) {
    if (!current_adapter_) return nullptr;
    auto* slot = current_adapter_->find_handle(value);
    if (!slot || slot->kind != HandleSlot::Kind::Set) return nullptr;
    return static_cast<ValueSet*>(slot->payload);
}

ValueMap* FactValueAdapter::map_for(Value* value) {
    if (!current_adapter_) return nullptr;
    auto* slot = current_adapter_->find_handle(value);
    if (!slot || slot->kind != HandleSlot::Kind::Map) return nullptr;
    return static_cast<ValueMap*>(slot->payload);
}

std::shared_ptr<TypedList> FactValueAdapter::take_list_owner(TypedList* value) {
    auto it = list_owners_.find(value);
    if (it == list_owners_.end()) return {};
    auto owner = std::move(it->second);
    list_owners_.erase(it);
    return owner;
}

std::shared_ptr<ValueSet> FactValueAdapter::take_set_owner(ValueSet* value) {
    auto it = set_owners_.find(value);
    if (it == set_owners_.end()) return {};
    auto owner = std::move(it->second);
    set_owners_.erase(it);
    return owner;
}

std::shared_ptr<ValueMap> FactValueAdapter::take_map_owner(ValueMap* value) {
    auto it = map_owners_.find(value);
    if (it == map_owners_.end()) return {};
    auto owner = std::move(it->second);
    map_owners_.erase(it);
    return owner;
}

ConstraintValue& field_slot(Fact& fact, const char* name) {
    auto interned = rulesforge::StringInterner::instance().intern(std::string_view(name));
    return fact.fields[rulesforge::InternedString(interned)];
}

ConstraintValue string_value(const char* value) {
    return ConstraintValue(std::in_place_type<std::string>, value);
}

ConstraintValue bytes_value(const uint8_t* data, size_t len) {
    return ConstraintValue(std::in_place_type<std::string>, reinterpret_cast<const char*>(data), len);
}

Value* FactValueAdapter::create_object_impl() {
    if (!current_adapter_ || !current_adapter_->arena_) return nullptr;

    Fact* fact = current_adapter_->arena_->create_fact();
    current_adapter_->last_fact_ = fact;
    if (!current_adapter_->root_fact_) {
        current_adapter_->root_fact_ = fact;
    }

    return current_adapter_->register_handle(HandleSlot::Kind::Object, fact);
}

void FactValueAdapter::set_field_int_impl(Value* obj, const char* name, int32_t val) {
    Fact* fact = FactValueAdapter::object_for(obj);
    if (!fact) return;
    field_slot(*fact, name) = static_cast<int64_t>(val);
}

void FactValueAdapter::set_field_int64_impl(Value* obj, const char* name, int64_t val) {
    Fact* fact = FactValueAdapter::object_for(obj);
    if (!fact) return;
    field_slot(*fact, name) = val;
}

void FactValueAdapter::set_field_double_impl(Value* obj, const char* name, double val) {
    Fact* fact = FactValueAdapter::object_for(obj);
    if (!fact) return;
    field_slot(*fact, name) = val;
}

void FactValueAdapter::set_field_string_impl(Value* obj, const char* name, const char* val) {
    Fact* fact = FactValueAdapter::object_for(obj);
    if (!fact) return;
    if (std::strcmp(name, "__type__") == 0) {
        fact->type = val;
        return;
    }

    field_slot(*fact, name) = string_value(val);
}

void FactValueAdapter::set_field_bytes_impl(Value* obj, const char* name, const uint8_t* data, size_t len) {
    Fact* fact = FactValueAdapter::object_for(obj);
    if (!fact) return;
    field_slot(*fact, name) = bytes_value(data, len);
}

Value* FactValueAdapter::create_list_impl() {
    if (!current_adapter_) return nullptr;

    auto list = std::make_shared<TypedList>();
    list->values.reserve(kReservedListItems);
    TypedList* raw = list.get();
    current_adapter_->list_owners_.emplace(raw, std::move(list));
    return current_adapter_->register_handle(HandleSlot::Kind::List, raw);
}

void FactValueAdapter::add_list_item_int_impl(Value* list, int32_t val) {
    TypedList* typed_list = FactValueAdapter::list_for(list);
    if (!typed_list) return;
    typed_list->values.push_back(static_cast<int64_t>(val));
}

void FactValueAdapter::add_list_item_int64_impl(Value* list, int64_t val) {
    TypedList* typed_list = FactValueAdapter::list_for(list);
    if (!typed_list) return;
    typed_list->values.push_back(val);
}

void FactValueAdapter::add_list_item_double_impl(Value* list, double val) {
    TypedList* typed_list = FactValueAdapter::list_for(list);
    if (!typed_list) return;
    typed_list->values.push_back(val);
}

void FactValueAdapter::add_list_item_string_impl(Value* list, const char* val) {
    TypedList* typed_list = FactValueAdapter::list_for(list);
    if (!typed_list) return;
    typed_list->values.emplace_back(std::in_place_type<std::string>, val);
}

void FactValueAdapter::add_list_item_object_impl(Value* list, Value* obj) {
    TypedList* typed_list = FactValueAdapter::list_for(list);
    Fact* fact = FactValueAdapter::object_for(obj);
    if (!typed_list || !fact) return;

    FactList fact_list;
    fact_list.facts.push_back(fact);
    typed_list->values.push_back(std::move(fact_list));
}

void FactValueAdapter::set_field_list_impl(Value* obj, const char* name, Value* list) {
    Fact* fact = FactValueAdapter::object_for(obj);
    TypedList* typed_list = FactValueAdapter::list_for(list);
    if (!fact || !typed_list) return;

    auto owner = current_adapter_->take_list_owner(typed_list);
    if (!owner) return;
    field_slot(*fact, name) = std::move(owner);
}

void FactValueAdapter::set_field_object_impl(Value* obj, const char* name, Value* child) {
    Fact* parent = FactValueAdapter::object_for(obj);
    Fact* child_fact = FactValueAdapter::object_for(child);
    if (!parent || !child_fact) return;

    FactList fact_list;
    fact_list.facts.push_back(child_fact);
    field_slot(*parent, name) = std::move(fact_list);
}

Value* FactValueAdapter::create_set_impl() {
    if (!current_adapter_) return nullptr;

    auto value_set = std::make_shared<ValueSet>();
    ValueSet* raw = value_set.get();
    current_adapter_->set_owners_.emplace(raw, std::move(value_set));
    return current_adapter_->register_handle(HandleSlot::Kind::Set, raw);
}

void FactValueAdapter::add_set_item_int_impl(Value* set, int32_t val) {
    ValueSet* value_set = FactValueAdapter::set_for(set);
    if (!value_set) return;
    value_set->values.insert(static_cast<int64_t>(val));
}

void FactValueAdapter::add_set_item_int64_impl(Value* set, int64_t val) {
    ValueSet* value_set = FactValueAdapter::set_for(set);
    if (!value_set) return;
    value_set->values.insert(val);
}

void FactValueAdapter::add_set_item_double_impl(Value* set, double val) {
    ValueSet* value_set = FactValueAdapter::set_for(set);
    if (!value_set) return;
    value_set->values.insert(val);
}

void FactValueAdapter::add_set_item_string_impl(Value* set, const char* val) {
    ValueSet* value_set = FactValueAdapter::set_for(set);
    if (!value_set) return;
    value_set->values.emplace(std::in_place_type<std::string>, val);
}

void FactValueAdapter::set_field_set_impl(Value* obj, const char* name, Value* set) {
    Fact* fact = FactValueAdapter::object_for(obj);
    ValueSet* value_set = FactValueAdapter::set_for(set);
    if (!fact || !value_set) return;

    auto owner = current_adapter_->take_set_owner(value_set);
    if (!owner) return;
    field_slot(*fact, name) = std::move(owner);
}

Value* FactValueAdapter::create_map_impl() {
    if (!current_adapter_) return nullptr;

    auto value_map = std::make_shared<ValueMap>();
    ValueMap* raw = value_map.get();
    current_adapter_->map_owners_.emplace(raw, std::move(value_map));
    return current_adapter_->register_handle(HandleSlot::Kind::Map, raw);
}

void FactValueAdapter::add_map_entry_string_string_impl(Value* map, const char* key, const char* val) {
    ValueMap* value_map = FactValueAdapter::map_for(map);
    if (!value_map) return;
    value_map->entries.insert_or_assign(string_value(key), string_value(val));
}

void FactValueAdapter::add_map_entry_string_int_impl(Value* map, const char* key, int32_t val) {
    ValueMap* value_map = FactValueAdapter::map_for(map);
    if (!value_map) return;
    value_map->entries.insert_or_assign(string_value(key), static_cast<int64_t>(val));
}

void FactValueAdapter::add_map_entry_string_int64_impl(Value* map, const char* key, int64_t val) {
    ValueMap* value_map = FactValueAdapter::map_for(map);
    if (!value_map) return;
    value_map->entries.insert_or_assign(string_value(key), val);
}

void FactValueAdapter::add_map_entry_string_double_impl(Value* map, const char* key, double val) {
    ValueMap* value_map = FactValueAdapter::map_for(map);
    if (!value_map) return;
    value_map->entries.insert_or_assign(string_value(key), val);
}

void FactValueAdapter::set_field_map_impl(Value* obj, const char* name, Value* map) {
    Fact* fact = FactValueAdapter::object_for(obj);
    ValueMap* value_map = FactValueAdapter::map_for(map);
    if (!fact || !value_map) return;

    auto owner = current_adapter_->take_map_owner(value_map);
    if (!owner) return;
    field_slot(*fact, name) = std::move(owner);
}

void FactValueAdapter::destroy_value_impl(Value* value) {
    if (!current_adapter_ || !value) return;

    HandleSlot* slot = current_adapter_->find_handle(value);
    if (!slot) return;

    switch (slot->kind) {
        case HandleSlot::Kind::List:
            current_adapter_->list_owners_.erase(static_cast<TypedList*>(slot->payload));
            break;
        case HandleSlot::Kind::Set:
            current_adapter_->set_owners_.erase(static_cast<ValueSet*>(slot->payload));
            break;
        case HandleSlot::Kind::Map:
            current_adapter_->map_owners_.erase(static_cast<ValueMap*>(slot->payload));
            break;
        case HandleSlot::Kind::Object:
            if (current_adapter_->last_fact_ == slot->payload) {
                current_adapter_->last_fact_ = nullptr;
            }
            if (current_adapter_->root_fact_ == slot->payload) {
                current_adapter_->root_fact_ = nullptr;
            }
            break;
    }

    current_adapter_->release_handle(value);
}

} // namespace rulesforge
