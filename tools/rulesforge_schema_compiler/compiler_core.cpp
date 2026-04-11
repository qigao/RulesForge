#include "compiler_core.hpp"

#include "rfl_parser_impl.hpp"
#include "template_model.hpp"
#include "core/constraint_types.hpp"
#include "data_bind.h"
#include "mustache.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <set>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace rulesforge_compiler {
namespace {

struct DummyValue {
    int unused;
};

Value* create_dummy_container() {
    static DummyValue dummy{};
    return reinterpret_cast<Value*>(&dummy);
}

void noop_set_int(Value*, char const*, int32_t) {}
void noop_set_int64(Value*, char const*, int64_t) {}
void noop_set_double(Value*, char const*, double) {}
void noop_set_string(Value*, char const*, char const*) {}
void noop_set_bytes(Value*, char const*, uint8_t const*, size_t) {}
void noop_add_list_int(Value*, int32_t) {}
void noop_add_list_int64(Value*, int64_t) {}
void noop_add_list_double(Value*, double) {}
void noop_add_list_string(Value*, char const*) {}
void noop_add_list_object(Value*, Value*) {}
void noop_set_field_list(Value*, char const*, Value*) {}
void noop_set_field_object(Value*, char const*, Value*) {}
void noop_add_set_int(Value*, int32_t) {}
void noop_add_set_int64(Value*, int64_t) {}
void noop_add_set_double(Value*, double) {}
void noop_add_set_string(Value*, char const*) {}
void noop_set_field_set(Value*, char const*, Value*) {}
void noop_add_map_string_string(Value*, char const*, char const*) {}
void noop_add_map_string_int(Value*, char const*, int32_t) {}
void noop_add_map_string_int64(Value*, char const*, int64_t) {}
void noop_add_map_string_double(Value*, char const*, double) {}
void noop_set_field_map(Value*, char const*, Value*) {}
void noop_destroy_value(Value*) {}

DataBindValueApi full_dummy_api() {
    DataBindValueApi api{};
    api.create_object = &create_dummy_container;
    api.set_field_int = &noop_set_int;
    api.set_field_int64 = &noop_set_int64;
    api.set_field_double = &noop_set_double;
    api.set_field_string = &noop_set_string;
    api.set_field_bytes = &noop_set_bytes;
    api.create_list = &create_dummy_container;
    api.add_list_item_int = &noop_add_list_int;
    api.add_list_item_int64 = &noop_add_list_int64;
    api.add_list_item_double = &noop_add_list_double;
    api.add_list_item_string = &noop_add_list_string;
    api.add_list_item_object = &noop_add_list_object;
    api.set_field_list = &noop_set_field_list;
    api.set_field_object = &noop_set_field_object;
    api.create_set = &create_dummy_container;
    api.add_set_item_int = &noop_add_set_int;
    api.add_set_item_int64 = &noop_add_set_int64;
    api.add_set_item_double = &noop_add_set_double;
    api.add_set_item_string = &noop_add_set_string;
    api.set_field_set = &noop_set_field_set;
    api.create_map = &create_dummy_container;
    api.add_map_entry_string_string = &noop_add_map_string_string;
    api.add_map_entry_string_int = &noop_add_map_string_int;
    api.add_map_entry_string_int64 = &noop_add_map_string_int64;
    api.add_map_entry_string_double = &noop_add_map_string_double;
    api.set_field_map = &noop_set_field_map;
    api.destroy_value = &noop_destroy_value;
    return api;
}

char const* scalar_type_name(FieldType type) {
    switch (type) {
        case FT_String: return "String";
        case FT_Int: return "int";
        case FT_Long: return "long";
        case FT_Double: return "double";
        case FT_Float: return "float";
        case FT_Boolean: return "boolean";
        case FT_Number: return "Number";
        case FT_Object: return "Object";
        default: return "Unknown";
    }
}

std::string render_type_parameter(TypeParameter const& param) {
    if (param.base_type == FT_Object) {
        if (!param.custom_type.empty()) return param.custom_type;
        return "Object";
    }

    std::string base = scalar_type_name(param.base_type);
    if (!param.nested) return base;
    return base + "<" + render_type_parameter(*param.nested) + ">";
}

std::string package_leaf(std::string const& package_name) {
    size_t pos = package_name.find_last_of('.');
    return pos == std::string::npos ? package_name : package_name.substr(pos + 1);
}

std::string sanitize_identifier(std::string value) {
    if (value.empty()) return "generated";
    for (char& c : value) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) c = '_';
    }
    if (!(std::isalpha(static_cast<unsigned char>(value[0])) || value[0] == '_')) {
        value.insert(value.begin(), '_');
    }
    return value;
}

std::string upper_identifier(std::string value) {
    value = sanitize_identifier(std::move(value));
    for (char& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return value;
}

std::string c_type_name(FieldType base_type, std::string const& custom_type);
std::string cpp_type_name(FieldType base_type, std::string const& custom_type);
std::string go_type_name(FieldType base_type, std::string const& custom_type);
std::string ts_type_name(FieldType base_type, std::string const& custom_type);
std::string py_type_name(FieldType base_type, std::string const& custom_type);

std::string c_type_from_param(TypeParameter const& param) {
    if (param.base_type == FT_Object) return c_type_name(FT_Object, param.custom_type);
    if (param.nested) return c_type_from_param(*param.nested);
    return c_type_name(param.base_type, param.custom_type);
}

std::string go_type_from_param(TypeParameter const& param) {
    if (param.base_type == FT_Object) return go_type_name(FT_Object, param.custom_type);
    if (param.nested) return go_type_from_param(*param.nested);
    return go_type_name(param.base_type, param.custom_type);
}

std::string cpp_type_from_param(TypeParameter const& param) {
    if (param.base_type == FT_Object) return cpp_type_name(FT_Object, param.custom_type);
    if (param.nested) return cpp_type_from_param(*param.nested);
    return cpp_type_name(param.base_type, param.custom_type);
}

std::string ts_type_from_param(TypeParameter const& param) {
    if (param.base_type == FT_Object) return ts_type_name(FT_Object, param.custom_type);
    if (param.nested) return ts_type_from_param(*param.nested);
    return ts_type_name(param.base_type, param.custom_type);
}

std::string py_type_from_param(TypeParameter const& param) {
    if (param.base_type == FT_Object) return py_type_name(FT_Object, param.custom_type);
    if (param.nested) return py_type_from_param(*param.nested);
    return py_type_name(param.base_type, param.custom_type);
}

std::string c_type_name(FieldType base_type, std::string const& custom_type) {
    switch (base_type) {
        case FT_String: return "char*";
        case FT_Int:
        case FT_Boolean: return "int32_t";
        case FT_Long: return "int64_t";
        case FT_Float: return "float";
        case FT_Double: return "double";
        case FT_Object: return sanitize_identifier(custom_type);
        default: return "void*";
    }
}

std::string go_type_name(FieldType base_type, std::string const& custom_type) {
    switch (base_type) {
        case FT_String: return "string";
        case FT_Int: return "int32";
        case FT_Boolean: return "bool";
        case FT_Long: return "int64";
        case FT_Float: return "float32";
        case FT_Double: return "float64";
        case FT_Object: return sanitize_identifier(custom_type);
        default: return "any";
    }
}

std::string cpp_type_name(FieldType base_type, std::string const& custom_type) {
    switch (base_type) {
        case FT_String: return "std::string";
        case FT_Int: return "std::int32_t";
        case FT_Boolean: return "bool";
        case FT_Long: return "std::int64_t";
        case FT_Float: return "float";
        case FT_Double: return "double";
        case FT_Object: return sanitize_identifier(custom_type);
        default: return "std::any";
    }
}

std::string ts_type_name(FieldType base_type, std::string const& custom_type) {
    switch (base_type) {
        case FT_String: return "string";
        case FT_Int:
        case FT_Long:
        case FT_Float:
        case FT_Double: return "number";
        case FT_Boolean: return "boolean";
        case FT_Object: return sanitize_identifier(custom_type);
        default: return "unknown";
    }
}

std::string py_type_name(FieldType base_type, std::string const& custom_type) {
    switch (base_type) {
        case FT_String: return "str";
        case FT_Int: return "int";
        case FT_Long: return "int";
        case FT_Float:
        case FT_Double: return "float";
        case FT_Boolean: return "bool";
        case FT_Object: return sanitize_identifier(custom_type);
        default: return "Any";
    }
}

std::string c_field_type(ParsedField const& field) {
    switch (field.type) {
        case FT_List:
        case FT_Set:
        case FT_Map: return "void*";
        case FT_Object:
            if (!field.type_params.empty()) return c_type_from_param(field.type_params[0]);
            return "void*";
        default: return c_type_name(field.type, "");
    }
}

std::string go_field_type(ParsedField const& field) {
    switch (field.type) {
        case FT_List:
            return field.type_params.empty() ? "[]any" : "[]" + go_type_from_param(field.type_params[0]);
        case FT_Set:
            return field.type_params.empty() ? "map[any]struct{}"
                                             : "map[" + go_type_from_param(field.type_params[0]) + "]struct{}";
        case FT_Map:
            if (field.type_params.size() < 2) return "map[string]any";
            return "map[" + go_type_from_param(field.type_params[0]) + "]"
                   + go_type_from_param(field.type_params[1]);
        case FT_Object:
            if (!field.type_params.empty()) return go_type_from_param(field.type_params[0]);
            return "any";
        default:
            return go_type_name(field.type, "");
    }
}

std::string cpp_field_type(ParsedField const& field) {
    switch (field.type) {
        case FT_List:
            return field.type_params.empty() ? "std::vector<std::any>"
                                             : "std::vector<" + cpp_type_from_param(field.type_params[0]) + ">";
        case FT_Set:
            return field.type_params.empty() ? "std::set<std::any>"
                                             : "std::set<" + cpp_type_from_param(field.type_params[0]) + ">";
        case FT_Map:
            if (field.type_params.size() < 2) return "std::map<std::string, std::any>";
            return "std::map<" + cpp_type_from_param(field.type_params[0]) + ", "
                   + cpp_type_from_param(field.type_params[1]) + ">";
        case FT_Object:
            if (!field.type_params.empty()) return cpp_type_from_param(field.type_params[0]);
            return "std::any";
        default:
            return cpp_type_name(field.type, "");
    }
}

std::string ts_field_type(ParsedField const& field) {
    switch (field.type) {
        case FT_List:
            return field.type_params.empty() ? "unknown[]" : ts_type_from_param(field.type_params[0]) + "[]";
        case FT_Set:
            return field.type_params.empty() ? "Set<unknown>" : "Set<" + ts_type_from_param(field.type_params[0]) + ">";
        case FT_Map:
            if (field.type_params.size() < 2) return "Map<string, unknown>";
            return "Map<" + ts_type_from_param(field.type_params[0]) + ", "
                   + ts_type_from_param(field.type_params[1]) + ">";
        case FT_Object:
            if (!field.type_params.empty()) return ts_type_from_param(field.type_params[0]);
            return "unknown";
        default:
            return ts_type_name(field.type, "");
    }
}

std::string py_field_type(ParsedField const& field) {
    switch (field.type) {
        case FT_List:
            return field.type_params.empty() ? "list[Any]" : "list[" + py_type_from_param(field.type_params[0]) + "]";
        case FT_Set:
            return field.type_params.empty() ? "set[Any]" : "set[" + py_type_from_param(field.type_params[0]) + "]";
        case FT_Map:
            if (field.type_params.size() < 2) return "dict[str, Any]";
            return "dict[" + py_type_from_param(field.type_params[0]) + ", "
                   + py_type_from_param(field.type_params[1]) + "]";
        case FT_Object:
            if (!field.type_params.empty()) return py_type_from_param(field.type_params[0]);
            return "Any";
        default:
            return py_type_name(field.type, "");
    }
}

std::string render_field_type(ParsedField const& field) {
    switch (field.type) {
        case FT_List:
            if (!field.type_params.empty()) {
                return "List<" + render_type_parameter(field.type_params[0]) + ">";
            }
            return "List";
        case FT_Set:
            if (!field.type_params.empty()) {
                return "Set<" + render_type_parameter(field.type_params[0]) + ">";
            }
            return "Set";
        case FT_Map:
            if (field.type_params.size() >= 2) {
                return "Map<" + render_type_parameter(field.type_params[0]) + ", "
                       + render_type_parameter(field.type_params[1]) + ">";
            }
            return "Map";
        case FT_Object:
            if (!field.type_params.empty() && !field.type_params[0].custom_type.empty()) {
                return field.type_params[0].custom_type;
            }
            return "Object";
        default:
            return scalar_type_name(field.type);
    }
}

bool is_identifier_value(std::string const& value) {
    if (value.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(value[0])) || value[0] == '_')) return false;
    for (char c : value) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')) return false;
    }
    return true;
}

std::string read_text_file(std::string const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::unique_ptr<TemplateNode> build_template_model(parser_state const& state) {
    auto root = std::make_unique<TemplateNode>(TemplateNode::Type::Map);

    if (!state.package_name.empty()) {
        add_string(*root, "package", state.package_name);
        add_string(*root, "has_package", "1");
        add_string(*root, "package_leaf", sanitize_identifier(package_leaf(state.package_name)));
        add_string(*root, "header_guard", upper_identifier(state.package_name) + "_GENERATED_H");
    } else {
        add_string(*root, "package_leaf", "generated");
        add_string(*root, "header_guard", "GENERATED_TYPES_H");
    }

    TemplateNode* enums = add_list(*root, "enums");
    for (auto const& e : state.parsed_enums) {
        TemplateNode* enum_node = list_append_map(*enums);
        add_string(*enum_node, "name", e.enum_name);
        add_string(*enum_node, "underlying_type", e.underlying_type);
        TemplateNode* values = add_list(*enum_node, "values");
        for (auto const& value : e.values) {
            TemplateNode* value_node = list_append_map(*values);
            add_string(*value_node, "name", value);
            add_string(*value_node, "c_name", upper_identifier(e.enum_name + "_" + value));
            add_string(*value_node, "enum_name", e.enum_name);
            add_string(*value_node, "go_name", sanitize_identifier(e.enum_name + "_" + value));
        }
    }

    TemplateNode* declarations = add_list(*root, "declarations");
    for (auto const& decl : state.parsed_declarations) {
        TemplateNode* decl_node = list_append_map(*declarations);
        add_string(*decl_node, "name", decl.type_name);
        add_string(*decl_node, "go_name", sanitize_identifier(decl.type_name));
        add_string(*decl_node, "ts_name", sanitize_identifier(decl.type_name));
        add_string(*decl_node, "py_name", sanitize_identifier(decl.type_name));
        add_string(*decl_node, "c_name", sanitize_identifier(decl.type_name));

        TemplateNode* annotations = add_list(*decl_node, "annotations");
        for (auto const& [name, value] : decl.annotations) {
            TemplateNode* ann = list_append_map(*annotations);
            add_string(*ann, "name", name);
            add_string(*ann, "value", value);
            if (value == "true") {
                add_string(*ann, "is_flag", "1");
            } else if (is_identifier_value(value)) {
                add_string(*ann, "is_identifier", "1");
            } else {
                add_string(*ann, "is_string", "1");
            }
        }

        TemplateNode* fields = add_list(*decl_node, "fields");
        for (auto const& field : decl.fields) {
            TemplateNode* field_node = list_append_map(*fields);
            add_string(*field_node, "name", field.name);
            add_string(*field_node, "type", render_field_type(field));
            add_string(*field_node, "c_type", c_field_type(field));
            add_string(*field_node, "cpp_type", cpp_field_type(field));
            add_string(*field_node, "go_type", go_field_type(field));
            add_string(*field_node, "ts_type", ts_field_type(field));
            add_string(*field_node, "py_type", py_field_type(field));
        }
    }

    return root;
}

void append_annotations(std::ostringstream& out,
                        std::map<std::string, std::string> const& annotations) {
    for (auto const& [name, value] : annotations) {
        if (value == "true") {
            out << "@" << name << "\n";
        } else if (is_identifier_value(value)) {
            out << "@" << name << "(" << value << ")\n";
        } else {
            out << "@" << name << "(\"" << value << "\")\n";
        }
    }
}

bool validate_one_format(parser_state const& state,
                         DataBindFormat format,
                         std::string const& format_name,
                         std::string& out_error) {
    std::vector<void const*> declarations;
    std::vector<void const*> enums;
    declarations.reserve(state.parsed_declarations.size());
    enums.reserve(state.parsed_enums.size());

    for (auto const& decl : state.parsed_declarations) { declarations.push_back(&decl); }
    for (auto const& e : state.parsed_enums) { enums.push_back(&e); }

    DataBindValueApi api = full_dummy_api();
    DataBind* codec = data_bind_create_from_declarations_and_enums(
        declarations.data(),
        declarations.size(),
        enums.empty() ? nullptr : enums.data(),
        enums.size(),
        format,
        &api);

    if (!codec) {
        out_error = format_name + " codec creation failed: " + std::string(data_bind_get_error(nullptr));
        return false;
    }

    data_bind_free(codec);
    return true;
}

bool precheck_field_support(ParsedField const& field,
                            std::string const& full_name,
                            DataBindFormat format,
                            std::map<std::string, ParsedDeclaration const*> const& decls_by_name,
                            std::map<std::string, ParsedEnum const*> const& enums_by_name,
                            std::string& out_reason);

bool precheck_fields_support(std::vector<ParsedField> const& fields,
                             std::string const& prefix,
                             DataBindFormat format,
                             std::map<std::string, ParsedDeclaration const*> const& decls_by_name,
                             std::map<std::string, ParsedEnum const*> const& enums_by_name,
                             std::string& out_reason) {
    for (auto const& field : fields) {
        std::string full_name = prefix.empty() ? field.name : prefix + "." + field.name;
        if (!precheck_field_support(field, full_name, format, decls_by_name, enums_by_name, out_reason)) {
            return false;
        }
    }
    return true;
}

bool precheck_field_support(ParsedField const& field,
                            std::string const& full_name,
                            DataBindFormat format,
                            std::map<std::string, ParsedDeclaration const*> const& decls_by_name,
                            std::map<std::string, ParsedEnum const*> const& enums_by_name,
                            std::string& out_reason) {
    auto unsupported = [&](std::string const& reason) {
        out_reason = "field '" + full_name + "': " + reason;
        return false;
    };

    switch (field.type) {
        case FT_Boolean:
        case FT_Int:
        case FT_Long:
        case FT_Float:
        case FT_Double:
        case FT_String:
            return true;

        case FT_Object: {
            if (field.type_params.empty()) return unsupported("missing object type");
            std::string const& custom = field.type_params[0].custom_type;
            if (auto enum_it = enums_by_name.find(custom); enum_it != enums_by_name.end()) {
                ParsedField enum_field{field.name, parse_field_type(enum_it->second->underlying_type), {}};
                return precheck_field_support(enum_field, full_name, format, decls_by_name, enums_by_name, out_reason);
            }
            auto decl_it = decls_by_name.find(custom);
            if (decl_it == decls_by_name.end()) return unsupported("unknown object type '" + custom + "'");
            return precheck_fields_support(decl_it->second->fields, full_name, format, decls_by_name, enums_by_name,
                                           out_reason);
        }

        case FT_List:
            if (field.type_params.empty()) return unsupported("List requires an element type");
            switch (field.type_params[0].base_type) {
                case FT_String:
                case FT_Long:
                case FT_Float:
                case FT_Double:
                case FT_Boolean:
                case FT_Int:
                    return true;
                case FT_Object:
                    if (format == DATA_BIND_FORMAT_JSON) return true;
                    return unsupported("binary codec does not support List<Object>");
                default:
                    return unsupported("unsupported List element type");
            }

        case FT_Set:
            if (field.type_params.empty()) return unsupported("Set requires an element type");
            switch (field.type_params[0].base_type) {
                case FT_String:
                case FT_Float:
                case FT_Double:
                case FT_Boolean:
                case FT_Int:
                case FT_Long:
                    return true;
                default:
                    return unsupported("unsupported Set element type");
            }

        case FT_Map:
            if (field.type_params.size() < 2) return unsupported("Map requires key and value types");
            if (field.type_params[0].base_type != FT_String) {
                return unsupported(format == DATA_BIND_FORMAT_BINARY
                                       ? "binary codec only handles Map<String, T>"
                                       : "json codec only handles Map<String, T>");
            }
            switch (field.type_params[1].base_type) {
                case FT_String:
                case FT_Float:
                case FT_Double:
                case FT_Boolean:
                case FT_Int:
                case FT_Long:
                    return true;
                default:
                    return unsupported("unsupported Map value type");
            }

        default:
            return unsupported(format == DATA_BIND_FORMAT_BINARY ? "unsupported binary field type"
                                                                 : "unsupported json field type");
    }
}

bool precheck_format_support(parser_state const& state,
                             DataBindFormat format,
                             std::string const& format_name,
                             std::string& out_error) {
    std::map<std::string, ParsedDeclaration const*> decls_by_name;
    std::map<std::string, ParsedEnum const*> enums_by_name;
    for (auto const& decl : state.parsed_declarations) decls_by_name.emplace(decl.type_name, &decl);
    for (auto const& e : state.parsed_enums) enums_by_name.emplace(e.enum_name, &e);

    for (auto const& decl : state.parsed_declarations) {
        std::string reason;
        if (!precheck_fields_support(decl.fields, decl.type_name, format, decls_by_name, enums_by_name, reason)) {
            out_error = format_name + " validation failed for declaration '" + decl.type_name + "': " + reason;
            return false;
        }
    }
    return true;
}

void collect_object_dependencies(ParsedDeclaration const& decl,
                                 std::map<std::string, ParsedDeclaration const*> const& by_name,
                                 std::set<std::string>& needed) {
    if (!needed.insert(decl.type_name).second) return;

    for (auto const& field : decl.fields) {
        auto const collect_param = [&](auto const& self, TypeParameter const& param) -> void {
            if (param.base_type == FT_Object && !param.custom_type.empty()) {
                auto it = by_name.find(param.custom_type);
                if (it != by_name.end()) collect_object_dependencies(*it->second, by_name, needed);
            }
            if (param.nested) self(self, *param.nested);
        };

        if (field.type == FT_Object && !field.type_params.empty() && !field.type_params[0].custom_type.empty()) {
            auto it = by_name.find(field.type_params[0].custom_type);
            if (it != by_name.end()) collect_object_dependencies(*it->second, by_name, needed);
        }

        for (auto const& param : field.type_params) {
            collect_param(collect_param, param);
        }
    }
}

parser_state build_declaration_closure(parser_state const& state,
                                       ParsedDeclaration const& root_decl) {
    parser_state subset;
    subset.package_name = state.package_name;
    subset.parsed_enums = state.parsed_enums;

    std::map<std::string, ParsedDeclaration const*> by_name;
    for (auto const& decl : state.parsed_declarations) {
        by_name.emplace(decl.type_name, &decl);
    }

    std::set<std::string> needed;
    collect_object_dependencies(root_decl, by_name, needed);

    for (auto const& decl : state.parsed_declarations) {
        if (needed.count(decl.type_name)) subset.parsed_declarations.push_back(decl);
    }

    return subset;
}

bool validate_single_format(parser_state const& state,
                            DataBindFormat format,
                            std::string const& format_name,
                            std::string& out_error) {
    if (!precheck_format_support(state, format, format_name, out_error)) return false;
    if (validate_one_format(state, format, format_name, out_error)) return true;

    std::string aggregate_error = out_error;
    for (auto const& decl : state.parsed_declarations) {
        parser_state subset = build_declaration_closure(state, decl);
        std::string local_error;
        if (!validate_one_format(subset, format, format_name, local_error)) {
            out_error = format_name + " validation failed for declaration '" + decl.type_name
                        + "': " + local_error;
            return false;
        }
    }

    out_error = aggregate_error;
    return false;
}

} // namespace

std::string builtin_template_name(OutputLanguage language) {
    switch (language) {
        case OutputLanguage::C: return "c_header.mustache";
        case OutputLanguage::Cpp: return "cpp_types.mustache";
        case OutputLanguage::Go: return "go_types.mustache";
        case OutputLanguage::TypeScript: return "ts_types.mustache";
        case OutputLanguage::Python: return "python_types.mustache";
    }
    return {};
}

bool compile_source(std::string const& source,
                    std::string const& source_name,
                    ParsingResult& result,
                    parser_state& out_state) {
    result = {};
    out_state = rfl_parse_lemon(source, source_name, result.errors);
    result.success = result.errors.empty();
    return result.success;
}

bool compile_file(std::string const& file_path,
                  ParsingResult& result,
                  parser_state& out_state) {
    std::ifstream in(file_path, std::ios::binary);
    if (!in) {
        result.success = false;
        result.errors.push_back({.file_name = file_path, .message = "Failed to read input file"});
        return false;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return compile_source(buffer.str(), file_path, result, out_state);
}

std::string emit_declarations_only(parser_state const& state) {
    std::ostringstream out;

    if (!state.package_name.empty()) {
        out << "package " << state.package_name << "\n\n";
    }

    for (auto const& e : state.parsed_enums) {
        out << "enum " << e.enum_name;
        if (!e.underlying_type.empty() && e.underlying_type != "int") {
            out << "<" << e.underlying_type << ">";
        }
        out << "\n";
        for (size_t i = 0; i < e.values.size(); ++i) {
            out << "    " << e.values[i];
            if (i + 1 < e.values.size()) out << ",";
            out << "\n";
        }
        out << "end\n\n";
    }

    for (auto const& decl : state.parsed_declarations) {
        append_annotations(out, decl.annotations);
        out << "declare " << decl.type_name << "\n";
        for (auto const& field : decl.fields) {
            out << "    " << field.name << ": " << render_field_type(field) << "\n";
        }
        out << "end\n\n";
    }

    return out.str();
}

bool render_with_template_file(parser_state const& state,
                               std::string const& template_path,
                               std::string& out_text,
                               std::string& out_error) {
    std::string templ_text = read_text_file(template_path);
    if (templ_text.empty()) {
        out_error = "failed to read template file: " + template_path;
        return false;
    }

    MUSTACHE_TEMPLATE* templ = mustache_compile(templ_text.c_str(), templ_text.size(), nullptr, nullptr, 0);
    if (!templ) {
        out_error = "failed to compile template file: " + template_path;
        return false;
    }

    auto model = build_template_model(state);
    MUSTACHE_DATAPROVIDER provider = template_provider();
    MUSTACHE_STRING_RENDERER renderer{};

    if (mustache_string_renderer_init(&renderer) != 0) {
        mustache_release(templ);
        out_error = "failed to initialize template renderer";
        return false;
    }

    int rc = mustache_process(templ, &renderer.base, &renderer, &provider, model.get());
    if (rc != MUSTACHE_ERR_SUCCESS) {
        mustache_string_renderer_free(&renderer);
        mustache_release(templ);
        out_error = "failed to render template file: " + template_path;
        return false;
    }

    char* rendered = mustache_string_renderer_get(&renderer);
    if (!rendered) {
        mustache_string_renderer_free(&renderer);
        mustache_release(templ);
        out_error = "failed to fetch rendered template output";
        return false;
    }

    out_text = rendered;
    free(rendered);
    mustache_string_renderer_free(&renderer);
    mustache_release(templ);
    return true;
}

bool validate_databind_support(parser_state const& state,
                               ValidationMode mode,
                               std::string& out_error) {
    if ((mode == ValidationMode::Both || mode == ValidationMode::JsonOnly)
        && !validate_single_format(state, DATA_BIND_FORMAT_JSON, "json", out_error)) {
        return false;
    }
    if ((mode == ValidationMode::Both || mode == ValidationMode::BinaryOnly)
        && !validate_single_format(state, DATA_BIND_FORMAT_BINARY, "binary", out_error)) {
        return false;
    }
    return true;
}

} // namespace rulesforge_compiler
