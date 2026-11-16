#include "script/script_catalog.hpp"
#include <algorithm>
#include <data_bind.h>
#include <filesystem>
#include <fstream>
#include <json_parser.h>
#include <limits>
#include <map>
#include <set>

namespace rulesforge {
namespace {
constexpr size_t kDeploymentBytes = 16 * 1024 * 1024;
constexpr size_t kFileBytes = 4 * 1024 * 1024;
constexpr size_t kModules = 64, kBindings = 256, kSchemaDepth = 32;
constexpr size_t kSchemaNodes = 65536;
char const kBundleSchema[] = R"(
schema RulesForgeProgram [id(1), version(1), byte_order(little)];
message Module { string name; string path; }
message NativeModule { string path; }
message Binding { string name; string module; string function; list<string> parameters; string result; }
composite Limits {
  uint64 source_bytes; uint64 value_bytes; uint64 value_nodes; uint32 value_depth;
  uint32 steps; uint32 loops; uint32 recursion; uint64 retained_bytes; uint64 stack_bytes;
  uint64 max_sessions;
}
message Bundle {
  string provider; string rules; string signatures; string mode;
  list<Module> modules; list<Binding> bindings; Limits limits;
  optional list<NativeModule> native_modules;
}
)";
using Codec = std::unique_ptr<DataBind, decltype(&data_bind_free)>;
using Value = std::unique_ptr<DataBindValue, decltype(&data_bind_value_free)>;
[[noreturn]] void invalid(std::string const &s) {
  throw std::invalid_argument("Program configuration: " + s);
}
void db(DataBindStatus status, DataBindError const &error) {
  if (status == DATA_BIND_OK)
    return;
  if (status == DATA_BIND_ERR_OOM)
    throw std::bad_alloc();
  invalid(error.message);
}
std::string read(std::filesystem::path const &p, size_t &total) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  if (!f)
    invalid("Cannot read " + p.generic_string());
  auto length = f.tellg();
  if (length < 0 || static_cast<uintmax_t>(length) > kFileBytes ||
      static_cast<size_t>(length) > kDeploymentBytes - total)
    throw std::length_error("Program source byte limit exceeded");
  std::string text(static_cast<size_t>(length), '\0');
  f.seekg(0);
  if (!text.empty() && !f.read(text.data(), static_cast<std::streamsize>(text.size())))
    invalid("Incomplete read: " + p.generic_string());
  if (text.find('\0') != std::string::npos)
    invalid("Embedded NUL in deployment text");
  total += text.size();
  return text;
}
std::filesystem::path from_utf8(std::string const &text) {
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
std::string to_utf8(std::filesystem::path const &path) {
  auto text = path.generic_u8string();
  return std::string(reinterpret_cast<char const *>(text.data()), text.size());
}
std::filesystem::path contained(std::filesystem::path const &base, std::string const &name) {
  if (name.empty() || name.find('\0') != std::string::npos)
    invalid("Empty/invalid deployment path");
  auto path = std::filesystem::canonical(base / from_utf8(name));
  auto relative = path.lexically_relative(base);
  if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
    invalid("Path escapes deployment directory: " + name);
  return path;
}
std::string string(DataBindValue const *v) {
  const char *text = nullptr;
  size_t length = 0;
  if (data_bind_value_get_string(v, &text, &length) != DATA_BIND_OK)
    invalid("Expected a string");
  std::string result(text ? text : "", length);
  if (result.find('\0') != std::string::npos)
    invalid("Embedded NUL in configuration string");
  return result;
}
void reject_unknown(DataBind *codec, char const *type, json_value_t const *value,
                    size_t depth = 0) {
  if (depth > kSchemaDepth)
    invalid("Configuration nesting limit exceeded");
  DataBindSchemaType info{};
  info.size = sizeof(info);
  if (!data_bind_schema_find_type(codec, type, &info))
    return;
  if (json_type(value) != JSON_OBJECT)
    return; // The typed binder reports the shape error.
  std::set<std::string> seen;
  for (size_t n = 0; n < json_object_size(value); ++n) {
    auto key = json_object_key(value, n);
    auto length = json_object_key_len(value, n);
    std::string name(key ? key : "", length);
    if (!seen.insert(name).second)
      invalid("Duplicate configuration field: " + name);
    bool found = false;
    for (size_t i = 0; i < info.field_count; ++i) {
      DataBindSchemaField field{};
      field.size = sizeof(field);
      if (!data_bind_schema_field_at(codec, type, i, &field))
        invalid("Cannot inspect bundle schema");
      if (name != field.name)
        continue;
      found = true;
      auto child = json_object_value(value, n);
      if (field.is_collection && field.inner_type) {
        for (size_t j = 0; j < json_array_size(child); ++j)
          reject_unknown(codec, field.inner_type, json_array_get(child, j), depth + 1);
      } else if (field.type)
        reject_unknown(codec, field.type, child, depth + 1);
      break;
    }
    if (!found)
      invalid("Unknown configuration field: " + name);
  }
}
size_t positive(DataBindValue const *v) {
  uint64_t n = 0;
  if (data_bind_value_get_uint64(v, &n) != DATA_BIND_OK || !n || n > SIZE_MAX)
    invalid("Limits must be positive native-size integers");
  return static_cast<size_t>(n);
}
uint32_t positive32(DataBindValue const *v) {
  auto n = positive(v);
  if (n > UINT32_MAX)
    invalid("Limit exceeds uint32");
  return static_cast<uint32_t>(n);
}
ScriptShape shape(DataBind *codec, std::string const &type, size_t depth,
                  std::set<std::string> &visiting, size_t &nodes) {
  if (nodes >= kSchemaNodes)
    throw std::length_error("Script signature node limit exceeded");
  ++nodes;
  if (depth > kSchemaDepth)
    invalid("Script schema is too deep");
  ScriptShape result;
  static const std::map<std::string, ScriptKind> scalars = {
      {"bool", ScriptKind::Boolean},    {"int64", ScriptKind::Integer},
      {"uint64", ScriptKind::Unsigned}, {"double", ScriptKind::Number},
      {"string", ScriptKind::String},   {"uuid", ScriptKind::Uuid},
      {"decimal", ScriptKind::Decimal}, {"money", ScriptKind::Money}};
  if (auto it = scalars.find(type); it != scalars.end()) {
    result.kind = it->second;
    return result;
  }
  DataBindSchemaType info{};
  info.size = sizeof(info);
  if (!data_bind_schema_find_type(codec, type.c_str(), &info) ||
      (info.kind != DATA_BIND_SCHEMA_MESSAGE && info.kind != DATA_BIND_SCHEMA_COMPOSITE))
    invalid("Unsupported script type: " + type);
  if (!visiting.insert(type).second)
    invalid("Recursive script schema: " + type);
  result.kind = ScriptKind::Record;
  if (info.field_count > kBindings)
    invalid("Script record has too many fields");
  for (size_t i = 0; i < info.field_count; ++i) {
    DataBindSchemaField field{};
    field.size = sizeof(field);
    if (!data_bind_schema_field_at(codec, type.c_str(), i, &field))
      invalid("Cannot inspect script schema field");
    if (field.is_optional || field.has_default || field.is_map || field.is_group || field.is_enum ||
        data_bind_schema_field_constraint_count(codec, type.c_str(), i))
      invalid("Unsupported script field semantics: " + std::string(field.name));
    ScriptShape child;
    if (field.is_collection) {
      if (nodes >= kSchemaNodes)
        throw std::length_error("Script signature node limit exceeded");
      ++nodes;
      if (!field.collection_kind ||
          (std::string(field.collection_kind) != "list" &&
           std::string(field.collection_kind) != "array") ||
          !field.inner_type)
        invalid("Unsupported script collection");
      child.kind = ScriptKind::Array;
      child.element =
          std::make_shared<ScriptShape>(shape(codec, field.inner_type, depth + 1, visiting, nodes));
    } else
      child = shape(codec, field.type, depth + 1, visiting, nodes);
    child.nullable = field.is_nullable != 0;
    result.fields.emplace_back(field.name, std::move(child));
  }
  visiting.erase(type);
  return result;
}
} // namespace

std::shared_ptr<ScriptCatalog const> load_script_catalog(std::string const &name) {
  auto path = std::filesystem::canonical(from_utf8(name));
  auto base = path.parent_path();
  size_t total = 0;
  auto json = read(path, total);
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBind *raw = nullptr;
  db(data_bind_create_from_text(kBundleSchema, sizeof(kBundleSchema) - 1, &raw, &error), error);
  Codec config(raw, data_bind_free);
  std::unique_ptr<json_value_t, decltype(&json_free)> document(json_parse(json.data(), json.size()),
                                                               json_free);
  if (!document)
    invalid(json_get_error());
  if (json_type(document.get()) != JSON_OBJECT)
    invalid("Bundle root must be an object");
  reject_unknown(config.get(), "Bundle", document.get());
  db(data_bind_validate_json(config.get(), "Bundle", json.data(), json.size(), &error), error);
  DataBindValue *value = nullptr;
  db(data_bind_parse_json(config.get(), "Bundle", json.data(), json.size(), &value, &error), error);
  Value bundle(value, data_bind_value_free);
  auto get = [&](char const *key) { return data_bind_value_get(bundle.get(), key); };
  auto result = std::make_shared<ScriptCatalog>();
  result->provider_path = to_utf8(contained(base, string(get("provider"))));
  auto native = get("native_modules");
  auto native_count = data_bind_value_count(native);
  if (native_count > RF_SCRIPT_NATIVE_MODULE_LIMIT)
    invalid("Native module count exceeds Toad's attachment capacity");
  std::set<std::string> native_paths;
  for (size_t i = 0; i < native_count; ++i) {
    auto item = data_bind_value_at(native, i);
    auto module_path = to_utf8(contained(base, string(data_bind_value_get(item, "path"))));
    if (!native_paths.insert(module_path).second)
      invalid("Duplicate native module path");
    result->native_modules.push_back(std::move(module_path));
  }
  auto rules = contained(base, string(get("rules")));
  result->rule_path = to_utf8(rules);
  result->rule_source = read(rules, total);
  auto schema_text = read(contained(base, string(get("signatures"))), total);
  raw = nullptr;
  db(data_bind_create_from_text(schema_text.data(), schema_text.size(), &raw, &error), error);
  Codec schema(raw, data_bind_free);
  auto limits = get("limits");
  auto limit = [&](char const *key) { return data_bind_value_get(limits, key); };
  auto &l = result->limits;
  l.source_bytes = positive(limit("source_bytes"));
  l.value_bytes = positive(limit("value_bytes"));
  l.value_nodes = positive(limit("value_nodes"));
  l.value_depth = positive32(limit("value_depth"));
  l.steps = positive32(limit("steps"));
  l.loops = positive32(limit("loops"));
  l.recursion = positive32(limit("recursion"));
  l.retained_bytes = positive(limit("retained_bytes"));
  l.stack_bytes = positive(limit("stack_bytes"));
  result->max_sessions = positive(limit("max_sessions"));
  auto mode = string(get("mode"));
  if (mode == "interpreter")
    l.mode = TOAD_SCRIPT_EXEC_INTERPRETER;
  else if (mode == "jit")
    l.mode = TOAD_SCRIPT_EXEC_JIT;
  else
    invalid("Unknown execution mode");
  if (l.source_bytes > kFileBytes || l.value_bytes > 4 * 1024 * 1024 || l.value_nodes > 65536 ||
      l.value_depth > 64 || l.recursion > 100 || l.retained_bytes > 128 * 1024 * 1024 ||
      l.stack_bytes > 1024 * 1024 || l.value_bytes > l.retained_bytes)
    invalid("Limits exceed the released Toad profile");
  std::map<std::string, size_t> modules;
  auto list = get("modules");
  auto count = data_bind_value_count(list);
  if (!count || count > kModules)
    invalid("Module count out of range");
  for (size_t i = 0; i < count; ++i) {
    auto item = data_bind_value_at(list, i);
    auto id = string(data_bind_value_get(item, "name"));
    if (id.empty() || !modules.emplace(id, i).second)
      invalid("Duplicate/empty module name");
    auto text = read(contained(base, string(data_bind_value_get(item, "path"))), total);
    if (text.size() > l.source_bytes)
      throw std::length_error("Script source byte limit exceeded");
    result->modules.push_back({std::move(id), std::move(text)});
  }
  std::set<std::string> names;
  size_t signature_nodes = 0;
  list = get("bindings");
  count = data_bind_value_count(list);
  if (!count || count > kBindings)
    invalid("Binding count out of range");
  for (size_t i = 0; i < count; ++i) {
    auto item = data_bind_value_at(list, i);
    ScriptBinding binding;
    binding.name = string(data_bind_value_get(item, "name"));
    if (binding.name.empty() || !names.insert(binding.name).second)
      invalid("Duplicate/empty binding");
    auto module = modules.find(string(data_bind_value_get(item, "module")));
    if (module == modules.end())
      invalid("Unknown module");
    binding.module = module->second;
    binding.export_name = string(data_bind_value_get(item, "function"));
    if (binding.export_name.empty())
      invalid("Empty export");
    std::set<std::string> visiting;
    auto parameters = data_bind_value_get(item, "parameters");
    auto n = data_bind_value_count(parameters);
    if (n > 16)
      invalid("Too many script parameters");
    for (size_t p = 0; p < n; ++p)
      binding.parameters.push_back(shape(schema.get(), string(data_bind_value_at(parameters, p)), 1,
                                         visiting, signature_nodes));
    binding.result = shape(schema.get(), string(data_bind_value_get(item, "result")), 1, visiting,
                           signature_nodes);
    result->bindings.push_back(std::move(binding));
  }
  return result;
}
} // namespace rulesforge
