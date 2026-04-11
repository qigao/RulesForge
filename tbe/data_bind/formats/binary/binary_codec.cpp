/**
 * @file binary_codec.cpp
 * @brief Binary format codec with MIR JIT compilation
 */

#include "binary_codec.h"
#include "mir_codegen.h"
#include "core/rfl_parser_state.hpp"
#include <mir.h>
#include <mir-gen.h>
#include <map>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fmt.h>

extern "C" {
extern char* tbe_read_varstring(const uint8_t* buf, size_t offset);
}

/* ───── Validate API callbacks against schema requirements ───── */

static bool require_callback(bool ok, char* error, size_t error_sz, const char* message) {
    if (ok) return true;
    fmt(error, error_sz, "{}", message);
    return false;
}

static bool validate_field_api(const ParsedField& field, const std::string& full_name,
                               const std::map<std::string, ParsedDeclaration>& decls_by_name,
                               const std::map<std::string, ParsedEnum>& enums_by_name,
                               const DataBindValueApi* api, char* error, size_t error_sz);

static bool validate_fields_api(const std::vector<ParsedField>& fields, const std::string& prefix,
                                const std::map<std::string, ParsedDeclaration>& decls_by_name,
                                const std::map<std::string, ParsedEnum>& enums_by_name,
                                const DataBindValueApi* api, char* error, size_t error_sz) {
    for (const auto& field : fields) {
        const std::string full_name = prefix.empty() ? field.name : prefix + "." + field.name;
        if (!validate_field_api(field, full_name, decls_by_name, enums_by_name, api, error, error_sz)) {
            return false;
        }
    }
    return true;
}

static bool validate_field_api(const ParsedField& field, const std::string& full_name,
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
                return validate_field_api(enum_field, full_name, decls_by_name, enums_by_name, api, error, error_sz);
            }
            if (auto decl_it = decls_by_name.find(field.type_params[0].custom_type); decl_it != decls_by_name.end()) {
                return validate_fields_api(decl_it->second.fields, full_name, decls_by_name, enums_by_name, api, error,
                                           error_sz);
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
                case FT_Long:
                    return require_callback(api->add_set_item_int64 != nullptr, error, error_sz,
                                            ("Schema field '" + full_name + "' requires add_set_item_int64").c_str());
                case FT_Float:
                case FT_Double:
                    return require_callback(api->add_set_item_double != nullptr, error, error_sz,
                                            ("Schema field '" + full_name + "' requires add_set_item_double").c_str());
                case FT_Boolean:
                case FT_Int:
                    return require_callback(api->add_set_item_int != nullptr, error, error_sz,
                                            ("Schema field '" + full_name + "' requires add_set_item_int").c_str());
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
                                        ("Schema field '" + full_name
                                         + "' is unsupported: binary codec only handles Map<String, T>")
                                            .c_str());
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
                case FT_Long:
                    return require_callback(api->add_map_entry_string_int64 != nullptr, error, error_sz,
                                            ("Schema field '" + full_name
                                             + "' requires add_map_entry_string_int64")
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
                default:
                    return require_callback(false, error, error_sz,
                                            ("Unsupported Map value type for field '" + full_name + "'").c_str());
            }
        default:
            return require_callback(false, error, error_sz,
                                    ("Unsupported binary field type for '" + full_name + "'").c_str());
    }
}

static bool validate_api_for_fields(const std::vector<ParsedDeclaration>& decls,
                                    const std::vector<ParsedEnum>& enums,
                                    const DataBindValueApi* api, char* error, size_t error_sz) {
    if (!require_callback(api != nullptr, error, error_sz, "Value API must not be null")) return false;
    if (!require_callback(api->create_object != nullptr, error, error_sz, "Binary codec requires create_object")) {
        return false;
    }
    if (!require_callback(api->destroy_value != nullptr, error, error_sz, "Binary codec requires destroy_value")) {
        return false;
    }

    std::map<std::string, ParsedDeclaration> decls_by_name;
    for (const auto& decl : decls) {
        decls_by_name.emplace(decl.type_name, decl);
    }

    std::map<std::string, ParsedEnum> enums_by_name;
    for (const auto& enum_decl : enums) {
        enums_by_name.emplace(enum_decl.enum_name, enum_decl);
    }

    for (const auto& decl : decls) {
        if (!validate_fields_api(decl.fields, decl.type_name, decls_by_name, enums_by_name, api, error, error_sz)) {
            return false;
        }
    }
    return true;
}

enum class ValidationFieldKind {
    Primitive,
    VarString,
    ListPrimitive,
    ListString,
    SetPrimitive,
    SetString,
    MapStringPrimitive,
    MapStringString
};

struct ValidationField {
    std::string name;
    ValidationFieldKind kind = ValidationFieldKind::Primitive;
    FieldType value_type = FT_Unknown;
    size_t value_size = 0;
};

static int field_size(FieldType type) {
    switch (type) {
        case FT_Boolean: return 1;
        case FT_Int: return 4;
        case FT_Long: return 8;
        case FT_Float: return 4;
        case FT_Double: return 8;
        default: return 0;
    }
}

static bool flatten_validation_fields(std::vector<ValidationField>& out,
                                      const std::vector<ParsedField>& fields,
                                      const std::map<std::string, ParsedDeclaration>& declarations,
                                      const std::map<std::string, ParsedEnum>& enums,
                                      const std::string& prefix,
                                      std::string* error_message) {
    for (const auto& field : fields) {
        const std::string full_name = prefix.empty() ? field.name : prefix + "." + field.name;

        if (field.type == FT_Object) {
            if (field.type_params.empty()) {
                if (error_message != nullptr) {
                    *error_message = "Schema field '" + full_name + "' is missing object type";
                }
                return false;
            }

            const std::string& custom_type = field.type_params[0].custom_type;
            auto enum_it = enums.find(custom_type);
            if (enum_it != enums.end()) {
                const FieldType underlying_type = parse_field_type(enum_it->second.underlying_type);
                const int size = field_size(underlying_type);
                if (size == 0) {
                    if (error_message != nullptr) {
                        *error_message = "Unsupported enum underlying type for field '" + full_name + "'";
                    }
                    return false;
                }
                out.push_back({full_name, ValidationFieldKind::Primitive, underlying_type,
                               static_cast<size_t>(size)});
                continue;
            }

            auto decl_it = declarations.find(custom_type);
            if (decl_it == declarations.end()) {
                if (error_message != nullptr) {
                    *error_message = "Unknown object type '" + custom_type + "' for field '" + full_name + "'";
                }
                return false;
            }

            if (!flatten_validation_fields(out, decl_it->second.fields, declarations, enums, full_name,
                                           error_message)) {
                return false;
            }
            continue;
        }

        if (field.type == FT_String) {
            out.push_back({full_name, ValidationFieldKind::VarString, FT_String, 0});
            continue;
        }

        if (field.type == FT_List) {
            if (field.type_params.empty()) {
                if (error_message != nullptr) {
                    *error_message = "List field '" + full_name + "' is missing element type";
                }
                return false;
            }

            const TypeParameter& elem = field.type_params[0];
            if (elem.base_type == FT_String) {
                out.push_back({full_name, ValidationFieldKind::ListString, elem.base_type, 0});
                continue;
            }

            const int size = field_size(elem.base_type);
            if (size == 0) {
                if (error_message != nullptr) {
                    *error_message = "Unsupported List element type for field '" + full_name + "'";
                }
                return false;
            }
            out.push_back({full_name, ValidationFieldKind::ListPrimitive, elem.base_type,
                           static_cast<size_t>(size)});
            continue;
        }

        if (field.type == FT_Set) {
            if (field.type_params.empty()) {
                if (error_message != nullptr) {
                    *error_message = "Set field '" + full_name + "' is missing element type";
                }
                return false;
            }

            const TypeParameter& elem = field.type_params[0];
            if (elem.base_type == FT_String) {
                out.push_back({full_name, ValidationFieldKind::SetString, elem.base_type, 0});
                continue;
            }

            const int size = field_size(elem.base_type);
            if (size == 0) {
                if (error_message != nullptr) {
                    *error_message = "Unsupported Set element type for field '" + full_name + "'";
                }
                return false;
            }
            out.push_back({full_name, ValidationFieldKind::SetPrimitive, elem.base_type,
                           static_cast<size_t>(size)});
            continue;
        }

        if (field.type == FT_Map) {
            if (field.type_params.size() < 2) {
                if (error_message != nullptr) {
                    *error_message = "Map field '" + full_name + "' is missing key/value types";
                }
                return false;
            }

            const TypeParameter& key_type = field.type_params[0];
            const TypeParameter& value_type = field.type_params[1];
            if (key_type.base_type != FT_String) {
                if (error_message != nullptr) {
                    *error_message = "Binary codec supports only Map<String, T>; field '" + full_name + "'";
                }
                return false;
            }

            if (value_type.base_type == FT_String) {
                out.push_back({full_name, ValidationFieldKind::MapStringString, value_type.base_type, 0});
                continue;
            }

            const int size = field_size(value_type.base_type);
            if (size == 0) {
                if (error_message != nullptr) {
                    *error_message = "Unsupported Map value type for field '" + full_name + "'";
                }
                return false;
            }
            out.push_back({full_name, ValidationFieldKind::MapStringPrimitive, value_type.base_type,
                           static_cast<size_t>(size)});
            continue;
        }

        const int size = field_size(field.type);
        if (size == 0) {
            if (error_message != nullptr) {
                *error_message = "Unsupported field type for '" + full_name + "'";
            }
            return false;
        }
        out.push_back({full_name, ValidationFieldKind::Primitive, field.type, static_cast<size_t>(size)});
    }

    return true;
}

static uint16_t read_u16_le(const uint8_t* data, size_t offset) {
    return static_cast<uint16_t>(data[offset])
         | static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1]) << 8);
}

static uint32_t read_u32_le(const uint8_t* data, size_t offset) {
    return static_cast<uint32_t>(data[offset])
         | static_cast<uint32_t>(static_cast<uint32_t>(data[offset + 1]) << 8)
         | static_cast<uint32_t>(static_cast<uint32_t>(data[offset + 2]) << 16)
         | static_cast<uint32_t>(static_cast<uint32_t>(data[offset + 3]) << 24);
}

static bool require_bytes(size_t offset, size_t need, size_t len) {
    return offset <= len && need <= (len - offset);
}

static bool scan_var_string(const uint8_t* data, size_t len, size_t& offset,
                            const std::string& field_name, char* error, size_t error_sz) {
    if (!require_bytes(offset, 2, len)) {
        fmt(error, error_sz, "Truncated binary data while reading string length for '{}'", field_name);
        return false;
    }

    const size_t string_len = read_u16_le(data, offset);
    if (!require_bytes(offset, 2 + string_len, len)) {
        fmt(error, error_sz, "Truncated binary data while reading string payload for '{}'", field_name);
        return false;
    }

    offset += 2 + string_len;
    return true;
}

static bool scan_counted_primitive(size_t item_size, const std::string& field_name,
                                   const uint8_t* data, size_t len, size_t& offset,
                                   char* error, size_t error_sz) {
    if (!require_bytes(offset, 4, len)) {
        fmt(error, error_sz, "Truncated binary data while reading count for '{}'", field_name);
        return false;
    }

    const size_t count = read_u32_le(data, offset);
    offset += 4;

    if (count > ((std::numeric_limits<size_t>::max)() / item_size)) {
        fmt(error, error_sz, "Item count overflow while validating '{}'", field_name);
        return false;
    }

    const size_t required = count * item_size;
    if (!require_bytes(offset, required, len)) {
        fmt(error, error_sz, "Truncated binary data while reading '{}' items", field_name);
        return false;
    }

    offset += required;
    return true;
}

static bool scan_counted_strings(const std::string& field_name,
                                 const uint8_t* data, size_t len, size_t& offset,
                                 char* error, size_t error_sz) {
    if (!require_bytes(offset, 4, len)) {
        fmt(error, error_sz, "Truncated binary data while reading count for '{}'", field_name);
        return false;
    }

    const size_t count = read_u32_le(data, offset);
    offset += 4;

    for (size_t i = 0; i < count; ++i) {
        if (!scan_var_string(data, len, offset, field_name, error, error_sz)) {
            return false;
        }
    }

    return true;
}

static bool scan_map_string_primitive(size_t value_size, const std::string& field_name,
                                      const uint8_t* data, size_t len, size_t& offset,
                                      char* error, size_t error_sz) {
    if (!require_bytes(offset, 4, len)) {
        fmt(error, error_sz, "Truncated binary data while reading count for '{}'", field_name);
        return false;
    }

    const size_t count = read_u32_le(data, offset);
    offset += 4;

    for (size_t i = 0; i < count; ++i) {
        if (!scan_var_string(data, len, offset, field_name, error, error_sz)) {
            return false;
        }
        if (!require_bytes(offset, value_size, len)) {
            fmt(error, error_sz, "Truncated binary data while reading map value for '{}'", field_name);
            return false;
        }
        offset += value_size;
    }

    return true;
}

static bool scan_map_string_string(const std::string& field_name,
                                   const uint8_t* data, size_t len, size_t& offset,
                                   char* error, size_t error_sz) {
    if (!require_bytes(offset, 4, len)) {
        fmt(error, error_sz, "Truncated binary data while reading count for '{}'", field_name);
        return false;
    }

    const size_t count = read_u32_le(data, offset);
    offset += 4;

    for (size_t i = 0; i < count; ++i) {
        if (!scan_var_string(data, len, offset, field_name, error, error_sz)) {
            return false;
        }
        if (!scan_var_string(data, len, offset, field_name, error, error_sz)) {
            return false;
        }
    }

    return true;
}

static bool validate_binary_payload(const std::vector<ParsedDeclaration>& declarations,
                                    const std::vector<ParsedEnum>& enums,
                                    const char* type_name,
                                    const uint8_t* data,
                                    size_t len,
                                    char* error,
                                    size_t error_sz) {
    std::map<std::string, ParsedDeclaration> declarations_by_name;
    for (const auto& decl : declarations) {
        declarations_by_name.emplace(decl.type_name, decl);
    }

    std::map<std::string, ParsedEnum> enums_by_name;
    for (const auto& enum_decl : enums) {
        enums_by_name.emplace(enum_decl.enum_name, enum_decl);
    }

    auto decl_it = declarations_by_name.find(type_name);
    if (decl_it == declarations_by_name.end()) {
        fmt(error, error_sz, "Type not found: {}", type_name);
        return false;
    }

    std::vector<ValidationField> fields;
    std::string flatten_error;
    if (!flatten_validation_fields(fields, decl_it->second.fields, declarations_by_name, enums_by_name, type_name,
                                   &flatten_error)) {
        fmt(error, error_sz, "{}", flatten_error.c_str());
        return false;
    }

    size_t offset = 0;
    for (const auto& field : fields) {
        switch (field.kind) {
            case ValidationFieldKind::Primitive:
                if (!require_bytes(offset, field.value_size, len)) {
                    fmt(error, error_sz, "Truncated binary data while reading '{}'", field.name.c_str());
                    return false;
                }
                offset += field.value_size;
                break;
            case ValidationFieldKind::VarString:
                if (!scan_var_string(data, len, offset, field.name, error, error_sz)) {
                    return false;
                }
                break;
            case ValidationFieldKind::ListPrimitive:
            case ValidationFieldKind::SetPrimitive:
                if (!scan_counted_primitive(field.value_size, field.name, data, len, offset, error, error_sz)) {
                    return false;
                }
                break;
            case ValidationFieldKind::ListString:
            case ValidationFieldKind::SetString:
                if (!scan_counted_strings(field.name, data, len, offset, error, error_sz)) {
                    return false;
                }
                break;
            case ValidationFieldKind::MapStringPrimitive:
                if (!scan_map_string_primitive(field.value_size, field.name, data, len, offset, error, error_sz)) {
                    return false;
                }
                break;
            case ValidationFieldKind::MapStringString:
                if (!scan_map_string_string(field.name, data, len, offset, error, error_sz)) {
                    return false;
                }
                break;
        }
    }

    return true;
}

/* ───── BinaryCodec Implementation ───── */

BinaryCodec::BinaryCodec(const std::vector<ParsedDeclaration>& declarations,
                         const std::vector<ParsedEnum>& enums,
                         const DataBindValueApi* api)
    : ctx_(nullptr), declarations_(declarations), enums_(enums), api_(*api) {

    error_[0] = '\0';

    if (!validate_api_for_fields(declarations_, enums_, api, error_, sizeof(error_)))
        return;

    MIR_module_t module = compile_and_link();
    if (module == nullptr) {
        return;
    }

    MIR_gen_finish(ctx_);
    register_parse_functions(module);
}

BinaryCodec::~BinaryCodec() {
    if (ctx_) MIR_finish(ctx_);
}

Value* BinaryCodec::parse(const char* type_name, const uint8_t* data, size_t len) {
    error_[0] = '\0';

    if (!type_name || !data) {
        fmt(error_, sizeof(error_), "Invalid arguments");
        return nullptr;
    }

    auto it = func_map_.find(type_name);
    if (it == func_map_.end()) {
        fmt(error_, sizeof(error_), "Type not found: {}", type_name);
        return nullptr;
    }

    if (!validate_binary_payload(declarations_, enums_, type_name, data, len, error_, sizeof(error_))) {
        return nullptr;
    }

    auto parse_fn = (Value* (*)(const uint8_t*, size_t))it->second;
    Value* value = parse_fn(data, len);
    if (!value) {
        fmt(error_, sizeof(error_), "Binary parse failed for type '{}'", type_name);
        return nullptr;
    }

    error_[0] = '\0';
    return value;
}

const char* BinaryCodec::get_error() const {
    return error_[0] ? error_ : "Unknown error";
}

MIR_module_t BinaryCodec::compile_and_link() {
    ctx_ = MIR_init();
    std::string generation_error;
    BinaryAbiRequirements requirements;
    if (!collect_binary_abi_requirements(declarations_, enums_, requirements, &generation_error)) {
        fmt(error_, sizeof(error_), "{}",
                 generation_error.empty() ? "Failed to collect binary ABI requirements" : generation_error.c_str());
        MIR_finish(ctx_);
        ctx_ = nullptr;
        return nullptr;
    }

    MIR_module_t module
        = mir_generate_parsers(ctx_, declarations_, enums_, &generation_error);
    if (module == nullptr) {
        fmt(error_, sizeof(error_), "{}",
                 generation_error.empty() ? "Failed to generate parser module" : generation_error.c_str());
        MIR_finish(ctx_);
        ctx_ = nullptr;
        return nullptr;
    }

    MIR_load_module(ctx_, module);
    auto load_external = [this](const char* name, auto fn) {
        MIR_load_external(ctx_, name, reinterpret_cast<void*>(fn));
    };

    load_external("create_obj", api_.create_object);
    load_external("destroy_value", api_.destroy_value);
    if (requirements.set_int)
        load_external("set_int", api_.set_field_int);
    if (requirements.set_int64)
        load_external("set_int64", api_.set_field_int64);
    if (requirements.set_dbl)
        load_external("set_dbl", api_.set_field_double);
    if (requirements.set_str)
        load_external("set_str", api_.set_field_string);
    if (requirements.read_varstr)
        load_external("read_varstr", tbe_read_varstring);
    if (requirements.free_string)
        load_external("free", free);

    if (requirements.create_list)
        load_external("create_list", api_.create_list);
    if (requirements.add_list_int)
        load_external("add_list_int", api_.add_list_item_int);
    if (requirements.add_list_int64)
        load_external("add_list_int64", api_.add_list_item_int64);
    if (requirements.add_list_dbl)
        load_external("add_list_dbl", api_.add_list_item_double);
    if (requirements.add_list_str)
        load_external("add_list_str", api_.add_list_item_string);
    if (requirements.set_list)
        load_external("set_list", api_.set_field_list);

    if (requirements.create_set)
        load_external("create_set", api_.create_set);
    if (requirements.add_set_int)
        load_external("add_set_int", api_.add_set_item_int);
    if (requirements.add_set_int64)
        load_external("add_set_int64", api_.add_set_item_int64);
    if (requirements.add_set_dbl)
        load_external("add_set_dbl", api_.add_set_item_double);
    if (requirements.add_set_str)
        load_external("add_set_str", api_.add_set_item_string);
    if (requirements.set_set)
        load_external("set_set", api_.set_field_set);

    if (requirements.create_map)
        load_external("create_map", api_.create_map);
    if (requirements.add_map_str_str)
        load_external("add_map_str_str", api_.add_map_entry_string_string);
    if (requirements.add_map_str_int)
        load_external("add_map_str_int", api_.add_map_entry_string_int);
    if (requirements.add_map_str_int64)
        load_external("add_map_str_int64", api_.add_map_entry_string_int64);
    if (requirements.add_map_str_dbl)
        load_external("add_map_str_dbl", api_.add_map_entry_string_double);
    if (requirements.set_map)
        load_external("set_map", api_.set_field_map);

    MIR_gen_init(ctx_);
    MIR_link(ctx_, MIR_set_gen_interface, nullptr);
    return module;
}

void BinaryCodec::register_parse_functions(MIR_module_t module) {
    for (const auto& decl : declarations_) {
        const char* msg_name = decl.type_name.c_str();
        char func_name[256];
        fmt(func_name, sizeof(func_name), "parse_{}", msg_name);

        for (MIR_item_t item = DLIST_HEAD(MIR_item_t, module->items); item;
             item = DLIST_NEXT(MIR_item_t, item)) {
            if (item->item_type == MIR_func_item &&
                strcmp(item->u.func->name, func_name) == 0) {
                func_map_[msg_name] = item->addr;
                break;
            }
        }
    }
}
