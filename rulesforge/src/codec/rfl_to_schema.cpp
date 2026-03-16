#include "codec/rfl_to_schema.hpp"
#include <sstream>

std::string RflToSchemaConverter::map_field_type(FieldType type) {
    switch (type) {
        case FT_String:  return "string";
        case FT_Int:     return "int32";
        case FT_Long:    return "int64";
        case FT_Double:  return "double";
        case FT_Float:   return "float";
        case FT_Boolean: return "bool";
        default:         return "bytes"; // Fallback for complex types
    }
}

std::string RflToSchemaConverter::convert_declaration(ParsedDeclaration const& decl) {
    std::ostringstream oss;

    oss << "message " << decl.type_name << " {\n";

    for (auto const& field : decl.fields) {
        oss << "    " << map_field_type(field.type) << " " << field.name << ";\n";
    }

    oss << "}\n";

    return oss.str();
}

std::string RflToSchemaConverter::convert(std::vector<ParsedDeclaration> const& declarations) {
    std::ostringstream oss;

    // Schema header
    oss << "schema RulesForge [id(1), version(1), byte_order(little)];\n\n";

    // Convert each declaration
    for (auto const& decl : declarations) {
        oss << convert_declaration(decl);
        oss << "\n";
    }

    return oss.str();
}
