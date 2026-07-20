#include "databind_schema_importer.hpp"

#include "data_bind.h"

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>

namespace rulesforge {
namespace {

std::string copy_string(char const* text) {
  return text != nullptr ? std::string(text) : std::string();
}

std::string data_bind_error_message(DataBindStatus status, DataBindError const& error)
{
  if (error.message[0] != '\0') {
    return std::string(error.message);
  }
  char const* status_name = data_bind_status_name(status);
  return status_name != nullptr ? std::string(status_name) : std::string("unknown DataBind error");
}

FieldType schema_type_to_field_type(std::string_view type)
{
  if (type == "string") {
    return FT_String;
  }
  if (type == "bool" || type == "boolean") {
    return FT_Boolean;
  }
  if (type == "float") {
    return FT_Float;
  }
  if (type == "double") {
    return FT_Double;
  }
  if (type == "uuid") {
    return FT_Uuid;
  }
  if (type == "datetime" || type == "date" || type == "time" || type == "decimal"
      || type == "bigint" || type == "money") {
    return FT_String;
  }
  if (type == "duration") {
    return FT_Long;
  }
  if (type == "int64" || type == "int64_t" || type == "uint64" || type == "uint64_t") {
    return FT_Long;
  }
  if (type == "byte" || type == "int8" || type == "int8_t" || type == "uint8" || type == "uint8_t"
      || type == "int16" || type == "int16_t" || type == "uint16" || type == "uint16_t"
      || type == "int32" || type == "int32_t" || type == "uint32" || type == "uint32_t") {
    return FT_Int;
  }
  if (type == "bytes" || type == "array" || type == "list") {
    return FT_List;
  }
  if (type == "set") {
    return FT_Set;
  }
  if (type == "map") {
    return FT_Map;
  }
  return parse_field_type(type);
}

TypeParameter make_type_parameter(std::string const& type_name)
{
  FieldType field_type = schema_type_to_field_type(type_name);
  if (field_type == FT_Unknown || field_type == FT_Object) {
    return TypeParameter(FT_Object, type_name);
  }
  return TypeParameter(field_type);
}

TypeParameter make_type_parameter(std::string const& type_name,
                                  std::map<std::string, std::string> const& enum_underlying_types)
{
  auto enum_it = enum_underlying_types.find(type_name);
  if (enum_it != enum_underlying_types.end()) {
    return make_type_parameter(enum_it->second);
  }
  return make_type_parameter(type_name);
}

ParsedField convert_field(DataBindSchemaField const& field,
                          std::map<std::string, std::string> const& enum_underlying_types)
{
  ParsedField out;
  out.name = copy_string(field.name);

  std::string type = copy_string(field.type);
  std::string collection_kind = copy_string(field.collection_kind);
  if (collection_kind.empty() && type == "array") {
    collection_kind = "list";
  }
  if (field.is_group) {
    collection_kind = "list";
  }

  if (collection_kind == "list" || collection_kind == "array") {
    out.type = FT_List;
    std::string inner_type = copy_string(field.inner_type);
    if (!inner_type.empty()) {
      out.type_params.push_back(make_type_parameter(inner_type, enum_underlying_types));
    }
    return out;
  }
  if (collection_kind == "set") {
    out.type = FT_Set;
    std::string inner_type = copy_string(field.inner_type);
    if (!inner_type.empty()) {
      out.type_params.push_back(make_type_parameter(inner_type, enum_underlying_types));
    }
    return out;
  }
  if (collection_kind == "map") {
    out.type = FT_Map;
    std::string key_type = copy_string(field.key_type);
    std::string value_type = copy_string(field.value_type);
    if (!key_type.empty()) {
      out.type_params.push_back(make_type_parameter(key_type, enum_underlying_types));
    }
    if (!value_type.empty()) {
      out.type_params.push_back(make_type_parameter(value_type, enum_underlying_types));
    }
    return out;
  }

  if (field.is_enum) {
    auto enum_it = enum_underlying_types.find(type);
    if (enum_it != enum_underlying_types.end()) {
      out.type = schema_type_to_field_type(enum_it->second);
      return out;
    }
  }

  out.type = schema_type_to_field_type(type);
  if (out.type == FT_Unknown || out.type == FT_Object) {
    out.type = FT_Object;
    if (!type.empty()) {
      out.type_params.emplace_back(FT_Object, type);
    }
  }
  return out;
}

void append_declaration(DataBind* codec,
                        DataBindSchemaType const& type,
                        std::filesystem::path const& schema_path,
                        std::map<std::string, std::string> const& enum_underlying_types,
                        std::vector<ParsedDeclaration>& declarations)
{
  ParsedDeclaration decl;
  decl.type_name = copy_string(type.name);
  decl.annotations["schema_source"] = schema_path.string();
  decl.annotations["schema_decl_kind"] = data_bind_schema_kind_name(type.kind);

  std::size_t field_count = data_bind_schema_field_count(codec, type.name);
  decl.fields.reserve(field_count);
  for (std::size_t field_index = 0; field_index < field_count; ++field_index) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    if (data_bind_schema_field_at(codec, type.name, field_index, &field)) {
      decl.fields.push_back(convert_field(field, enum_underlying_types));
    }
  }

  if (!decl.type_name.empty()) {
    declarations.push_back(std::move(decl));
  }
}

std::string enum_underlying_type(std::string const& schema_underlying_type)
{
  FieldType field_type = schema_type_to_field_type(schema_underlying_type);
  switch (field_type) {
    case FT_Long:
      return "long";
    case FT_Int:
      return "int";
    default:
      return schema_underlying_type.empty() ? "int" : schema_underlying_type;
  }
}

void append_enums(DataBind* codec,
                  std::filesystem::path const& schema_path,
                  std::map<std::string, std::string>& enum_underlying_types,
                  std::vector<ParsedEnum>& enums)
{
  std::size_t enum_count = data_bind_schema_enum_count(codec);
  for (std::size_t i = 0; i < enum_count; ++i) {
    DataBindSchemaType enum_type = DATA_BIND_SCHEMA_TYPE_INIT;
    if (!data_bind_schema_enum_at(codec, i, &enum_type)) {
      continue;
    }

    ParsedEnum parsed;
    parsed.enum_name = copy_string(enum_type.name);
    parsed.underlying_type = enum_underlying_type(copy_string(enum_type.underlying_type));
    parsed.annotations["schema_source"] = schema_path.string();
    parsed.annotations["schema_decl_kind"] = data_bind_schema_kind_name(enum_type.kind);
    if (!parsed.enum_name.empty()) {
      enum_underlying_types[parsed.enum_name] = parsed.underlying_type;
    }

    std::size_t item_count = data_bind_schema_enum_item_count(codec, enum_type.name);
    parsed.values.reserve(item_count);
    for (std::size_t item_index = 0; item_index < item_count; ++item_index) {
      DataBindSchemaEnumItem item = DATA_BIND_SCHEMA_ENUM_ITEM_INIT;
      if (data_bind_schema_enum_item_at(codec, enum_type.name, item_index, &item)) {
        std::string item_name = copy_string(item.name);
        if (!item_name.empty()) parsed.values.push_back(std::move(item_name));
      }
    }

    if (!parsed.enum_name.empty()) {
      (void)schema_path;
      enums.push_back(std::move(parsed));
    }
  }
}

std::optional<std::filesystem::path> resolve_schema_path(SchemaImport const& import,
                                                         std::vector<std::string> const& base_dirs,
                                                         std::string const& default_source_name)
{
  std::filesystem::path requested(import.path);
  if (requested.is_absolute()) {
    if (std::filesystem::is_regular_file(requested)) {
      return std::filesystem::weakly_canonical(requested);
    }
    return std::nullopt;
  }

  std::vector<std::filesystem::path> search_dirs;
  std::string source_name = import.source_name.empty() ? default_source_name : import.source_name;
  if (!source_name.empty()) {
    std::filesystem::path source_path(source_name);
    if (source_path.has_parent_path()) {
      search_dirs.push_back(source_path.parent_path());
    }
  }
  for (auto const& base_dir : base_dirs) {
    if (!base_dir.empty()) {
      search_dirs.emplace_back(base_dir);
    }
  }
  if (search_dirs.empty()) {
    search_dirs.emplace_back(std::filesystem::current_path());
  }

  for (auto const& dir : search_dirs) {
    std::filesystem::path candidate = dir / requested;
    if (std::filesystem::is_regular_file(candidate)) {
      return std::filesystem::weakly_canonical(candidate);
    }
  }
  return std::nullopt;
}

bool is_declaration_kind(DataBindSchemaKind kind) {
  return kind == DATA_BIND_SCHEMA_MESSAGE
      || kind == DATA_BIND_SCHEMA_COMPOSITE
      || kind == DATA_BIND_SCHEMA_GROUP
      || kind == DATA_BIND_SCHEMA_UNION;
}

bool import_schema_file(std::filesystem::path const& schema_path,
                        parser_state& state,
                        std::vector<StructuredError>& errors,
                        SchemaImport const& import)
{
  DataBind* raw_codec = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status = data_bind_create(schema_path.string().c_str(), &raw_codec, &error);
  std::shared_ptr<DataBind> codec(raw_codec, data_bind_free);
  if (status != DATA_BIND_OK) {
    errors.push_back({.file_name = schema_path.string(),
                      .line = import.line,
                      .column = import.column,
                      .message = "DataBind schema load failed: "
                               + data_bind_error_message(status, error)});
    return false;
  }

  std::map<std::string, std::string> enum_underlying_types;
  append_enums(codec.get(), schema_path, enum_underlying_types, state.parsed_enums);

  std::size_t type_count = data_bind_schema_type_count(codec.get());
  for (std::size_t i = 0; i < type_count; ++i) {
    DataBindSchemaType type = DATA_BIND_SCHEMA_TYPE_INIT;
    if (data_bind_schema_type_at(codec.get(), i, &type) && is_declaration_kind(type.kind)) {
      append_declaration(codec.get(), type, schema_path, enum_underlying_types,
                         state.parsed_declarations);
    }
  }
  state.imported_data_bind_schemas.push_back(
      ImportedDataBindSchema{schema_path.string(), std::move(codec)});
  return true;
}

} // namespace

bool import_databind_schemas(parser_state& state,
                             std::vector<std::string> const& base_dirs,
                             std::vector<StructuredError>& errors,
                             std::string const& default_source_name)
{
  if (state.schema_imports.empty()) {
    return true;
  }

  std::set<std::filesystem::path> loaded;
  bool ok = true;
  for (auto const& import : state.schema_imports) {
    auto resolved = resolve_schema_path(import, base_dirs, default_source_name);
    if (!resolved) {
      std::string source_name = import.source_name.empty() ? default_source_name : import.source_name;
      errors.push_back({.file_name = source_name,
                        .line = import.line,
                        .column = import.column,
                        .message = "Could not resolve DataBind schema import: " + import.path});
      ok = false;
      continue;
    }

    if (!loaded.insert(*resolved).second) {
      continue;
    }
    ok = import_schema_file(*resolved, state, errors, import) && ok;
  }
  return ok;
}

} // namespace rulesforge
