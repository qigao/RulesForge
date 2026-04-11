/**
 * @file json_codec.cpp
 * @brief JSON format codec implementation using TurboNet::Parser
 */

#include "json_codec.h"
#include "core/rfl_parser_state.hpp"
#include <turbo_parser.h>
#include <fmt.h>
#include <cstring>
#include <map>

static bool require_callback(bool ok, char* error, size_t error_sz, const char* message) {
    if (ok) return true;
    fmt(error, error_sz, "{}", message);
    return false;
}

static void destroy_value(const DataBindValueApi& api, Value* value) {
    if (value) {
        api.destroy_value(value);
    }
}

class ScopedTempValue {
public:
    ScopedTempValue(const DataBindValueApi& api, Value* value)
        : api_(api), value_(value) {}

    ~ScopedTempValue() {
        destroy_value(api_, value_);
    }

    Value* get() const { return value_; }

    Value* release() {
        Value* value = value_;
        value_ = nullptr;
        return value;
    }

private:
    const DataBindValueApi& api_;
    Value* value_;
};

static bool validate_json_field_api(const ParsedField& field, const std::string& full_name,
                                    const std::map<std::string, ParsedDeclaration>& decls_by_name,
                                    const std::map<std::string, ParsedEnum>& enums_by_name,
                                    const DataBindValueApi* api, char* error, size_t error_sz);

static bool validate_json_fields_api(const std::vector<ParsedField>& fields, const std::string& prefix,
                                     const std::map<std::string, ParsedDeclaration>& decls_by_name,
                                     const std::map<std::string, ParsedEnum>& enums_by_name,
                                     const DataBindValueApi* api, char* error, size_t error_sz) {
    for (const auto& field : fields) {
        const std::string full_name = prefix.empty() ? field.name : prefix + "." + field.name;
        if (!validate_json_field_api(field, full_name, decls_by_name, enums_by_name, api, error, error_sz)) {
            return false;
        }
    }
    return true;
}

static bool validate_json_field_api(const ParsedField& field, const std::string& full_name,
                                    const std::map<std::string, ParsedDeclaration>& decls_by_name,
                                    const std::map<std::string, ParsedEnum>& enums_by_name,
                                    const DataBindValueApi* api, char* error, size_t error_sz) {
    switch (field.type) {
        case FT_Boolean:
        case FT_Int:
            return require_callback(api->set_field_int != nullptr, error, error_sz,
                                    ("Schema field '" + full_name + "' requires set_field_int").c_str());
        case FT_Long:
            return require_callback(api->set_field_int64 != nullptr, error, error_sz,
                                    ("Schema field '" + full_name + "' requires set_field_int64").c_str());
        case FT_Float:
        case FT_Double:
            return require_callback(api->set_field_double != nullptr, error, error_sz,
                                    ("Schema field '" + full_name + "' requires set_field_double").c_str());
        case FT_String:
            return require_callback(api->set_field_string != nullptr, error, error_sz,
                                    ("Schema field '" + full_name + "' requires set_field_string").c_str());
        case FT_Object:
            if (field.type_params.empty()) {
                return require_callback(false, error, error_sz,
                                        ("Schema field '" + full_name + "' is missing object type").c_str());
            }
            if (auto enum_it = enums_by_name.find(field.type_params[0].custom_type); enum_it != enums_by_name.end()) {
                ParsedField enum_field{field.name, parse_field_type(enum_it->second.underlying_type), {}};
                return validate_json_field_api(enum_field, full_name, decls_by_name, enums_by_name, api, error,
                                               error_sz);
            }
            if (!require_callback(api->set_field_object != nullptr, error, error_sz,
                                  ("Schema field '" + full_name + "' requires set_field_object").c_str())) {
                return false;
            }
            if (auto decl_it = decls_by_name.find(field.type_params[0].custom_type); decl_it != decls_by_name.end()) {
                return validate_json_fields_api(decl_it->second.fields, full_name, decls_by_name, enums_by_name, api,
                                                error, error_sz);
            }
            return require_callback(false, error, error_sz,
                                    ("Unknown object type for field '" + full_name + "'").c_str());
        case FT_List:
            if (field.type_params.empty()) {
                return require_callback(false, error, error_sz,
                                        ("Schema field '" + full_name + "' requires a List element type").c_str());
            }
            if (!require_callback(api->create_list != nullptr && api->set_field_list != nullptr, error, error_sz,
                                  ("Schema field '" + full_name
                                   + "' requires List API (create_list/set_field_list)")
                                      .c_str())) {
                return false;
            }
            switch (field.type_params[0].base_type) {
                case FT_String:
                    return require_callback(api->add_list_item_string != nullptr, error, error_sz,
                                            ("Schema field '" + full_name + "' requires add_list_item_string").c_str());
                case FT_Long:
                    return require_callback(api->add_list_item_int64 != nullptr, error, error_sz,
                                            ("Schema field '" + full_name + "' requires add_list_item_int64").c_str());
                case FT_Float:
                case FT_Double:
                    return require_callback(api->add_list_item_double != nullptr, error, error_sz,
                                            ("Schema field '" + full_name + "' requires add_list_item_double").c_str());
                case FT_Boolean:
                case FT_Int:
                    return require_callback(api->add_list_item_int != nullptr, error, error_sz,
                                            ("Schema field '" + full_name + "' requires add_list_item_int").c_str());
                case FT_Object:
                    return require_callback(api->add_list_item_object != nullptr, error, error_sz,
                                            ("Schema field '" + full_name + "' requires add_list_item_object").c_str());
                default:
                    return require_callback(false, error, error_sz,
                                            ("Unsupported List element type for field '" + full_name + "'").c_str());
            }
        case FT_Set:
            if (field.type_params.empty()) {
                return require_callback(false, error, error_sz,
                                        ("Schema field '" + full_name + "' requires a Set element type").c_str());
            }
            if (!require_callback(api->create_set != nullptr && api->set_field_set != nullptr, error, error_sz,
                                  ("Schema field '" + full_name
                                   + "' requires Set API (create_set/set_field_set)")
                                      .c_str())) {
                return false;
            }
            switch (field.type_params[0].base_type) {
                case FT_String:
                    return require_callback(api->add_set_item_string != nullptr, error, error_sz,
                                            ("Schema field '" + full_name + "' requires add_set_item_string").c_str());
                case FT_Float:
                case FT_Double:
                    return require_callback(api->add_set_item_double != nullptr, error, error_sz,
                                            ("Schema field '" + full_name + "' requires add_set_item_double").c_str());
                case FT_Boolean:
                case FT_Int:
                    return require_callback(api->add_set_item_int != nullptr, error, error_sz,
                                            ("Schema field '" + full_name + "' requires add_set_item_int").c_str());
                case FT_Long:
                    return require_callback(api->add_set_item_int64 != nullptr, error, error_sz,
                                            ("Schema field '" + full_name + "' requires add_set_item_int64").c_str());
                default:
                    return require_callback(false, error, error_sz,
                                            ("Unsupported Set element type for field '" + full_name + "'").c_str());
            }
        case FT_Map:
            if (field.type_params.size() < 2) {
                return require_callback(false, error, error_sz,
                                        ("Schema field '" + full_name + "' requires Map<K, V> types").c_str());
            }
            if (field.type_params[0].base_type != FT_String) {
                return require_callback(false, error, error_sz,
                                        ("Schema field '" + full_name + "' only supports Map<String, T>").c_str());
            }
            if (!require_callback(api->create_map != nullptr && api->set_field_map != nullptr, error, error_sz,
                                  ("Schema field '" + full_name
                                   + "' requires Map API (create_map/set_field_map)")
                                      .c_str())) {
                return false;
            }
            switch (field.type_params[1].base_type) {
                case FT_String:
                    return require_callback(api->add_map_entry_string_string != nullptr, error, error_sz,
                                            ("Schema field '" + full_name
                                             + "' requires add_map_entry_string_string")
                                                .c_str());
                case FT_Float:
                case FT_Double:
                    return require_callback(api->add_map_entry_string_double != nullptr, error, error_sz,
                                            ("Schema field '" + full_name
                                             + "' requires add_map_entry_string_double")
                                                .c_str());
                case FT_Boolean:
                case FT_Int:
                    return require_callback(api->add_map_entry_string_int != nullptr, error, error_sz,
                                            ("Schema field '" + full_name
                                             + "' requires add_map_entry_string_int")
                                                .c_str());
                case FT_Long:
                    return require_callback(api->add_map_entry_string_int64 != nullptr, error, error_sz,
                                            ("Schema field '" + full_name
                                             + "' requires add_map_entry_string_int64")
                                                .c_str());
                default:
                    return require_callback(false, error, error_sz,
                                            ("Unsupported Map value type for field '" + full_name + "'").c_str());
            }
        default:
            return require_callback(false, error, error_sz,
                                    ("Unsupported JSON field type for '" + full_name + "'").c_str());
    }
}

static bool validate_api_for_declarations(const std::vector<ParsedDeclaration>& declarations,
                                          const std::vector<ParsedEnum>& enums,
                                          const DataBindValueApi* api,
                                          char* error,
                                          size_t error_sz) {
    if (!require_callback(api != nullptr, error, error_sz, "Value API must not be null")) return false;
    if (!require_callback(api->create_object != nullptr, error, error_sz, "JSON codec requires create_object")) {
        return false;
    }
    if (!require_callback(api->destroy_value != nullptr, error, error_sz, "JSON codec requires destroy_value")) {
        return false;
    }

    std::map<std::string, ParsedDeclaration> decls_by_name;
    for (const auto& decl : declarations) {
        decls_by_name.emplace(decl.type_name, decl);
    }

    std::map<std::string, ParsedEnum> enums_by_name;
    for (const auto& enum_decl : enums) {
        enums_by_name.emplace(enum_decl.enum_name, enum_decl);
    }

    for (const auto& decl : declarations) {
        if (!validate_json_fields_api(decl.fields, decl.type_name, decls_by_name, enums_by_name, api, error,
                                      error_sz)) {
            return false;
        }
    }
    return true;
}

JsonCodec::JsonCodec(const std::vector<ParsedDeclaration>& declarations,
                     const std::vector<ParsedEnum>& enums,
                     const DataBindValueApi* api)
    : declarations_(declarations), enums_(enums), api_(*api) {
    error_[0] = '\0';
    validate_api_for_declarations(declarations_, enums_, api, error_, sizeof(error_));
}

static bool set_field_from_json(Value* obj, const std::string& field_name,
                                 const json_value_t* value, const ParsedField& field,
                                 const std::vector<ParsedDeclaration>& declarations,
                                 const std::vector<ParsedEnum>& enums,
                                 const DataBindValueApi& api, char* error_buf,
                                 JsonCodec* codec) {
    if (!value) {
        return true; // Optional field
    }

    turbo_json_type_t json_type = turbo_json_type(value);

    switch (field.type) {
        case FT_Int:
            if (json_type == TURBO_JSON_NUMBER) {
                api.set_field_int(obj, field_name.c_str(), static_cast<int32_t>(turbo_json_number(value)));
            } else if (json_type == TURBO_JSON_BOOL) {
                api.set_field_int(obj, field_name.c_str(), turbo_json_bool(value) ? 1 : 0);
            } else {
                fmt(error_buf, 256, "Field '{}' expected number, got type {}",
                         field_name, json_type);
                return false;
            }
            break;

        case FT_Long:
            if (json_type == TURBO_JSON_NUMBER) {
                api.set_field_int64(obj, field_name.c_str(), static_cast<int64_t>(turbo_json_number(value)));
            } else {
                fmt(error_buf, 256, "Field '{}' expected number, got type {}",
                         field_name, json_type);
                return false;
            }
            break;

        case FT_Float:
        case FT_Double:
            if (json_type == TURBO_JSON_NUMBER) {
                api.set_field_double(obj, field_name.c_str(), turbo_json_number(value));
            } else {
                fmt(error_buf, 256, "Field '{}' expected number, got type {}",
                         field_name, json_type);
                return false;
            }
            break;

        case FT_String:
            if (json_type == TURBO_JSON_STRING) {
                api.set_field_string(obj, field_name.c_str(), turbo_json_string(value));
            } else {
                fmt(error_buf, 256, "Field '{}' expected string, got type {}",
                         field_name, json_type);
                return false;
            }
            break;

        case FT_Boolean:
            if (json_type == TURBO_JSON_BOOL) {
                api.set_field_int(obj, field_name.c_str(), turbo_json_bool(value) ? 1 : 0);
            } else if (json_type == TURBO_JSON_NUMBER) {
                api.set_field_int(obj, field_name.c_str(), turbo_json_number(value) != 0.0 ? 1 : 0);
            } else {
                fmt(error_buf, 256, "Field '{}' expected boolean, got type {}",
                         field_name, json_type);
                return false;
            }
            break;

        case FT_List: {
            if (json_type != TURBO_JSON_ARRAY) {
                fmt(error_buf, 256, "Field '{}' expected array, got type {}",
                         field_name, json_type);
                return false;
            }

            if (field.type_params.empty()) {
                fmt(error_buf, 256, "List field '{}' missing type parameter",
                         field_name);
                return false;
            }

            Value* list = api.create_list();
            if (!list) {
                fmt(error_buf, 256, "Failed to create list for '{}'", field_name);
                return false;
            }
            ScopedTempValue list_guard(api, list);
            size_t len = turbo_json_array_size(value);
            const TypeParameter& elem_type = field.type_params[0];

            for (size_t i = 0; i < len; i++) {
                const json_value_t* elem = turbo_json_array_get(value, i);
                turbo_json_type_t elem_json_type = turbo_json_type(elem);

                switch (elem_type.base_type) {
                    case FT_Int:
                        if (elem_json_type == TURBO_JSON_NUMBER) {
                            api.add_list_item_int(list, static_cast<int32_t>(turbo_json_number(elem)));
                        } else if (elem_json_type == TURBO_JSON_BOOL) {
                            api.add_list_item_int(list, turbo_json_bool(elem) ? 1 : 0);
                        } else {
                            fmt(error_buf, 256, "List '{}'[{}] expected int", field_name, i);
                            return false;
                        }
                        break;

                    case FT_Boolean:
                        if (elem_json_type == TURBO_JSON_BOOL) {
                            api.add_list_item_int(list, turbo_json_bool(elem) ? 1 : 0);
                        } else if (elem_json_type == TURBO_JSON_NUMBER) {
                            api.add_list_item_int(list, turbo_json_number(elem) != 0.0 ? 1 : 0);
                        } else {
                            fmt(error_buf, 256, "List '{}'[{}] expected boolean", field_name, i);
                            return false;
                        }
                        break;

                    case FT_Long:
                        if (elem_json_type == TURBO_JSON_NUMBER) {
                            api.add_list_item_int64(list, static_cast<int64_t>(turbo_json_number(elem)));
                        } else {
                            fmt(error_buf, 256, "List '{}'[{}] expected long", field_name, i);
                            return false;
                        }
                        break;

                    case FT_Double:
                    case FT_Float:
                        if (elem_json_type == TURBO_JSON_NUMBER) {
                            api.add_list_item_double(list, turbo_json_number(elem));
                        } else {
                            fmt(error_buf, 256, "List '{}'[{}] expected double", field_name, i);
                            return false;
                        }
                        break;

                    case FT_String:
                        if (elem_json_type == TURBO_JSON_STRING) {
                            api.add_list_item_string(list, turbo_json_string(elem));
                        } else {
                            fmt(error_buf, 256, "List '{}'[{}] expected string", field_name, i);
                            return false;
                        }
                        break;

                    case FT_Object: {
                        if (elem_json_type != TURBO_JSON_OBJECT) {
                            fmt(error_buf, 256, "List '{}'[{}] expected object", field_name, i);
                            return false;
                        }

                        Value* nested_obj = codec->parse_nested_object(elem, elem_type.custom_type);
                        if (!nested_obj) return false;
                        ScopedTempValue nested_guard(api, nested_obj);
                        api.add_list_item_object(list, nested_obj);
                        nested_guard.release();
                        break;
                    }

                    default:
                        fmt(error_buf, 256, "Unsupported list element type for '{}'",
                                 field_name);
                        return false;
                }
            }

            api.set_field_list(obj, field_name.c_str(), list);
            list_guard.release();
            break;
        }

        case FT_Object: {
            if (!field.type_params.empty()) {
                for (const auto& enum_decl : enums) {
                    if (enum_decl.enum_name == field.type_params[0].custom_type) {
                        ParsedField enum_field{field.name, parse_field_type(enum_decl.underlying_type), {}};
                        return set_field_from_json(obj, field_name, value, enum_field, declarations, enums, api,
                                                   error_buf, codec);
                    }
                }
            }

            if (json_type != TURBO_JSON_OBJECT) {
                fmt(error_buf, 256, "Field '{}' expected object, got type {}",
                         field_name, json_type);
                return false;
            }

            if (field.type_params.empty()) {
                fmt(error_buf, 256, "Object field '{}' missing type parameter",
                         field_name);
                return false;
            }

            Value* nested_obj = codec->parse_nested_object(value, field.type_params[0].custom_type);
            if (!nested_obj) return false;
            ScopedTempValue nested_guard(api, nested_obj);
            api.set_field_object(obj, field_name.c_str(), nested_obj);
            nested_guard.release();
            break;
        }

        case FT_Set: {
            if (json_type != TURBO_JSON_ARRAY) {
                fmt(error_buf, 256, "Field '{}' expected array for Set, got type {}",
                         field_name, json_type);
                return false;
            }

            if (field.type_params.empty()) {
                fmt(error_buf, 256, "Set field '{}' missing type parameter",
                         field_name);
                return false;
            }

            Value* set = api.create_set();
            if (!set) {
                fmt(error_buf, 256, "Failed to create set for '{}'", field_name);
                return false;
            }
            ScopedTempValue set_guard(api, set);
            size_t len = turbo_json_array_size(value);
            const TypeParameter& elem_type = field.type_params[0];

            for (size_t i = 0; i < len; i++) {
                const json_value_t* elem = turbo_json_array_get(value, i);
                turbo_json_type_t elem_json_type = turbo_json_type(elem);

                switch (elem_type.base_type) {
                    case FT_Int:
                        if (elem_json_type == TURBO_JSON_NUMBER) {
                            api.add_set_item_int(set, static_cast<int32_t>(turbo_json_number(elem)));
                        } else if (elem_json_type == TURBO_JSON_BOOL) {
                            api.add_set_item_int(set, turbo_json_bool(elem) ? 1 : 0);
                        } else {
                            fmt(error_buf, 256, "Set '{}'[{}] expected int", field_name, i);
                            return false;
                        }
                        break;

                    case FT_Boolean:
                        if (elem_json_type == TURBO_JSON_BOOL) {
                            api.add_set_item_int(set, turbo_json_bool(elem) ? 1 : 0);
                        } else if (elem_json_type == TURBO_JSON_NUMBER) {
                            api.add_set_item_int(set, turbo_json_number(elem) != 0.0 ? 1 : 0);
                        } else {
                            fmt(error_buf, 256, "Set '{}'[{}] expected boolean", field_name, i);
                            return false;
                        }
                        break;

                    case FT_Long:
                        if (elem_json_type == TURBO_JSON_NUMBER) {
                            api.add_set_item_int64(set, static_cast<int64_t>(turbo_json_number(elem)));
                        } else {
                            fmt(error_buf, 256, "Set '{}'[{}] expected long", field_name, i);
                            return false;
                        }
                        break;

                    case FT_Double:
                    case FT_Float:
                        if (elem_json_type == TURBO_JSON_NUMBER) {
                            api.add_set_item_double(set, turbo_json_number(elem));
                        } else {
                            fmt(error_buf, 256, "Set '{}'[{}] expected double", field_name, i);
                            return false;
                        }
                        break;

                    case FT_String:
                        if (elem_json_type == TURBO_JSON_STRING) {
                            api.add_set_item_string(set, turbo_json_string(elem));
                        } else {
                            fmt(error_buf, 256, "Set '{}'[{}] expected string", field_name, i);
                            return false;
                        }
                        break;

                    default:
                        fmt(error_buf, 256, "Unsupported set element type for '{}'",
                                 field_name);
                        return false;
                }
            }

            api.set_field_set(obj, field_name.c_str(), set);
            set_guard.release();
            break;
        }

        case FT_Map: {
            if (json_type != TURBO_JSON_OBJECT) {
                fmt(error_buf, 256, "Field '{}' expected object for Map, got type {}",
                         field_name, json_type);
                return false;
            }

            if (field.type_params.size() < 2) {
                fmt(error_buf, 256, "Map field '{}' missing type parameters",
                         field_name);
                return false;
            }

            Value* map = api.create_map();
            if (!map) {
                fmt(error_buf, 256, "Failed to create map for '{}'", field_name);
                return false;
            }
            ScopedTempValue map_guard(api, map);
            const TypeParameter& key_type = field.type_params[0];
            const TypeParameter& val_type = field.type_params[1];

            // Only support string keys for now
            if (key_type.base_type != FT_String) {
                fmt(error_buf, 256, "Map field '{}' only supports string keys",
                         field_name);
                return false;
            }

            // Iterate over JSON object entries
            size_t entry_count = turbo_json_object_size(value);
            for (size_t i = 0; i < entry_count; i++) {
                const char* key = turbo_json_object_key(value, i);
                json_value_t* val = turbo_json_object_value(value, i);
                turbo_json_type_t val_json_type = turbo_json_type(val);

                switch (val_type.base_type) {
                    case FT_String:
                        if (val_json_type == TURBO_JSON_STRING) {
                            api.add_map_entry_string_string(map, key, turbo_json_string(val));
                        } else {
                            fmt(error_buf, 256, "Map '{}'['{}'] expected string", field_name, key);
                            return false;
                        }
                        break;

                    case FT_Int:
                        if (val_json_type == TURBO_JSON_NUMBER) {
                            api.add_map_entry_string_int(map, key, static_cast<int32_t>(turbo_json_number(val)));
                        } else if (val_json_type == TURBO_JSON_BOOL) {
                            api.add_map_entry_string_int(map, key, turbo_json_bool(val) ? 1 : 0);
                        } else {
                            fmt(error_buf, 256, "Map '{}'['{}'] expected int", field_name, key);
                            return false;
                        }
                        break;

                    case FT_Boolean:
                        if (val_json_type == TURBO_JSON_BOOL) {
                            api.add_map_entry_string_int(map, key, turbo_json_bool(val) ? 1 : 0);
                        } else if (val_json_type == TURBO_JSON_NUMBER) {
                            api.add_map_entry_string_int(map, key, turbo_json_number(val) != 0.0 ? 1 : 0);
                        } else {
                            fmt(error_buf, 256, "Map '{}'['{}'] expected boolean", field_name, key);
                            return false;
                        }
                        break;

                    case FT_Long:
                        if (val_json_type == TURBO_JSON_NUMBER) {
                            api.add_map_entry_string_int64(map, key, static_cast<int64_t>(turbo_json_number(val)));
                        } else {
                            fmt(error_buf, 256, "Map '{}'['{}'] expected long", field_name, key);
                            return false;
                        }
                        break;

                    case FT_Double:
                    case FT_Float:
                        if (val_json_type == TURBO_JSON_NUMBER) {
                            api.add_map_entry_string_double(map, key, turbo_json_number(val));
                        } else {
                            fmt(error_buf, 256, "Map '{}'['{}'] expected double", field_name, key);
                            return false;
                        }
                        break;

                    default:
                        fmt(error_buf, 256, "Unsupported map value type for '{}'",
                                 field_name);
                        return false;
                }
            }

            api.set_field_map(obj, field_name.c_str(), map);
            map_guard.release();
            break;
        }

        default:
            fmt(error_buf, 256, "Unknown field type for '{}'", field_name);
            return false;
    }
    return true;
}

Value* JsonCodec::parse(const char* type_name, const uint8_t* data, size_t len) {
    (void)len;
    return parse_string(type_name, reinterpret_cast<const char*>(data));
}

Value* JsonCodec::parse_string(const char* type_name, const char* json_data) {
    error_[0] = '\0';

    if (!type_name || !json_data) {
        fmt(error_, sizeof(error_), "Invalid arguments");
        return nullptr;
    }

    // Find declaration
    const ParsedDeclaration* decl = nullptr;
    for (const auto& d : declarations_) {
        if (d.type_name == type_name) {
            decl = &d;
            break;
        }
    }

    if (!decl) {
        fmt(error_, sizeof(error_), "Type not found: {}", type_name);
        return nullptr;
    }

    // Parse JSON
    json_value_t* root = nullptr;
    int result = turbo_parse_json(reinterpret_cast<const uint8_t*>(json_data),
                                   strlen(json_data), &root);

    if (result != 0 || !root) {
        fmt(error_, sizeof(error_), "JSON parse error: code {}", result);
        return nullptr;
    }

    if (turbo_json_type(root) != TURBO_JSON_OBJECT) {
        fmt(error_, sizeof(error_), "JSON must be an object");
        turbo_free_json(&root);
        return nullptr;
    }

    // Create Value object
    Value* obj = api_.create_object();
    if (!obj) {
        fmt(error_, sizeof(error_), "Failed to create object");
        turbo_free_json(&root);
        return nullptr;
    }
    ScopedTempValue obj_guard(api_, obj);

    // Set fields
    for (const auto& field : decl->fields) {
        json_value_t* field_value = turbo_json_object_get(root, field.name.c_str());
        if (!set_field_from_json(obj, field.name, field_value, field, declarations_, enums_, api_, error_, this)) {
            turbo_free_json(&root);
            return nullptr;
        }
    }

    turbo_free_json(&root);
    error_[0] = '\0';
    return obj_guard.release();
}

const char* JsonCodec::get_error() const {
    return error_;
}

Value* JsonCodec::parse_nested_object(const json_value_t* json_obj, const std::string& type_name) {
    // Find type declaration
    const ParsedDeclaration* decl = nullptr;
    for (const auto& d : declarations_) {
        if (d.type_name == type_name) {
            decl = &d;
            break;
        }
    }

    if (!decl) {
        fmt(error_, sizeof(error_), "Type '{}' not found in declarations", type_name);
        return nullptr;
    }

    // Create object
    Value* obj = api_.create_object();
    if (!obj) {
        fmt(error_, sizeof(error_), "Failed to create object of type '{}'", type_name);
        return nullptr;
    }
    ScopedTempValue obj_guard(api_, obj);

    // Store type name for this Value* (will be set later by adapter)
    // We use set_field_string with a special internal field name
    if (api_.set_field_string) {
        api_.set_field_string(obj, "__type__", type_name.c_str());
    }

    // Recursively set fields
    for (const auto& field : decl->fields) {
        const json_value_t* field_value = turbo_json_object_get(json_obj, field.name.c_str());
        if (!set_field_from_json(obj, field.name, field_value, field, declarations_, enums_, api_, error_, this)) {
            return nullptr;
        }
    }

    return obj_guard.release();
}
