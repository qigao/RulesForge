/**
 * @file json_codec.cpp
 * @brief JSON format codec implementation using TurboNet::Parser
 */

#include "json_codec.h"
#include "core/rfl_parser_state.hpp"
#include <turbo_parser.h>
#include <cstring>

JsonCodec::JsonCodec(const std::vector<ParsedDeclaration>& declarations,
                     const DataBindValueApi* api)
    : declarations_(declarations), api_(*api) {
    error_[0] = '\0';
}

static bool set_field_from_json(Value* obj, const std::string& field_name,
                                 const json_value_t* value, const ParsedField& field,
                                 const std::vector<ParsedDeclaration>& declarations,
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
                snprintf(error_buf, 256, "Field '%s' expected number, got type %d",
                         field_name.c_str(), json_type);
                return false;
            }
            break;

        case FT_Long:
        case FT_Float:
        case FT_Double:
            if (json_type == TURBO_JSON_NUMBER) {
                api.set_field_double(obj, field_name.c_str(), turbo_json_number(value));
            } else {
                snprintf(error_buf, 256, "Field '%s' expected number, got type %d",
                         field_name.c_str(), json_type);
                return false;
            }
            break;

        case FT_String:
            if (json_type == TURBO_JSON_STRING) {
                api.set_field_string(obj, field_name.c_str(), turbo_json_string(value));
            } else {
                snprintf(error_buf, 256, "Field '%s' expected string, got type %d",
                         field_name.c_str(), json_type);
                return false;
            }
            break;

        case FT_Boolean:
            if (json_type == TURBO_JSON_BOOL) {
                api.set_field_int(obj, field_name.c_str(), turbo_json_bool(value) ? 1 : 0);
            } else if (json_type == TURBO_JSON_NUMBER) {
                api.set_field_int(obj, field_name.c_str(), turbo_json_number(value) != 0.0 ? 1 : 0);
            } else {
                snprintf(error_buf, 256, "Field '%s' expected boolean, got type %d",
                         field_name.c_str(), json_type);
                return false;
            }
            break;

        case FT_List: {
            if (!api.create_list) {
                snprintf(error_buf, 256, "List type not supported (API missing create_list)");
                return false;
            }

            if (json_type != TURBO_JSON_ARRAY) {
                snprintf(error_buf, 256, "Field '%s' expected array, got type %d",
                         field_name.c_str(), json_type);
                return false;
            }

            if (field.type_params.empty()) {
                snprintf(error_buf, 256, "List field '%s' missing type parameter",
                         field_name.c_str());
                return false;
            }

            Value* list = api.create_list();
            size_t len = turbo_json_array_size(value);
            const TypeParameter& elem_type = field.type_params[0];

            for (size_t i = 0; i < len; i++) {
                const json_value_t* elem = turbo_json_array_get(value, i);
                turbo_json_type_t elem_json_type = turbo_json_type(elem);

                switch (elem_type.base_type) {
                    case FT_Int:
                        if (elem_json_type == TURBO_JSON_NUMBER) {
                            api.add_list_item_int(list, static_cast<int32_t>(turbo_json_number(elem)));
                        } else {
                            snprintf(error_buf, 256, "List '%s'[%zu] expected int", field_name.c_str(), i);
                            return false;
                        }
                        break;

                    case FT_Long:
                        if (elem_json_type == TURBO_JSON_NUMBER) {
                            api.add_list_item_int(list, static_cast<int32_t>(turbo_json_number(elem)));
                        } else {
                            snprintf(error_buf, 256, "List '%s'[%zu] expected long", field_name.c_str(), i);
                            return false;
                        }
                        break;

                    case FT_Double:
                    case FT_Float:
                        if (elem_json_type == TURBO_JSON_NUMBER) {
                            api.add_list_item_double(list, turbo_json_number(elem));
                        } else {
                            snprintf(error_buf, 256, "List '%s'[%zu] expected double", field_name.c_str(), i);
                            return false;
                        }
                        break;

                    case FT_String:
                        if (elem_json_type == TURBO_JSON_STRING) {
                            api.add_list_item_string(list, turbo_json_string(elem));
                        } else {
                            snprintf(error_buf, 256, "List '%s'[%zu] expected string", field_name.c_str(), i);
                            return false;
                        }
                        break;

                    case FT_Object: {
                        if (!api.add_list_item_object) {
                            snprintf(error_buf, 256, "Nested objects in list not supported");
                            return false;
                        }

                        if (elem_json_type != TURBO_JSON_OBJECT) {
                            snprintf(error_buf, 256, "List '%s'[%zu] expected object", field_name.c_str(), i);
                            return false;
                        }

                        Value* nested_obj = codec->parse_nested_object(elem, elem_type.custom_type);
                        if (!nested_obj) return false;

                        api.add_list_item_object(list, nested_obj);
                        break;
                    }

                    default:
                        snprintf(error_buf, 256, "Unsupported list element type for '%s'",
                                 field_name.c_str());
                        return false;
                }
            }

            api.set_field_list(obj, field_name.c_str(), list);
            break;
        }

        case FT_Object: {
            if (!api.set_field_object) {
                snprintf(error_buf, 256, "Nested objects not supported (API missing set_field_object)");
                return false;
            }

            if (json_type != TURBO_JSON_OBJECT) {
                snprintf(error_buf, 256, "Field '%s' expected object, got type %d",
                         field_name.c_str(), json_type);
                return false;
            }

            if (field.type_params.empty()) {
                snprintf(error_buf, 256, "Object field '%s' missing type parameter",
                         field_name.c_str());
                return false;
            }

            Value* nested_obj = codec->parse_nested_object(value, field.type_params[0].custom_type);
            if (!nested_obj) return false;

            api.set_field_object(obj, field_name.c_str(), nested_obj);
            break;
        }

        case FT_Set: {
            if (!api.create_set) {
                snprintf(error_buf, 256, "Set type not supported (API missing create_set)");
                return false;
            }

            if (json_type != TURBO_JSON_ARRAY) {
                snprintf(error_buf, 256, "Field '%s' expected array for Set, got type %d",
                         field_name.c_str(), json_type);
                return false;
            }

            if (field.type_params.empty()) {
                snprintf(error_buf, 256, "Set field '%s' missing type parameter",
                         field_name.c_str());
                return false;
            }

            Value* set = api.create_set();
            size_t len = turbo_json_array_size(value);
            const TypeParameter& elem_type = field.type_params[0];

            for (size_t i = 0; i < len; i++) {
                const json_value_t* elem = turbo_json_array_get(value, i);
                turbo_json_type_t elem_json_type = turbo_json_type(elem);

                switch (elem_type.base_type) {
                    case FT_Int:
                        if (elem_json_type == TURBO_JSON_NUMBER) {
                            api.add_set_item_int(set, static_cast<int32_t>(turbo_json_number(elem)));
                        }
                        break;

                    case FT_Double:
                    case FT_Float:
                        if (elem_json_type == TURBO_JSON_NUMBER) {
                            api.add_set_item_double(set, turbo_json_number(elem));
                        }
                        break;

                    case FT_String:
                        if (elem_json_type == TURBO_JSON_STRING) {
                            api.add_set_item_string(set, turbo_json_string(elem));
                        }
                        break;

                    default:
                        snprintf(error_buf, 256, "Unsupported set element type for '%s'",
                                 field_name.c_str());
                        return false;
                }
            }

            api.set_field_set(obj, field_name.c_str(), set);
            break;
        }

        case FT_Map: {
            if (!api.create_map) {
                snprintf(error_buf, 256, "Map type not supported (API missing create_map)");
                return false;
            }

            if (json_type != TURBO_JSON_OBJECT) {
                snprintf(error_buf, 256, "Field '%s' expected object for Map, got type %d",
                         field_name.c_str(), json_type);
                return false;
            }

            if (field.type_params.size() < 2) {
                snprintf(error_buf, 256, "Map field '%s' missing type parameters",
                         field_name.c_str());
                return false;
            }

            Value* map = api.create_map();
            const TypeParameter& key_type = field.type_params[0];
            const TypeParameter& val_type = field.type_params[1];

            // Only support string keys for now
            if (key_type.base_type != FT_String) {
                snprintf(error_buf, 256, "Map field '%s' only supports string keys",
                         field_name.c_str());
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
                        }
                        break;

                    case FT_Int:
                        if (val_json_type == TURBO_JSON_NUMBER) {
                            api.add_map_entry_string_int(map, key, static_cast<int32_t>(turbo_json_number(val)));
                        }
                        break;

                    case FT_Double:
                    case FT_Float:
                        if (val_json_type == TURBO_JSON_NUMBER) {
                            api.add_map_entry_string_double(map, key, turbo_json_number(val));
                        }
                        break;

                    default:
                        snprintf(error_buf, 256, "Unsupported map value type for '%s'",
                                 field_name.c_str());
                        return false;
                }
            }

            api.set_field_map(obj, field_name.c_str(), map);
            break;
        }

        default:
            snprintf(error_buf, 256, "Unknown field type for '%s'", field_name.c_str());
            return false;
    }
    return true;
}

Value* JsonCodec::parse(const char* type_name, const uint8_t* data, size_t len) {
    return parse_string(type_name, reinterpret_cast<const char*>(data));
}

Value* JsonCodec::parse_string(const char* type_name, const char* json_data) {
    if (!type_name || !json_data) {
        snprintf(error_, sizeof(error_), "Invalid arguments");
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
        snprintf(error_, sizeof(error_), "Type not found: %s", type_name);
        return nullptr;
    }

    // Parse JSON
    json_value_t* root = nullptr;
    int result = turbo_parse_json(reinterpret_cast<const uint8_t*>(json_data),
                                   strlen(json_data), &root);

    if (result != 0 || !root) {
        snprintf(error_, sizeof(error_), "JSON parse error: code %d", result);
        return nullptr;
    }

    if (turbo_json_type(root) != TURBO_JSON_OBJECT) {
        snprintf(error_, sizeof(error_), "JSON must be an object");
        turbo_free_json(&root);
        return nullptr;
    }

    // Create Value object
    Value* obj = api_.create_object();
    if (!obj) {
        snprintf(error_, sizeof(error_), "Failed to create object");
        turbo_free_json(&root);
        return nullptr;
    }

    // Set fields
    for (const auto& field : decl->fields) {
        json_value_t* field_value = turbo_json_object_get(root, field.name.c_str());
        if (!set_field_from_json(obj, field.name, field_value, field, declarations_, api_, error_, this)) {
            turbo_free_json(&root);
            return nullptr;
        }
    }

    turbo_free_json(&root);
    error_[0] = '\0';
    return obj;
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
        snprintf(error_, sizeof(error_), "Type '%s' not found in declarations", type_name.c_str());
        return nullptr;
    }

    // Create object
    Value* obj = api_.create_object();
    if (!obj) {
        snprintf(error_, sizeof(error_), "Failed to create object of type '%s'", type_name.c_str());
        return nullptr;
    }

    // Store type name for this Value* (will be set later by adapter)
    // We use set_field_string with a special internal field name
    if (api_.set_field_string) {
        api_.set_field_string(obj, "__type__", type_name.c_str());
    }

    // Recursively set fields
    for (const auto& field : decl->fields) {
        const json_value_t* field_value = turbo_json_object_get(json_obj, field.name.c_str());
        if (!set_field_from_json(obj, field.name, field_value, field, declarations_, api_, error_, this)) {
            return nullptr;
        }
    }

    return obj;
}
