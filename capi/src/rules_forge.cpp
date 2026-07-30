// Include the header file to get type definitions
#include "rules_forge.h"

// Include C string handling
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fmt.h>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Include C++ backend
#include "core/errors.hpp"
#include "core/exceptions.hpp"
#include "engine/continuous_session.hpp"
#include "engine/knowledge_base.hpp"
#include "engine/query_result.hpp"
#include "engine/stateful_session.hpp"
#include "rfl_parser.hpp"

#include <data_bind.h>

using namespace rulesforge;

namespace {
thread_local char last_error[1024] = "";

static void set_error(const char *msg) { fmt(last_error, sizeof(last_error), "{}", msg); }

static void set_error_fmt(const char *prefix, const char *detail) {
  fmt(last_error, sizeof(last_error), "{}{}", prefix, detail);
}

static char const *data_bind_error_detail(DataBindStatus status, DataBindError const &error) {
  if (error.message[0] != '\0') {
    return error.message;
  }
  char const *status_name = data_bind_status_name(status);
  return status_name ? status_name : "unknown DataBind error";
}

static ruleforge_status_t map_session_inconsistent(SessionInconsistentException const &e) {
  set_error(e.what());
  return RULES_FORGE_ERROR_SESSION_INCONSISTENT;
}

static std::string unqualified_type_name(std::string const &type_name) {
  auto const dot = type_name.find_last_of('.');
  return dot == std::string::npos ? type_name : type_name.substr(dot + 1);
}

static bool is_schema_imported_fact_type(StatefulSession const &session, char const *fact_type) {
  if (!fact_type) {
    return false;
  }
  auto const kb = session.get_knowledge_base();
  if (!kb) {
    return false;
  }
  std::string const requested(fact_type);
  for (auto const &decl : kb->get_parser_state().parsed_declarations) {
    if (decl.annotations.find("schema_source") == decl.annotations.end()) {
      continue;
    }
    if (decl.type_name == requested || unqualified_type_name(decl.type_name) == requested) {
      return true;
    }
  }
  return false;
}

static bool is_schema_imported_fact_type(KnowledgeBase const &kb, char const *fact_type) {
  return fact_type != nullptr && kb.has_data_bind_type(fact_type);
}

static ruleforge_status_t require_schema_imported_fact_type(StatefulSession const &session,
                                                            char const *fact_type) {
  if (is_schema_imported_fact_type(session, fact_type)) {
    return RULES_FORGE_OK;
  }
  fmt(last_error, sizeof(last_error),
      "External input fact type '{}' must be imported into the knowledge base from a .schema file.",
      fact_type ? fact_type : "<null>");
  return RULES_FORGE_ERROR_INVALID_ARGUMENT;
}

static ruleforge_status_t require_schema_imported_fact_type(KnowledgeBase const &kb,
                                                            char const *fact_type) {
  if (is_schema_imported_fact_type(kb, fact_type)) {
    return RULES_FORGE_OK;
  }
  fmt(last_error, sizeof(last_error),
      "External input fact type '{}' must be imported into the knowledge base from a .schema file.",
      fact_type ? fact_type : "<null>");
  return RULES_FORGE_ERROR_INVALID_ARGUMENT;
}

struct DataBindHandleDeleter {
  void operator()(DataBind *codec) const { data_bind_free(codec); }
};

struct DataBindValueDeleter {
  void operator()(DataBindValue *value) const { data_bind_value_free(value); }
};

struct DataBindObjectDeleter {
  void operator()(DataBindObject *object) const { data_bind_object_free(object); }
};

struct DataBindStreamDeleter {
  void operator()(data_bind_stream_t *stream) const { data_bind_stream_destroy(stream); }
};

using DataBindHandle = std::shared_ptr<DataBind>;
using DataBindValueHandle = std::unique_ptr<DataBindValue, DataBindValueDeleter>;
using DataBindObjectHandle = std::unique_ptr<DataBindObject, DataBindObjectDeleter>;
using DataBindStreamHandle = std::unique_ptr<data_bind_stream_t, DataBindStreamDeleter>;

constexpr int kMinimumDataBindVersion = 20000;
constexpr int kRequiredDataBindAbi = 8;

struct DataBindView {
  DataBind *value = nullptr;
  DataBind *get() const { return value; }
  explicit operator bool() const { return value != nullptr; }
};

static DataBindView get_kb_data_bind_or_set_error(KnowledgeBase const &kb,
                                                   char const *fact_type) {
  if (!fact_type) {
    set_error("Fact type is NULL");
    return {};
  }
  DataBind *codec = kb.find_data_bind_codec(fact_type);
  auto const *imported_path = kb.find_data_bind_schema_path(fact_type);
  if (!codec || !imported_path) {
    fmt(last_error, sizeof(last_error),
        "Fact type '{}' is not provided by an imported DataBind schema.", fact_type);
    return {};
  }
  return {codec};
}

static void require_data_bind_value(DataBindStatus status, char const *kind) {
  if (status == DATA_BIND_OK) return;
  char const *status_name = data_bind_status_name(status);
  throw std::runtime_error(std::string("Failed to extract DataBind ") + kind + ": "
                           + (status_name ? status_name : "unknown DataBind error"));
}

static std::optional<DataBindSchemaField> find_data_bind_field(
    DataBind *codec, std::string const &type_name, std::string_view field_name) {
  if (!codec || type_name.empty()) return std::nullopt;
  size_t const field_count = data_bind_schema_field_count(codec, type_name.c_str());
  for (size_t index = 0; index < field_count; ++index) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    if (data_bind_schema_field_at(codec, type_name.c_str(), index, &field)
        && field.name && field_name == field.name) {
      return field;
    }
  }
  return std::nullopt;
}

static std::string child_declared_type(DataBindSchemaField const &field,
                                       DataBindValueKind value_kind) {
  char const *type = field.type;
  if (value_kind == DATA_BIND_VALUE_LIST || value_kind == DATA_BIND_VALUE_SET) {
    type = field.inner_type;
  } else if (value_kind == DATA_BIND_VALUE_MAP) {
    type = field.value_type;
  }
  return type ? std::string(type) : std::string();
}

static bool enum_item_matches(EnumNumericValue const &value, char const *text) {
  if (!text) return false;
  char *end = nullptr;
  if (auto const *signed_value = std::get_if<int64_t>(&value)) {
    long long const parsed = std::strtoll(text, &end, 0);
    return end != text && *end == '\0' && parsed == *signed_value;
  }
  unsigned long long const parsed = std::strtoull(text, &end, 0);
  return end != text && *end == '\0' && parsed == std::get<uint64_t>(value);
}

static ConstraintValue data_bind_enum_to_constraint(DataBind *codec,
                                                     std::string const &enum_type,
                                                     DataBindValue const *value) {
  EnumNumericValue numeric;
  switch (data_bind_value_kind(value)) {
  case DATA_BIND_VALUE_INT: {
    int32_t extracted = 0;
    require_data_bind_value(data_bind_value_get_int32(value, &extracted), "enum");
    numeric = static_cast<int64_t>(extracted);
    break;
  }
  case DATA_BIND_VALUE_INT64: {
    int64_t extracted = 0;
    require_data_bind_value(data_bind_value_get_int64(value, &extracted), "enum");
    numeric = extracted;
    break;
  }
  case DATA_BIND_VALUE_UINT64: {
    uint64_t extracted = 0;
    require_data_bind_value(data_bind_value_get_uint64(value, &extracted), "enum");
    numeric = extracted;
    break;
  }
  default:
    throw std::runtime_error("DataBind enum value is not an integer kind");
  }

  std::string item_name;
  size_t const item_count = data_bind_schema_enum_item_count(codec, enum_type.c_str());
  for (size_t index = 0; index < item_count; ++index) {
    DataBindSchemaEnumItem item = DATA_BIND_SCHEMA_ENUM_ITEM_INIT;
    if (data_bind_schema_enum_item_at(codec, enum_type.c_str(), index, &item)
        && item.name && enum_item_matches(numeric, item.value)) {
      item_name = item.name;
      break;
    }
  }
  return EnumValue{enum_type, std::move(numeric), std::move(item_name)};
}

static ConstraintValue data_bind_value_to_constraint(
    DataBindValue const *value, DataBind *codec = nullptr,
    std::string const &declared_type = {}) {
  if (!value) {
    return NilValue{};
  }

  if (codec && !declared_type.empty()
      && data_bind_schema_enum_item_count(codec, declared_type.c_str()) != 0) {
    return data_bind_enum_to_constraint(codec, declared_type, value);
  }

  switch (data_bind_value_kind(value)) {
  case DATA_BIND_VALUE_NULL:
    return NilValue{};
  case DATA_BIND_VALUE_INT: {
    int32_t extracted = 0;
    require_data_bind_value(data_bind_value_get_int32(value, &extracted), "int32");
    return static_cast<int64_t>(extracted);
  }
  case DATA_BIND_VALUE_INT64: {
    int64_t extracted = 0;
    require_data_bind_value(data_bind_value_get_int64(value, &extracted), "int64");
    return extracted;
  }
  case DATA_BIND_VALUE_UINT64: {
    uint64_t extracted = 0;
    require_data_bind_value(data_bind_value_get_uint64(value, &extracted), "uint64");
    return extracted;
  }
  case DATA_BIND_VALUE_DOUBLE: {
    double extracted = 0.0;
    require_data_bind_value(data_bind_value_get_double(value, &extracted), "double");
    return extracted;
  }
  case DATA_BIND_VALUE_BOOL: {
    int extracted = 0;
    require_data_bind_value(data_bind_value_get_bool(value, &extracted), "bool");
    return extracted != 0;
  }
  case DATA_BIND_VALUE_STRING: {
    char const *text = nullptr;
    size_t length = 0;
    require_data_bind_value(data_bind_value_get_string(value, &text, &length), "string");
    return std::string(text ? text : "", length);
  }
  case DATA_BIND_VALUE_BYTES: {
    size_t length = 0;
    uint8_t const *bytes = nullptr;
    require_data_bind_value(data_bind_value_get_bytes(value, &bytes, &length), "bytes");
    BytesValue result;
    if (length != 0) result.bytes.assign(bytes, bytes + length);
    return result;
  }
  case DATA_BIND_VALUE_OBJECT: {
    auto map = std::make_shared<ValueMap>();
    size_t field_count = data_bind_value_field_count(value);
    for (size_t i = 0; i < field_count; ++i) {
      char const *name = data_bind_value_field_name(value, i);
      DataBindValue const *child = data_bind_value_field_at(value, i);
      if (name) {
        std::string nested_type;
        if (auto field = find_data_bind_field(codec, declared_type, name)) {
          nested_type = child_declared_type(*field, data_bind_value_kind(child));
        }
        map->entries[std::string(name)] =
            data_bind_value_to_constraint(child, codec, nested_type);
      }
    }
    return map;
  }
  case DATA_BIND_VALUE_LIST: {
    auto list = std::make_shared<TypedList>();
    size_t count = data_bind_value_count(value);
    list->values.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      list->values.push_back(
          data_bind_value_to_constraint(data_bind_value_at(value, i), codec, declared_type));
    }
    return list;
  }
  case DATA_BIND_VALUE_SET: {
    auto set = std::make_shared<ValueSet>();
    size_t count = data_bind_value_count(value);
    for (size_t i = 0; i < count; ++i) {
      set->values.insert(
          data_bind_value_to_constraint(data_bind_value_at(value, i), codec, declared_type));
    }
    return set;
  }
  case DATA_BIND_VALUE_MAP: {
    auto map = std::make_shared<ValueMap>();
    size_t count = data_bind_value_count(value);
    for (size_t i = 0; i < count; ++i) {
      DataBindMapEntry entry = data_bind_value_map_entry_at(value, i);
      if (entry.key) {
        map->entries[std::string(entry.key)] =
            data_bind_value_to_constraint(entry.value, codec, declared_type);
      }
    }
    return map;
  }
  case DATA_BIND_VALUE_UUID: {
    turbo_uuid_t uuid{};
    require_data_bind_value(data_bind_value_get_uuid(value, uuid.bytes), "UUID");
    return uuid;
  }
  case DATA_BIND_VALUE_DATETIME: {
    turbo_datetime_t extracted{};
    require_data_bind_value(data_bind_value_get_datetime(value, &extracted), "datetime");
    return DateTimeValue{extracted.year, extracted.month, extracted.day, extracted.hour,
                         extracted.minute, extracted.second, extracted.millisecond,
                         extracted.tz_offset, extracted.has_tz != 0};
  }
  case DATA_BIND_VALUE_DATE: {
    DataBindDate extracted{};
    require_data_bind_value(data_bind_value_get_date(value, &extracted), "date");
    return DateValue{extracted.year, extracted.month, extracted.day};
  }
  case DATA_BIND_VALUE_TIME: {
    DataBindTime extracted{};
    require_data_bind_value(data_bind_value_get_time(value, &extracted), "time");
    return TimeValue{extracted.hour, extracted.minute, extracted.second, extracted.millisecond};
  }
  case DATA_BIND_VALUE_DURATION: {
    int64_t extracted = 0;
    require_data_bind_value(
        data_bind_value_get_duration_milliseconds(value, &extracted), "duration");
    return DurationValue{extracted};
  }
  case DATA_BIND_VALUE_DECIMAL: {
    DataBindDecimal extracted{};
    require_data_bind_value(data_bind_value_get_decimal(value, &extracted), "decimal");
    return DecimalValue{extracted.mantissa, extracted.scale};
  }
  case DATA_BIND_VALUE_BIGINT: {
    char const *text = nullptr;
    size_t length = 0;
    require_data_bind_value(data_bind_value_get_bigint(value, &text, &length), "bigint");
    return BigIntValue{std::string(text ? text : "", length)};
  }
  case DATA_BIND_VALUE_MONEY: {
    DataBindMoney extracted{};
    require_data_bind_value(data_bind_value_get_money(value, &extracted), "money");
    size_t currency_length = 0;
    while (currency_length < sizeof(extracted.currency)
           && extracted.currency[currency_length] != '\0') ++currency_length;
    return MoneyValue{DecimalValue{extracted.amount.mantissa, extracted.amount.scale},
                      std::string(extracted.currency, currency_length)};
  }
  }

  return NilValue{};
}

static bool populate_fact_from_data_bind_value(Fact &fact, DataBindValue const *value,
                                                DataBind *codec = nullptr,
                                                char const *fact_type = nullptr) {
  if (!value || data_bind_value_kind(value) != DATA_BIND_VALUE_OBJECT) {
    return false;
  }

  size_t field_count = data_bind_value_field_count(value);
  for (size_t i = 0; i < field_count; ++i) {
    char const *name = data_bind_value_field_name(value, i);
    DataBindValue const *child = data_bind_value_field_at(value, i);
    if (name) {
      std::string declared_type;
      if (auto field = find_data_bind_field(codec, fact_type ? fact_type : "", name)) {
        declared_type = child_declared_type(*field, data_bind_value_kind(child));
      }
      fact.fields[name] = data_bind_value_to_constraint(child, codec, declared_type);
    }
  }
  return true;
}

static ruleforge_status_t insert_data_bind_fact(StatefulSession &session,
                                                char const *fact_type,
                                                DataBindValue const *value,
                                                ruleforge_fact_t *out_fact) {
  if (!fact_type || !value) {
    set_error("Fact type or bound value is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (data_bind_value_kind(value) != DATA_BIND_VALUE_OBJECT) {
    set_error("DataBind result is not an object fact");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  Fact *fact = session.create_fact(fact_type);
  auto const kb = session.get_knowledge_base();
  DataBind *codec = kb ? kb->find_data_bind_codec(fact_type) : nullptr;
  if (!populate_fact_from_data_bind_value(*fact, value, codec, fact_type)) {
    set_error("Failed to convert DataBind result to fact");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  session.add_fact(fact);
  if (out_fact) {
    *out_fact = reinterpret_cast<ruleforge_fact_t>(fact);
  }
  return RULES_FORGE_OK;
}

static ruleforge_status_t insert_data_bind_object(
    StatefulSession &session, DataBindObject const *object,
    ruleforge_fact_t *out_fact) {
  if (!object) {
    set_error("DataBindObject is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  return insert_data_bind_fact(
      session, data_bind_object_type_name(object),
      data_bind_object_value(object), out_fact);
}

static ruleforge_status_t insert_data_bind_fact_list(StatefulSession &session,
                                                     char const *fact_type,
                                                     DataBindValue const *value,
                                                     ruleforge_fact_t **out_facts,
                                                     int *out_loaded_count) {
  if (out_facts) {
    *out_facts = nullptr;
  }
  if (out_loaded_count) {
    *out_loaded_count = 0;
  }
  if (!fact_type || !value) {
    set_error("Fact type or bound value list is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (data_bind_value_kind(value) != DATA_BIND_VALUE_LIST) {
    set_error("DataBind all-result is not a list");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  size_t count = data_bind_value_count(value);

  // Phase 1: validate all items and build fact objects BEFORE inserting any.
  // This ensures we do not leave the session with partially inserted facts if
  // a later item is invalid.
  struct BuiltFact {
    Fact *fact;
  };
  std::vector<BuiltFact> built;
  built.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    DataBindValue const *item = data_bind_value_at(value, i);
    if (!item || data_bind_value_kind(item) != DATA_BIND_VALUE_OBJECT) {
      set_error("DataBind list item is not an object fact");
      // Facts created so far are arena-owned; they will be freed with the session.
      // They have NOT been add_fact'd yet, so working memory is still clean.
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    Fact *fact = session.create_fact(fact_type);
    auto const kb = session.get_knowledge_base();
    DataBind *codec = kb ? kb->find_data_bind_codec(fact_type) : nullptr;
    if (!populate_fact_from_data_bind_value(*fact, item, codec, fact_type)) {
      set_error("Failed to convert DataBind list item to fact");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    built.push_back({fact});
  }

  std::unique_ptr<ruleforge_fact_t, decltype(&std::free)> fact_array(nullptr, &std::free);
  if (out_facts && !built.empty()) {
    fact_array.reset(
        static_cast<ruleforge_fact_t *>(std::calloc(built.size(), sizeof(ruleforge_fact_t))));
    if (!fact_array) {
      set_error("Failed to allocate fact handle array");
      return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
    }
  }

  // Phase 2: all validation and output allocation succeeded; commit the facts.
  std::vector<Fact *> facts_to_insert;
  facts_to_insert.reserve(built.size());
  for (auto const &built_fact : built) {
    facts_to_insert.push_back(built_fact.fact);
  }
  session.add_facts(facts_to_insert);
  for (size_t i = 0; i < built.size(); ++i) {
    if (fact_array) {
      fact_array.get()[i] = reinterpret_cast<ruleforge_fact_t>(built[i].fact);
    }
  }

  if (out_facts) {
    *out_facts = fact_array.release();
  }
  if (out_loaded_count) {
    *out_loaded_count = static_cast<int>(built.size());
  }
  return RULES_FORGE_OK;
}

static DataBindHandle create_data_bind_or_set_error(char const *schema_path) {
  if (!schema_path) {
    set_error("Schema path is NULL");
    return {};
  }
  DataBind *raw_codec = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status = data_bind_create(schema_path, &raw_codec, &error);
  if (status != DATA_BIND_OK || !raw_codec) {
    if (raw_codec) {
      data_bind_free(raw_codec);
    }
    set_error_fmt("DataBind schema load failed: ",
                  data_bind_error_detail(status, error));
    return {};
  }
  return DataBindHandle(raw_codec, DataBindHandleDeleter{});
}

struct KnowledgeBaseWrapper {
  std::shared_ptr<KnowledgeBase> kb;
};

} // namespace

ruleforge_status_t ruleforge_init() {
  int const library_version = data_bind_library_version();
  int const library_abi = data_bind_abi_version();
  if (library_version < kMinimumDataBindVersion
      || library_abi != kRequiredDataBindAbi
      || DATA_BIND_ABI_VERSION != kRequiredDataBindAbi) {
    fmt(last_error, sizeof(last_error),
        "Incompatible DataBind library: need version >= 2.0.0 with ABI {}, got {} with ABI {}",
        kRequiredDataBindAbi,
        data_bind_version_string() ? data_bind_version_string() : "<unknown>", library_abi);
    return RULES_FORGE_ERROR_GENERIC;
  }
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

ruleforge_status_t ruleforge_cleanup() {
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

const char *ruleforge_get_last_error_message() { return last_error; }

const char *ruleforge_get_version() { return RULEFORGE_VERSION_STRING; }

// Knowledge Base functions
// Helper function for crossing C++/pure C boundaries safely

ruleforge_status_t ruleforge_kb_create(ruleforge_knowledge_base_t *out_kb) {
  if (!out_kb) {
    set_error("Output Knowledge Base pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto kb_wrapper = new KnowledgeBaseWrapper();
    kb_wrapper->kb = KnowledgeBase::create_empty();
    *out_kb = reinterpret_cast<ruleforge_knowledge_base_t>(kb_wrapper);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to create Knowledge Base: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_kb_load_drl(ruleforge_knowledge_base_t kb, const char *drl_source) {
  if (!kb) {
    set_error("Knowledge Base handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (!drl_source) {
    set_error("RFL source is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    ParsingResult result;
    auto kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);

    auto compiled_kb = build_knowledge_base(drl_source, result, "C_API_Source");

    if (!result.success || !compiled_kb) {
      std::string error_msg = "RFL compilation failed: ";
      for (const auto &err : result.errors) {
        error_msg += err.to_string() + "; ";
      }
      if (result.errors.empty()) {
        error_msg += "Unknown compilation error.";
      }
      fmt(last_error, sizeof(last_error), "{}", error_msg);
      return RULES_FORGE_ERROR_COMPILATION_FAILED;
    }

    kb_wrapper->kb = std::move(compiled_kb);

    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to load RFL: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_kb_load_drl_file(ruleforge_knowledge_base_t kb, const char *file_path,
                                              const char **base_dirs, int base_dir_count) {
  if (!kb || !file_path) {
    set_error("Knowledge Base handle or file_path is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);
    std::vector<std::string> dirs;
    for (int i = 0; i < base_dir_count; i++) {
      if (base_dirs[i]) {
        dirs.emplace_back(base_dirs[i]);
      }
    }

    ParsingResult result;
    std::shared_ptr<KnowledgeBase> compiled_kb;
    if (dirs.empty()) {
      compiled_kb = build_knowledge_base(result, std::string(file_path));
    } else {
      compiled_kb = build_knowledge_base(std::string(file_path), dirs, result);
    }

    if (!result.success || !compiled_kb) {
      std::string error_msg = "RFL compilation failed: ";
      for (const auto &err : result.errors) {
        error_msg += err.to_string() + "; ";
      }
      if (result.errors.empty()) {
        error_msg += "Unknown compilation error.";
      }
      fmt(last_error, sizeof(last_error), "{}", error_msg);
      return RULES_FORGE_ERROR_COMPILATION_FAILED;
    }

    kb_wrapper->kb = std::move(compiled_kb);

    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to load RFL file: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_kb_load_drl_files(ruleforge_knowledge_base_t kb,
                                               const char **file_paths, int file_count,
                                               const char **base_dirs, int base_dir_count) {
  if (!kb || !file_paths || file_count <= 0) {
    set_error("Invalid arguments");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);
    std::vector<std::string> files;
    for (int i = 0; i < file_count; i++) {
      if (file_paths[i]) {
        files.emplace_back(file_paths[i]);
      }
    }

    std::vector<std::string> dirs;
    for (int i = 0; i < base_dir_count; i++) {
      if (base_dirs[i]) {
        dirs.emplace_back(base_dirs[i]);
      }
    }

    ParsingResult result;
    auto compiled_kb = build_knowledge_base(files, dirs, result);

    if (!result.success || !compiled_kb) {
      std::string error_msg = "RFL compilation failed: ";
      for (const auto &err : result.errors) {
        error_msg += err.to_string() + "; ";
      }
      if (result.errors.empty()) {
        error_msg += "Unknown compilation error.";
      }
      fmt(last_error, sizeof(last_error), "{}", error_msg);
      return RULES_FORGE_ERROR_COMPILATION_FAILED;
    }

    kb_wrapper->kb = std::move(compiled_kb);

    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to load RFL files: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_kb_load_decision_table_csv(ruleforge_knowledge_base_t kb,
                                                        const char *csv_source) {
  if (!kb) {
    set_error("Knowledge Base handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (!csv_source) {
    set_error("CSV source is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    ParsingResult result;
    auto kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);

    auto compiled_kb = build_knowledge_base_from_csv_string(csv_source, result, "C_API_CSV_Source");

    if (!result.success || !compiled_kb) {
      std::string error_msg = "Decision table compilation failed: ";
      for (const auto &err : result.errors) {
        error_msg += err.to_string() + "; ";
      }
      if (result.errors.empty()) {
        error_msg += "Unknown compilation error.";
      }
      fmt(last_error, sizeof(last_error), "{}", error_msg);
      return RULES_FORGE_ERROR_COMPILATION_FAILED;
    }

    kb_wrapper->kb = std::move(compiled_kb);

    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to load CSV: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_kb_destroy(ruleforge_knowledge_base_t kb) {
  if (!kb) {
    set_error("Knowledge Base handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);

    delete kb_wrapper;
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to destroy Knowledge Base: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_kb_has_schema_type(ruleforge_knowledge_base_t kb,
                                                char const *type_name,
                                                int *out_exists) {
  if (out_exists) {
    *out_exists = 0;
  }
  if (!kb || !type_name || !out_exists) {
    set_error("Knowledge Base, type name, or output pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  auto const *wrapper = reinterpret_cast<KnowledgeBaseWrapper const *>(kb);
  if (!wrapper->kb) {
    set_error("Knowledge Base is not initialized");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  *out_exists = wrapper->kb->has_data_bind_type(type_name) ? 1 : 0;
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

// Session wrapper for safe C++/C interop
struct StatefulSessionWrapper {
  std::unique_ptr<StatefulSession> session;
  size_t active_data_bind_streams = 0;
};

struct ContinuousSessionWrapper {
  std::shared_ptr<KnowledgeBase> kb;
  std::unique_ptr<ContinuousSession> session;
  size_t max_pending_results = 0;
  size_t active_data_bind_streams = 0;
};

struct ruleforge_continuous_result_handle_s {
  ContinuousStepResult result;
  std::vector<Fact> output_facts;
};

struct ruleforge_continuous_data_bind_stream_handle_s {
  ContinuousSessionWrapper *session_wrapper = nullptr;
  DataBindView codec;
  DataBindValueHandle value;
  DataBindValue *output_value = nullptr;
  DataBindStreamHandle stream;
  DataBindError error = DATA_BIND_ERROR_INIT;
  std::string fact_type;
  std::string event_id;
  std::string event_id_field;
  std::string entry_point;
  std::string event_time_field;
  std::vector<EventEnvelope> pending_events;
  std::string callback_error;
  int64_t event_time_ms = 0;
  bool multiple = false;
  bool failed = false;
  bool finished = false;
};

struct ruleforge_data_bind_stream_handle_s {
  StatefulSessionWrapper *session_wrapper = nullptr;
  DataBindView codec;
  DataBindValueHandle value;
  DataBindValue *output_value = nullptr;
  DataBindStreamHandle stream;
  DataBindError error = DATA_BIND_ERROR_INIT;
  std::string fact_type;
  bool multiple = false;
  bool failed = false;
  bool finished = false;
};

struct ruleforge_data_bind_object_handle_s {
  DataBindHandle codec;
  DataBindObjectHandle object;
};

namespace {

using DataBindObjectTextParser = DataBindStatus (*)(
    DataBind *, char const *, char const *, size_t, DataBindObject **,
    DataBindError *);
using DataBindObjectSerializer = DataBindStatus (*)(
    DataBind *, DataBindObject const *, char **, size_t *, DataBindError *);
using DataBindObjectWriter = DataBindStatus (*)(
    DataBind *, DataBindObject const *, DataBindWriteFn, void *, DataBindError *);

static ruleforge_status_t map_data_bind_object_error(
    char const *operation, DataBindStatus status, DataBindError const &error) {
  set_error_fmt(operation, data_bind_error_detail(status, error));
  if (status == DATA_BIND_ERR_OOM) {
    return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
  }
  if (status == DATA_BIND_ERR_IO || status == DATA_BIND_ERR_RUNTIME) {
    return RULES_FORGE_ERROR_GENERIC;
  }
  return RULES_FORGE_ERROR_INVALID_ARGUMENT;
}

static ruleforge_status_t publish_data_bind_object(
    DataBindHandle codec, DataBindObjectHandle object,
    ruleforge_data_bind_object_t *out_object) {
  if (!codec || !object || !out_object) {
    set_error("DataBind codec, object, or output handle is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto handle = std::make_unique<ruleforge_data_bind_object_handle_s>();
    handle->codec = std::move(codec);
    handle->object = std::move(object);
    *out_object = handle.release();
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (std::bad_alloc const &) {
    set_error("Failed to allocate DataBindObject handle");
    return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
  }
}

static ruleforge_status_t create_text_data_bind_object(
    char const *schema_path, char const *fact_type, char const *text,
    size_t len, ruleforge_data_bind_object_t *out_object,
    DataBindObjectTextParser parse, char const *operation) {
  if (out_object) {
    *out_object = nullptr;
  }
  if (!schema_path || !fact_type || !text || !out_object) {
    set_error("Schema path, fact type, text input, or output object is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  auto codec = create_data_bind_or_set_error(schema_path);
  if (!codec) {
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindObject *raw_object = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status = parse(codec.get(), fact_type, text, len,
                                &raw_object, &error);
  DataBindObjectHandle object(raw_object);
  if (status != DATA_BIND_OK) {
    return map_data_bind_object_error(operation, status, error);
  }
  return publish_data_bind_object(std::move(codec), std::move(object), out_object);
}

static ruleforge_status_t serialize_data_bind_object(
    ruleforge_data_bind_object_t object, char **out_text, size_t *out_len,
    DataBindObjectSerializer serialize, char const *operation) {
  if (out_text) {
    *out_text = nullptr;
  }
  if (out_len) {
    *out_len = 0;
  }
  auto *handle = reinterpret_cast<ruleforge_data_bind_object_handle_s *>(object);
  if (!handle || !handle->codec || !handle->object || !out_text) {
    set_error("DataBind codec, object, or serialized output pointer is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status = serialize(
      handle->codec.get(), handle->object.get(), out_text, out_len, &error);
  if (status != DATA_BIND_OK) {
    return map_data_bind_object_error(operation, status, error);
  }
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

static ruleforge_status_t write_data_bind_object(
    ruleforge_data_bind_object_t object, ruleforge_write_fn write, void *user,
    DataBindObjectWriter writer, char const *operation) {
  auto *handle = reinterpret_cast<ruleforge_data_bind_object_handle_s *>(object);
  if (!handle || !handle->codec || !handle->object || !write) {
    set_error("DataBind codec, object, or byte sink is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status = writer(
      handle->codec.get(), handle->object.get(), write, user, &error);
  if (status != DATA_BIND_OK) {
    return map_data_bind_object_error(operation, status, error);
  }
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

static ruleforge_status_t prepare_data_bind_stream(
    ruleforge_stateful_session_t session, char const *fact_type, bool multiple,
    ruleforge_data_bind_stream_t *out_stream,
    std::unique_ptr<ruleforge_data_bind_stream_handle_s> &handle) {
  if (out_stream) {
    *out_stream = nullptr;
  }
  if (!session || !fact_type || !out_stream) {
    set_error("Session, fact type, or output stream is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  try {
    auto *session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    auto type_status = require_schema_imported_fact_type(*session_wrapper->session, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    handle = std::make_unique<ruleforge_data_bind_stream_handle_s>();
    handle->session_wrapper = session_wrapper;
    auto const kb = session_wrapper->session->get_knowledge_base();
    handle->codec = kb ? get_kb_data_bind_or_set_error(*kb, fact_type)
                       : DataBindView{};
    handle->fact_type = fact_type;
    handle->multiple = multiple;
    if (!handle->codec) {
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    return RULES_FORGE_OK;
  } catch (std::bad_alloc const &) {
    set_error("Failed to allocate DataBind stream");
    return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
  } catch (std::exception const &e) {
    set_error_fmt("Failed to create DataBind stream: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

static ruleforge_status_t publish_data_bind_stream(
    std::unique_ptr<ruleforge_data_bind_stream_handle_s> handle,
    data_bind_stream_t *raw_stream, ruleforge_data_bind_stream_t *out_stream) {
  handle->stream.reset(raw_stream);
  if (!handle->stream) {
    set_error_fmt("DataBind stream creation failed: ",
                  data_bind_error_detail(DATA_BIND_ERR_INVALID_ARG, handle->error));
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  ++handle->session_wrapper->active_data_bind_streams;
  *out_stream = handle.release();
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

static ruleforge_status_t reject_unusable_data_bind_stream(
    ruleforge_data_bind_stream_handle_s const *stream) {
  if (!stream) {
    set_error("DataBind stream handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (stream->failed || stream->finished) {
    set_error("DataBind stream is no longer usable");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  return RULES_FORGE_OK;
}

static ruleforge_status_t map_continuous_exception(std::exception const &e) {
  set_error(e.what());
  if (dynamic_cast<std::length_error const *>(&e)) {
    return RULES_FORGE_ERROR_RESOURCE_LIMIT;
  }
  if (dynamic_cast<std::bad_alloc const *>(&e)) {
    return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
  }
  if (dynamic_cast<std::invalid_argument const *>(&e)) {
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  return RULES_FORGE_ERROR_GENERIC;
}

static std::unique_ptr<ruleforge_continuous_result_handle_s>
prepare_continuous_result(ContinuousSessionWrapper const &wrapper) {
  auto handle = std::make_unique<ruleforge_continuous_result_handle_s>();
  handle->output_facts.reserve(wrapper.max_pending_results);
  return handle;
}

static ruleforge_status_t publish_continuous_result(
    std::unique_ptr<ruleforge_continuous_result_handle_s> handle,
    ContinuousStepResult result, ruleforge_continuous_result_t *out_result) {
  if (!out_result) {
    set_error("Output continuous result pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  for (auto &output : result.outputs) {
    Fact fact;
    fact.id = output.fact_id;
    fact.type = std::move(output.fact_type);
    fact.fields = std::move(output.fields);
    handle->output_facts.push_back(std::move(fact));
  }
  handle->result = std::move(result);
  *out_result = handle.release();
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

static std::shared_ptr<Fact> fact_from_data_bind_value(char const *fact_type,
                                                      DataBindValue const *value,
                                                      DataBind *codec) {
  if (!fact_type || !value || data_bind_value_kind(value) != DATA_BIND_VALUE_OBJECT) {
    throw std::invalid_argument("DataBind result is not an object fact");
  }
  auto fact = std::make_shared<Fact>();
  fact->type = fact_type;
  if (!populate_fact_from_data_bind_value(*fact, value, codec, fact_type)) {
    throw std::invalid_argument("Failed to convert DataBind result to fact");
  }
  return fact;
}

static ruleforge_status_t push_continuous_bound_value(
    ContinuousSessionWrapper *wrapper, char const *fact_type,
    char const *event_id, char const *entry_point, int64_t event_time_ms,
    DataBindValue const *value, ruleforge_continuous_result_t *out_result) {
  if (out_result) {
    *out_result = nullptr;
  }
  if (!wrapper || !wrapper->session || !fact_type || !event_id || !entry_point || !out_result) {
    set_error("Continuous session, event metadata, or output result is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto fact = fact_from_data_bind_value(
        fact_type, value, wrapper->kb->find_data_bind_codec(fact_type));
    auto result_handle = prepare_continuous_result(*wrapper);
    EventEnvelope event{event_id, entry_point, event_time_ms, std::move(fact)};
    return publish_continuous_result(
        std::move(result_handle), wrapper->session->push(std::move(event)), out_result);
  } catch (std::exception const &e) {
    return map_continuous_exception(e);
  }
}

static ruleforge_status_t push_continuous_bound_object(
    ContinuousSessionWrapper *wrapper, DataBindObject const *object,
    char const *event_id, char const *entry_point, int64_t event_time_ms,
    ruleforge_continuous_result_t *out_result) {
  if (!object) {
    if (out_result) {
      *out_result = nullptr;
    }
    set_error("DataBindObject is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  return push_continuous_bound_value(
      wrapper, data_bind_object_type_name(object), event_id, entry_point,
      event_time_ms, data_bind_object_value(object), out_result);
}

static ruleforge_status_t push_continuous_bound_list(
    ContinuousSessionWrapper *wrapper, char const *fact_type,
    char const *event_id_field, char const *event_time_field,
    char const *entry_point, DataBindValue const *value,
    ruleforge_continuous_result_t *out_result) {
  if (out_result) {
    *out_result = nullptr;
  }
  if (!wrapper || !wrapper->session || !fact_type || !event_id_field
      || !event_time_field || !entry_point || !value || !out_result) {
    set_error("Continuous event batch arguments are invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (data_bind_value_kind(value) != DATA_BIND_VALUE_LIST) {
    set_error("DataBind continuous batch result is not a list");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  try {
    size_t const count = data_bind_value_count(value);
    std::vector<EventEnvelope> events;
    events.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      DataBindValue const *item = data_bind_value_at(value, i);
      if (!item || data_bind_value_kind(item) != DATA_BIND_VALUE_OBJECT) {
        throw std::invalid_argument("DataBind continuous batch item is not an object");
      }
      DataBindValue const *id_value = data_bind_value_get(item, event_id_field);
      DataBindValue const *time_value = data_bind_value_get(item, event_time_field);
      if (!id_value || data_bind_value_kind(id_value) != DATA_BIND_VALUE_STRING) {
        throw std::invalid_argument("Continuous event ID field must be a string");
      }
      char const *event_id = data_bind_value_as_string(id_value);
      if (!event_id || event_id[0] == '\0') {
        throw std::invalid_argument("Continuous event ID field cannot be empty");
      }
      int64_t event_time_ms = 0;
      if (time_value && data_bind_value_kind(time_value) == DATA_BIND_VALUE_INT) {
        event_time_ms = data_bind_value_as_int(time_value);
      } else if (time_value && data_bind_value_kind(time_value) == DATA_BIND_VALUE_INT64) {
        event_time_ms = data_bind_value_as_int64(time_value);
      } else {
        throw std::invalid_argument("Continuous event time field must be int or int64");
      }
      events.push_back(EventEnvelope{
          event_id, entry_point, event_time_ms,
          fact_from_data_bind_value(
              fact_type, item, wrapper->kb->find_data_bind_codec(fact_type))});
    }

    auto result_handle = prepare_continuous_result(*wrapper);
    return publish_continuous_result(
        std::move(result_handle), wrapper->session->push_batch(events), out_result);
  } catch (std::exception const &e) {
    return map_continuous_exception(e);
  }
}

static EventEnvelope continuous_event_from_bound_value(
    char const *fact_type, char const *event_id_field,
    char const *event_time_field, char const *entry_point,
    DataBindValue const *value, DataBind *codec) {
  if (!value || data_bind_value_kind(value) != DATA_BIND_VALUE_OBJECT) {
    throw std::invalid_argument("DataBind continuous event is not an object");
  }
  DataBindValue const *id_value = data_bind_value_get(value, event_id_field);
  DataBindValue const *time_value = data_bind_value_get(value, event_time_field);
  if (!id_value || data_bind_value_kind(id_value) != DATA_BIND_VALUE_STRING) {
    throw std::invalid_argument("Continuous event ID field must be a string");
  }
  char const *event_id = data_bind_value_as_string(id_value);
  if (!event_id || event_id[0] == '\0') {
    throw std::invalid_argument("Continuous event ID field cannot be empty");
  }
  int64_t event_time_ms = 0;
  if (time_value && data_bind_value_kind(time_value) == DATA_BIND_VALUE_INT) {
    event_time_ms = data_bind_value_as_int(time_value);
  } else if (time_value && data_bind_value_kind(time_value) == DATA_BIND_VALUE_INT64) {
    event_time_ms = data_bind_value_as_int64(time_value);
  } else {
    throw std::invalid_argument("Continuous event time field must be int or int64");
  }
  return EventEnvelope{event_id, entry_point, event_time_ms,
                       fact_from_data_bind_value(fact_type, value, codec)};
}

static DataBindRecordAction collect_continuous_stream_record(
    void *user_data, DataBindValue const *record, uint64_t record_index) {
  auto *handle = static_cast<ruleforge_continuous_data_bind_stream_handle_s *>(user_data);
  if (!handle || record_index != handle->pending_events.size()) {
    return DATA_BIND_RECORD_ERROR;
  }
  try {
    handle->pending_events.push_back(continuous_event_from_bound_value(
        handle->fact_type.c_str(), handle->event_id_field.c_str(),
        handle->event_time_field.c_str(), handle->entry_point.c_str(), record,
        handle->codec.get()));
    return DATA_BIND_RECORD_CONTINUE;
  } catch (std::exception const &e) {
    handle->callback_error = e.what();
    return DATA_BIND_RECORD_ERROR;
  } catch (...) {
    handle->callback_error = "Failed to convert DataBind streaming record";
    return DATA_BIND_RECORD_ERROR;
  }
}

static ruleforge_status_t validate_continuous_path_arguments(
    ruleforge_continuous_session_t session, char const *fact_type,
    char const *source, char const *path,
    char const *event_id_field, char const *event_time_field,
    char const *entry_point, ruleforge_continuous_result_t *out_result) {
  if (out_result) {
    *out_result = nullptr;
  }
  if (!session || !fact_type || !source || !path || path[0] == '\0'
      || !event_id_field || event_id_field[0] == '\0'
      || !event_time_field || event_time_field[0] == '\0'
      || !entry_point || entry_point[0] == '\0' || !out_result) {
    set_error("Continuous path input arguments are invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
  return require_schema_imported_fact_type(*wrapper->kb, fact_type);
}

static ruleforge_status_t prepare_continuous_path_stream(
    ruleforge_continuous_session_t session, char const *fact_type,
    char const *path, char const *event_id_field,
    char const *event_time_field, char const *entry_point,
    ruleforge_continuous_data_bind_stream_t *out_stream,
    std::unique_ptr<ruleforge_continuous_data_bind_stream_handle_s> &handle) {
  if (out_stream) {
    *out_stream = nullptr;
  }
  if (!session || !fact_type || !path || path[0] == '\0'
      || !event_id_field || event_id_field[0] == '\0'
      || !event_time_field || event_time_field[0] == '\0'
      || !entry_point || entry_point[0] == '\0' || !out_stream) {
    set_error("Continuous path stream arguments are invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
    auto type_status = require_schema_imported_fact_type(*wrapper->kb, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    handle = std::make_unique<ruleforge_continuous_data_bind_stream_handle_s>();
    handle->session_wrapper = wrapper;
    handle->codec = get_kb_data_bind_or_set_error(*wrapper->kb, fact_type);
    handle->fact_type = fact_type;
    handle->event_id_field = event_id_field;
    handle->event_time_field = event_time_field;
    handle->entry_point = entry_point;
    handle->multiple = true;
    return handle->codec ? RULES_FORGE_OK : RULES_FORGE_ERROR_INVALID_ARGUMENT;
  } catch (std::exception const &e) {
    return map_continuous_exception(e);
  }
}

static ruleforge_status_t publish_continuous_path_stream(
    std::unique_ptr<ruleforge_continuous_data_bind_stream_handle_s> handle,
    data_bind_stream_t *raw_stream,
    ruleforge_continuous_data_bind_stream_t *out_stream) {
  handle->stream.reset(raw_stream);
  if (!handle->stream) {
    set_error_fmt("Continuous DataBind stream creation failed: ",
                  data_bind_error_detail(DATA_BIND_ERR_INVALID_ARG, handle->error));
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindStatus callback_status = data_bind_stream_set_record_callback(
      handle->stream.get(), collect_continuous_stream_record, handle.get());
  if (callback_status != DATA_BIND_OK) {
    set_error_fmt("Continuous DataBind callback setup failed: ",
                  data_bind_error_detail(callback_status, handle->error));
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  ++handle->session_wrapper->active_data_bind_streams;
  *out_stream = handle.release();
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

} // namespace

ruleforge_status_t ruleforge_data_bind_object_from_binary(
    char const *schema_path, char const *fact_type, uint8_t const *data,
    size_t len, ruleforge_data_bind_object_t *out_object) {
  if (out_object) {
    *out_object = nullptr;
  }
  if (!schema_path || !fact_type || !data || len == 0 || !out_object) {
    set_error("Schema path, fact type, binary input, or output object is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  auto codec = create_data_bind_or_set_error(schema_path);
  if (!codec) {
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindObject *raw_object = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status = data_bind_object_from_bin(
      codec.get(), fact_type, data, len, &raw_object, &error);
  DataBindObjectHandle object(raw_object);
  if (status != DATA_BIND_OK) {
    return map_data_bind_object_error(
        "DataBind binary object parse failed: ", status, error);
  }
  return publish_data_bind_object(std::move(codec), std::move(object), out_object);
}

ruleforge_status_t ruleforge_data_bind_object_from_json(
    char const *schema_path, char const *fact_type, char const *json,
    size_t len, ruleforge_data_bind_object_t *out_object) {
  return create_text_data_bind_object(
      schema_path, fact_type, json, len, out_object,
      data_bind_object_from_json, "DataBind JSON object parse failed: ");
}

ruleforge_status_t ruleforge_data_bind_object_from_yaml(
    char const *schema_path, char const *fact_type, char const *yaml,
    size_t len, ruleforge_data_bind_object_t *out_object) {
  return create_text_data_bind_object(
      schema_path, fact_type, yaml, len, out_object,
      data_bind_object_from_yaml, "DataBind YAML object parse failed: ");
}

ruleforge_status_t ruleforge_data_bind_object_from_xml(
    char const *schema_path, char const *fact_type, char const *xml,
    size_t len, ruleforge_data_bind_object_t *out_object) {
  return create_text_data_bind_object(
      schema_path, fact_type, xml, len, out_object,
      data_bind_object_from_xml, "DataBind XML object parse failed: ");
}

ruleforge_status_t ruleforge_data_bind_object_from_csv(
    char const *schema_path, char const *fact_type, char const *csv,
    size_t len, size_t row, ruleforge_data_bind_object_t *out_object) {
  if (out_object) {
    *out_object = nullptr;
  }
  if (!schema_path || !fact_type || !csv || !out_object) {
    set_error("Schema path, fact type, CSV input, or output object is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  auto codec = create_data_bind_or_set_error(schema_path);
  if (!codec) {
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindObject *raw_object = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status = data_bind_object_from_csv(
      codec.get(), fact_type, csv, len, row, &raw_object, &error);
  DataBindObjectHandle object(raw_object);
  if (status != DATA_BIND_OK) {
    return map_data_bind_object_error(
        "DataBind CSV object parse failed: ", status, error);
  }
  return publish_data_bind_object(std::move(codec), std::move(object), out_object);
}

ruleforge_status_t ruleforge_data_bind_object_clone(
    ruleforge_data_bind_object_t object,
    ruleforge_data_bind_object_t *out_object) {
  if (out_object) {
    *out_object = nullptr;
  }
  auto *handle = reinterpret_cast<ruleforge_data_bind_object_handle_s *>(object);
  if (!handle || !handle->codec || !handle->object || !out_object) {
    set_error("DataBind codec, object, or clone output pointer is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindObject *raw_copy = nullptr;
  DataBindStatus status = data_bind_object_clone(handle->object.get(), &raw_copy);
  DataBindObjectHandle copy(raw_copy);
  if (status != DATA_BIND_OK) {
    if (status == DATA_BIND_ERR_OOM) {
      set_error("Failed to allocate DataBindObject clone");
      return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
    }
    set_error("Failed to clone DataBindObject");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  return publish_data_bind_object(handle->codec, std::move(copy), out_object);
}

char const *ruleforge_data_bind_object_get_type_name(
    ruleforge_data_bind_object_t object) {
  auto *handle = reinterpret_cast<ruleforge_data_bind_object_handle_s *>(object);
  if (!handle || !handle->object) {
    set_error("DataBindObject handle is NULL");
    return nullptr;
  }
  char const *type_name = data_bind_object_type_name(handle->object.get());
  if (!type_name) {
    set_error("DataBindObject type name is unavailable");
    return nullptr;
  }
  last_error[0] = '\0';
  return type_name;
}

ruleforge_status_t ruleforge_data_bind_object_serialize_json(
    ruleforge_data_bind_object_t object, char **out_json, size_t *out_len) {
  return serialize_data_bind_object(
      object, out_json, out_len, data_bind_object_serialize_json,
      "DataBind JSON object serialization failed: ");
}

ruleforge_status_t ruleforge_data_bind_object_serialize_yaml(
    ruleforge_data_bind_object_t object, char **out_yaml, size_t *out_len) {
  return serialize_data_bind_object(
      object, out_yaml, out_len, data_bind_object_serialize_yaml,
      "DataBind YAML object serialization failed: ");
}

ruleforge_status_t ruleforge_data_bind_object_serialize_xml(
    ruleforge_data_bind_object_t object, char **out_xml, size_t *out_len) {
  return serialize_data_bind_object(
      object, out_xml, out_len, data_bind_object_serialize_xml,
      "DataBind XML object serialization failed: ");
}

ruleforge_status_t ruleforge_data_bind_object_serialize_csv(
    ruleforge_data_bind_object_t object, char **out_csv, size_t *out_len) {
  return serialize_data_bind_object(
      object, out_csv, out_len, data_bind_object_serialize_csv,
      "DataBind CSV object serialization failed: ");
}

ruleforge_status_t ruleforge_data_bind_object_serialize_binary(
    ruleforge_data_bind_object_t object, uint8_t **out_binary, size_t *out_len) {
  if (out_binary) {
    *out_binary = nullptr;
  }
  auto *handle = reinterpret_cast<ruleforge_data_bind_object_handle_s *>(object);
  if (!handle || !handle->codec || !handle->object || !out_binary || !out_len) {
    set_error("DataBind codec, object, or binary output is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status = data_bind_object_serialize_bin(
      handle->codec.get(), handle->object.get(), out_binary, out_len, &error);
  if (status != DATA_BIND_OK) {
    return map_data_bind_object_error(
        "DataBind binary object serialization failed: ", status, error);
  }
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

ruleforge_status_t ruleforge_data_bind_object_write_json(
    ruleforge_data_bind_object_t object, ruleforge_write_fn write, void *user) {
  return write_data_bind_object(
      object, write, user, data_bind_object_write_json,
      "DataBind JSON object write failed: ");
}

ruleforge_status_t ruleforge_data_bind_object_write_yaml(
    ruleforge_data_bind_object_t object, ruleforge_write_fn write, void *user) {
  return write_data_bind_object(
      object, write, user, data_bind_object_write_yaml,
      "DataBind YAML object write failed: ");
}

ruleforge_status_t ruleforge_data_bind_object_write_xml(
    ruleforge_data_bind_object_t object, ruleforge_write_fn write, void *user) {
  return write_data_bind_object(
      object, write, user, data_bind_object_write_xml,
      "DataBind XML object write failed: ");
}

ruleforge_status_t ruleforge_data_bind_object_write_csv(
    ruleforge_data_bind_object_t object, ruleforge_write_fn write, void *user) {
  return write_data_bind_object(
      object, write, user, data_bind_object_write_csv,
      "DataBind CSV object write failed: ");
}

void ruleforge_data_bind_serialized_free(char *data) {
  data_bind_serialized_free(data);
}

void ruleforge_data_bind_binary_free(void *data) {
  data_bind_binary_free(data);
}

ruleforge_status_t ruleforge_data_bind_object_destroy(
    ruleforge_data_bind_object_t object) {
  if (!object) {
    set_error("DataBindObject handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  delete reinterpret_cast<ruleforge_data_bind_object_handle_s *>(object);
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

ruleforge_status_t ruleforge_session_create(ruleforge_knowledge_base_t kb,
                                            ruleforge_stateful_session_t *out_session) {
  if (!kb) {
    set_error("Knowledge Base handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (!out_session) {
    set_error("Output Session pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);
    if (!kb_wrapper->kb) {
      set_error("Knowledge Base is not initialized");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    auto session_wrapper = new StatefulSessionWrapper();
    session_wrapper->session = kb_wrapper->kb->create_session();
    if (!session_wrapper->session) {
      delete session_wrapper;
      set_error("Failed to create session from Knowledge Base");
      return RULES_FORGE_ERROR_SESSION_CREATION_FAILED;
    }
    *out_session = reinterpret_cast<ruleforge_stateful_session_t>(session_wrapper);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to create session: ", e.what());
    return RULES_FORGE_ERROR_SESSION_CREATION_FAILED;
  }
}

ruleforge_status_t ruleforge_continuous_config_init(
    ruleforge_continuous_config_t *config) {
  if (!config) {
    set_error("Continuous config pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  ContinuousSessionConfig defaults;
  *config = {};
  config->abi_version = RULEFORGE_CONTINUOUS_CONFIG_ABI_V1;
  config->struct_size = sizeof(*config);
  config->max_active_events = defaults.max_active_events;
  config->max_dedup_entries = defaults.max_dedup_entries;
  config->max_pending_result_batches = defaults.max_pending_result_batches;
  config->max_pending_results = defaults.max_pending_results;
  config->max_input_batch_size = defaults.max_input_batch_size;
  config->max_replay_steps = defaults.max_replay_steps;
  config->max_rules_per_step = defaults.max_rules_per_step;
  config->allowed_lateness_ms = defaults.allowed_lateness_ms;
  config->event_retention_ms = defaults.event_retention_ms;
  config->dedup_retention_ms = defaults.dedup_retention_ms;
  config->max_event_time_lead_ms = defaults.max_event_time_lead_ms;
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

ruleforge_status_t ruleforge_continuous_session_create(
    ruleforge_knowledge_base_t kb, ruleforge_continuous_config_t const *config,
    ruleforge_continuous_session_t *out_session) {
  if (out_session) {
    *out_session = nullptr;
  }
  if (!kb || !config || !out_session) {
    set_error("Knowledge Base, continuous config, or output session is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (config->abi_version != RULEFORGE_CONTINUOUS_CONFIG_ABI_V1
      || config->struct_size < sizeof(*config)) {
    set_error("Unsupported continuous config ABI version or structure size");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (config->output_fact_type_count != 0 && !config->output_fact_types) {
    set_error("Continuous output fact type array is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  try {
    auto *kb_wrapper = reinterpret_cast<KnowledgeBaseWrapper *>(kb);
    if (!kb_wrapper->kb) {
      set_error("Knowledge Base is not initialized");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    ContinuousSessionConfig cpp_config;
    cpp_config.max_active_events = config->max_active_events;
    cpp_config.max_dedup_entries = config->max_dedup_entries;
    cpp_config.max_pending_result_batches = config->max_pending_result_batches;
    cpp_config.max_pending_results = config->max_pending_results;
    cpp_config.max_input_batch_size = config->max_input_batch_size;
    cpp_config.max_replay_steps = config->max_replay_steps;
    cpp_config.max_rules_per_step = config->max_rules_per_step;
    cpp_config.allowed_lateness_ms = config->allowed_lateness_ms;
    cpp_config.event_retention_ms = config->event_retention_ms;
    cpp_config.dedup_retention_ms = config->dedup_retention_ms;
    cpp_config.max_event_time_lead_ms = config->max_event_time_lead_ms;
    cpp_config.output_fact_types.reserve(config->output_fact_type_count);
    for (size_t i = 0; i < config->output_fact_type_count; ++i) {
      if (!config->output_fact_types[i]) {
        set_error("Continuous output fact type contains NULL");
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
      }
      cpp_config.output_fact_types.emplace_back(config->output_fact_types[i]);
    }

    auto wrapper = std::make_unique<ContinuousSessionWrapper>();
    wrapper->kb = kb_wrapper->kb;
    wrapper->max_pending_results = cpp_config.max_pending_results;
    wrapper->session = std::make_unique<ContinuousSession>(wrapper->kb, std::move(cpp_config));
    *out_session = reinterpret_cast<ruleforge_continuous_session_t>(wrapper.release());
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (std::exception const &e) {
    return map_continuous_exception(e);
  }
}

ruleforge_status_t ruleforge_continuous_session_destroy(
    ruleforge_continuous_session_t session) {
  if (!session) {
    set_error("Continuous session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
  if (wrapper->active_data_bind_streams != 0) {
    set_error("Cannot destroy continuous session while DataBind streams are active");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  delete wrapper;
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

ruleforge_status_t ruleforge_continuous_push_data_bind_object(
    ruleforge_continuous_session_t session,
    ruleforge_data_bind_object_t object, char const *event_id,
    char const *entry_point, int64_t event_time_ms,
    ruleforge_continuous_result_t *out_result) {
  if (out_result) {
    *out_result = nullptr;
  }
  auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
  auto *object_handle =
      reinterpret_cast<ruleforge_data_bind_object_handle_s *>(object);
  if (!wrapper || !wrapper->session || !object_handle || !object_handle->object
      || !event_id || !entry_point || !out_result) {
    set_error("Continuous session, DataBindObject, event metadata, or output result is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  char const *fact_type = data_bind_object_type_name(object_handle->object.get());
  DataBindValue const *value = data_bind_object_value(object_handle->object.get());
  if (!fact_type || !value) {
    set_error("DataBindObject type name or value is unavailable");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  auto type_status = require_schema_imported_fact_type(*wrapper->kb, fact_type);
  if (type_status != RULES_FORGE_OK) {
    return type_status;
  }
  return push_continuous_bound_object(
      wrapper, object_handle->object.get(), event_id, entry_point,
      event_time_ms, out_result);
}

ruleforge_status_t ruleforge_continuous_push_json(
    ruleforge_continuous_session_t session, char const *fact_type,
    char const *event_id, char const *entry_point,
    int64_t event_time_ms, char const *fact_json,
    ruleforge_continuous_result_t *out_result) {
  if (out_result) {
    *out_result = nullptr;
  }
  if (!session || !fact_type || !event_id || !entry_point
      || !fact_json || !out_result) {
    set_error("Continuous JSON event arguments are invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
  auto type_status = require_schema_imported_fact_type(*wrapper->kb, fact_type);
  if (type_status != RULES_FORGE_OK) {
    return type_status;
  }
  auto codec = get_kb_data_bind_or_set_error(*wrapper->kb, fact_type);
  if (!codec) {
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindObject *raw_object = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus bind_status = data_bind_object_from_json(
      codec.get(), fact_type, fact_json, std::strlen(fact_json),
      &raw_object, &error);
  DataBindObjectHandle object(raw_object);
  if (bind_status != DATA_BIND_OK) {
    set_error_fmt("DataBind JSON parse failed: ", data_bind_error_detail(bind_status, error));
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  return push_continuous_bound_object(
      wrapper, object.get(), event_id, entry_point, event_time_ms, out_result);
}

ruleforge_status_t ruleforge_continuous_push_yaml(
    ruleforge_continuous_session_t session, char const *fact_type,
    char const *event_id, char const *entry_point,
    int64_t event_time_ms, char const *fact_yaml,
    ruleforge_continuous_result_t *out_result) {
  if (out_result) {
    *out_result = nullptr;
  }
  if (!session || !fact_type || !event_id || !entry_point
      || !fact_yaml || !out_result) {
    set_error("Continuous YAML event arguments are invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
  auto type_status = require_schema_imported_fact_type(*wrapper->kb, fact_type);
  if (type_status != RULES_FORGE_OK) {
    return type_status;
  }
  auto codec = get_kb_data_bind_or_set_error(*wrapper->kb, fact_type);
  if (!codec) {
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindObject *raw_object = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus bind_status = data_bind_object_from_yaml(
      codec.get(), fact_type, fact_yaml, std::strlen(fact_yaml),
      &raw_object, &error);
  DataBindObjectHandle object(raw_object);
  if (bind_status != DATA_BIND_OK) {
    set_error_fmt("DataBind YAML parse failed: ", data_bind_error_detail(bind_status, error));
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  return push_continuous_bound_object(
      wrapper, object.get(), event_id, entry_point, event_time_ms, out_result);
}

ruleforge_status_t ruleforge_continuous_push_json_path(
    ruleforge_continuous_session_t session, char const *fact_type,
    char const *json_source, char const *json_path,
    char const *event_id_field, char const *event_time_field,
    char const *entry_point, ruleforge_continuous_result_t *out_result) {
  auto validation = validate_continuous_path_arguments(
      session, fact_type, json_source, json_path, event_id_field,
      event_time_field, entry_point, out_result);
  if (validation != RULES_FORGE_OK) {
    return validation;
  }
  auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
  auto codec = get_kb_data_bind_or_set_error(*wrapper->kb, fact_type);
  if (!codec) {
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindValue *raw_value = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status = data_bind_parse_json_path_all(
      codec.get(), fact_type, json_source, std::strlen(json_source), json_path,
      &raw_value, &error);
  DataBindValueHandle value(raw_value);
  if (status != DATA_BIND_OK) {
    set_error_fmt("Continuous DataBind JSON path parse failed: ",
                  data_bind_error_detail(status, error));
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  return push_continuous_bound_list(wrapper, fact_type, event_id_field,
                                    event_time_field, entry_point, value.get(), out_result);
}

ruleforge_status_t ruleforge_continuous_push_yaml_path(
    ruleforge_continuous_session_t session, char const *fact_type,
    char const *yaml_source, char const *yaml_path,
    char const *event_id_field, char const *event_time_field,
    char const *entry_point, ruleforge_continuous_result_t *out_result) {
  auto validation = validate_continuous_path_arguments(
      session, fact_type, yaml_source, yaml_path, event_id_field,
      event_time_field, entry_point, out_result);
  if (validation != RULES_FORGE_OK) {
    return validation;
  }
  auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
  auto codec = get_kb_data_bind_or_set_error(*wrapper->kb, fact_type);
  if (!codec) {
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindValue *raw_value = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status = data_bind_parse_yaml_path_all(
      codec.get(), fact_type, yaml_source, std::strlen(yaml_source), yaml_path,
      &raw_value, &error);
  DataBindValueHandle value(raw_value);
  if (status != DATA_BIND_OK) {
    set_error_fmt("Continuous DataBind YAML path parse failed: ",
                  data_bind_error_detail(status, error));
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  return push_continuous_bound_list(wrapper, fact_type, event_id_field,
                                    event_time_field, entry_point, value.get(), out_result);
}

ruleforge_status_t ruleforge_continuous_push_csv_path(
    ruleforge_continuous_session_t session, char const *fact_type,
    char const *csv_source, char const *csv_path,
    char const *event_id_field, char const *event_time_field,
    char const *entry_point, ruleforge_continuous_result_t *out_result) {
  auto validation = validate_continuous_path_arguments(
      session, fact_type, csv_source, csv_path, event_id_field,
      event_time_field, entry_point, out_result);
  if (validation != RULES_FORGE_OK) {
    return validation;
  }
  auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
  auto codec = get_kb_data_bind_or_set_error(*wrapper->kb, fact_type);
  if (!codec) {
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindValue *raw_value = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status = data_bind_parse_csv_path(
      codec.get(), fact_type, csv_source, std::strlen(csv_source), csv_path,
      &raw_value, &error);
  DataBindValueHandle value(raw_value);
  if (status != DATA_BIND_OK) {
    set_error_fmt("Continuous DataBind CSV path parse failed: ",
                  data_bind_error_detail(status, error));
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  return push_continuous_bound_list(wrapper, fact_type, event_id_field,
                                    event_time_field, entry_point, value.get(), out_result);
}

ruleforge_status_t ruleforge_continuous_push_xml_path(
    ruleforge_continuous_session_t session, char const *fact_type,
    char const *xml_source, char const *xml_path,
    char const *event_id_field, char const *event_time_field,
    char const *entry_point, ruleforge_continuous_result_t *out_result) {
  auto validation = validate_continuous_path_arguments(
      session, fact_type, xml_source, xml_path, event_id_field,
      event_time_field, entry_point, out_result);
  if (validation != RULES_FORGE_OK) {
    return validation;
  }
  auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
  auto codec = get_kb_data_bind_or_set_error(*wrapper->kb, fact_type);
  if (!codec) {
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindValue *raw_value = nullptr;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status = data_bind_parse_xml_path_all(
      codec.get(), fact_type, xml_source, std::strlen(xml_source), xml_path,
      &raw_value, &error);
  DataBindValueHandle value(raw_value);
  if (status != DATA_BIND_OK) {
    set_error_fmt("Continuous DataBind XML path parse failed: ",
                  data_bind_error_detail(status, error));
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  return push_continuous_bound_list(wrapper, fact_type, event_id_field,
                                    event_time_field, entry_point, value.get(), out_result);
}

ruleforge_status_t ruleforge_continuous_advance_watermark(
    ruleforge_continuous_session_t session, int64_t watermark_ms,
    ruleforge_continuous_result_t *out_result) {
  if (out_result) {
    *out_result = nullptr;
  }
  if (!session || !out_result) {
    set_error("Continuous session or output result is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
    auto result_handle = prepare_continuous_result(*wrapper);
    return publish_continuous_result(
        std::move(result_handle), wrapper->session->advance_watermark(watermark_ms), out_result);
  } catch (std::exception const &e) {
    return map_continuous_exception(e);
  }
}

ruleforge_status_t ruleforge_continuous_drain(
    ruleforge_continuous_session_t session, ruleforge_continuous_result_t *out_result) {
  if (out_result) {
    *out_result = nullptr;
  }
  if (!session || !out_result) {
    set_error("Continuous session or output result is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
    auto result_handle = prepare_continuous_result(*wrapper);
    return publish_continuous_result(
        std::move(result_handle), wrapper->session->drain(), out_result);
  } catch (std::exception const &e) {
    return map_continuous_exception(e);
  }
}

ruleforge_status_t ruleforge_continuous_acknowledge(
    ruleforge_continuous_session_t session, uint64_t batch_id) {
  if (!session) {
    set_error("Continuous session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
    wrapper->session->acknowledge(batch_id);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (std::exception const &e) {
    return map_continuous_exception(e);
  }
}

ruleforge_status_t ruleforge_continuous_get_metrics(
    ruleforge_continuous_session_t session,
    ruleforge_continuous_metrics_t *out_metrics) {
  if (!session || !out_metrics) {
    set_error("Continuous session or output metrics is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
  auto metrics = wrapper->session->metrics();
  *out_metrics = {metrics.accepted_events, metrics.expired_events,
                  metrics.rejected_duplicates, metrics.rejected_late_events,
                  metrics.rejected_resource_limits, metrics.replay_recoveries,
                  metrics.active_events, metrics.dedup_entries,
                  metrics.pending_result_batches, metrics.pending_results};
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

ruleforge_status_t ruleforge_continuous_data_bind_stream_json_create(
    ruleforge_continuous_session_t session, char const *fact_type,
    char const *event_id, char const *entry_point,
    int64_t event_time_ms, ruleforge_continuous_data_bind_stream_t *out_stream) {
  if (out_stream) {
    *out_stream = nullptr;
  }
  if (!session || !fact_type || !event_id || !entry_point || !out_stream) {
    set_error("Continuous DataBind stream arguments are invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
    auto type_status = require_schema_imported_fact_type(*wrapper->kb, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    auto handle = std::make_unique<ruleforge_continuous_data_bind_stream_handle_s>();
    handle->session_wrapper = wrapper;
    handle->codec = get_kb_data_bind_or_set_error(*wrapper->kb, fact_type);
    handle->fact_type = fact_type;
    handle->event_id = event_id;
    handle->entry_point = entry_point;
    handle->event_time_ms = event_time_ms;
    if (!handle->codec) {
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    handle->stream.reset(data_bind_stream_json_create(
        handle->codec.get(), fact_type, &handle->output_value, &handle->error));
    if (!handle->stream) {
      set_error_fmt("DataBind stream creation failed: ",
                    data_bind_error_detail(DATA_BIND_ERR_INVALID_ARG, handle->error));
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    ++wrapper->active_data_bind_streams;
    *out_stream = handle.release();
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (std::exception const &e) {
    return map_continuous_exception(e);
  }
}

ruleforge_status_t ruleforge_continuous_data_bind_stream_yaml_create(
    ruleforge_continuous_session_t session, char const *fact_type,
    char const *event_id, char const *entry_point,
    int64_t event_time_ms, ruleforge_continuous_data_bind_stream_t *out_stream) {
  if (out_stream) {
    *out_stream = nullptr;
  }
  if (!session || !fact_type || !event_id || !entry_point || !out_stream) {
    set_error("Continuous DataBind stream arguments are invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto *wrapper = reinterpret_cast<ContinuousSessionWrapper *>(session);
    auto type_status = require_schema_imported_fact_type(*wrapper->kb, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    auto handle = std::make_unique<ruleforge_continuous_data_bind_stream_handle_s>();
    handle->session_wrapper = wrapper;
    handle->codec = get_kb_data_bind_or_set_error(*wrapper->kb, fact_type);
    handle->fact_type = fact_type;
    handle->event_id = event_id;
    handle->entry_point = entry_point;
    handle->event_time_ms = event_time_ms;
    if (!handle->codec) {
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    handle->stream.reset(data_bind_stream_yaml_create(
        handle->codec.get(), fact_type, &handle->output_value, &handle->error));
    if (!handle->stream) {
      set_error_fmt("DataBind stream creation failed: ",
                    data_bind_error_detail(DATA_BIND_ERR_INVALID_ARG, handle->error));
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    ++wrapper->active_data_bind_streams;
    *out_stream = handle.release();
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (std::exception const &e) {
    return map_continuous_exception(e);
  }
}

ruleforge_status_t ruleforge_continuous_data_bind_stream_json_path_create(
    ruleforge_continuous_session_t session, char const *fact_type,
    char const *json_path, char const *event_id_field,
    char const *event_time_field, char const *entry_point,
    ruleforge_continuous_data_bind_stream_t *out_stream) {
  std::unique_ptr<ruleforge_continuous_data_bind_stream_handle_s> handle;
  auto status = prepare_continuous_path_stream(
      session, fact_type, json_path, event_id_field,
      event_time_field, entry_point, out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_json_path_all_create(
      handle->codec.get(), fact_type, json_path, &handle->output_value, &handle->error);
  return publish_continuous_path_stream(std::move(handle), stream, out_stream);
}

ruleforge_status_t ruleforge_continuous_data_bind_stream_yaml_path_create(
    ruleforge_continuous_session_t session, char const *fact_type,
    char const *yaml_path, char const *event_id_field,
    char const *event_time_field, char const *entry_point,
    ruleforge_continuous_data_bind_stream_t *out_stream) {
  std::unique_ptr<ruleforge_continuous_data_bind_stream_handle_s> handle;
  auto status = prepare_continuous_path_stream(
      session, fact_type, yaml_path, event_id_field,
      event_time_field, entry_point, out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_yaml_path_all_create(
      handle->codec.get(), fact_type, yaml_path, &handle->output_value, &handle->error);
  return publish_continuous_path_stream(std::move(handle), stream, out_stream);
}

ruleforge_status_t ruleforge_continuous_data_bind_stream_csv_path_create(
    ruleforge_continuous_session_t session, char const *fact_type,
    char const *csv_path, char const *event_id_field,
    char const *event_time_field, char const *entry_point,
    ruleforge_continuous_data_bind_stream_t *out_stream) {
  std::unique_ptr<ruleforge_continuous_data_bind_stream_handle_s> handle;
  auto status = prepare_continuous_path_stream(
      session, fact_type, csv_path, event_id_field,
      event_time_field, entry_point, out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_csv_path_create(
      handle->codec.get(), fact_type, csv_path, &handle->output_value, &handle->error);
  return publish_continuous_path_stream(std::move(handle), stream, out_stream);
}

ruleforge_status_t ruleforge_continuous_data_bind_stream_xml_path_create(
    ruleforge_continuous_session_t session, char const *fact_type,
    char const *xml_path, char const *event_id_field,
    char const *event_time_field, char const *entry_point,
    ruleforge_continuous_data_bind_stream_t *out_stream) {
  std::unique_ptr<ruleforge_continuous_data_bind_stream_handle_s> handle;
  auto status = prepare_continuous_path_stream(
      session, fact_type, xml_path, event_id_field,
      event_time_field, entry_point, out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_xml_path_all_create(
      handle->codec.get(), fact_type, xml_path, &handle->output_value, &handle->error);
  return publish_continuous_path_stream(std::move(handle), stream, out_stream);
}

static ruleforge_status_t reject_unusable_continuous_data_bind_stream(
    ruleforge_continuous_data_bind_stream_handle_s const *stream) {
  if (!stream) {
    set_error("Continuous DataBind stream handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (stream->failed || stream->finished) {
    set_error("Continuous DataBind stream is no longer usable");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  return RULES_FORGE_OK;
}

ruleforge_status_t ruleforge_continuous_data_bind_stream_feed(
    ruleforge_continuous_data_bind_stream_t stream, void const *data, size_t len) {
  auto *handle = reinterpret_cast<ruleforge_continuous_data_bind_stream_handle_s *>(stream);
  auto state_status = reject_unusable_continuous_data_bind_stream(handle);
  if (state_status != RULES_FORGE_OK) {
    return state_status;
  }
  if (!data && len != 0) {
    set_error("Continuous DataBind stream chunk is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindStatus status = static_cast<DataBindStatus>(
      data_bind_stream_feed(handle->stream.get(), data, len));
  if (status != DATA_BIND_OK) {
    handle->failed = true;
    handle->pending_events.clear();
    if (!handle->callback_error.empty()) {
      set_error_fmt("Continuous DataBind record callback failed: ",
                    handle->callback_error.c_str());
    } else {
      set_error_fmt("Continuous DataBind stream feed failed: ",
                    data_bind_error_detail(status, handle->error));
    }
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

ruleforge_status_t ruleforge_continuous_data_bind_stream_feed_file(
    ruleforge_continuous_data_bind_stream_t stream, char const *file_path) {
  auto *handle = reinterpret_cast<ruleforge_continuous_data_bind_stream_handle_s *>(stream);
  auto state_status = reject_unusable_continuous_data_bind_stream(handle);
  if (state_status != RULES_FORGE_OK) {
    return state_status;
  }
  if (!file_path || file_path[0] == '\0') {
    set_error("Continuous DataBind stream file path is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindStatus status = static_cast<DataBindStatus>(
      data_bind_stream_feed_file(handle->stream.get(), file_path));
  if (status != DATA_BIND_OK) {
    handle->failed = true;
    handle->pending_events.clear();
    set_error_fmt("Continuous DataBind stream file feed failed: ",
                  data_bind_error_detail(status, handle->error));
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

ruleforge_status_t ruleforge_continuous_data_bind_stream_finish(
    ruleforge_continuous_data_bind_stream_t stream,
    ruleforge_continuous_result_t *out_result) {
  if (out_result) {
    *out_result = nullptr;
  }
  auto *handle = reinterpret_cast<ruleforge_continuous_data_bind_stream_handle_s *>(stream);
  auto state_status = reject_unusable_continuous_data_bind_stream(handle);
  if (state_status != RULES_FORGE_OK) {
    return state_status;
  }
  if (!out_result) {
    set_error("Output continuous result pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindStatus bind_status = static_cast<DataBindStatus>(
      data_bind_stream_finish(handle->stream.get()));
  handle->finished = true;
  handle->value.reset(handle->output_value);
  handle->output_value = nullptr;
  if (bind_status != DATA_BIND_OK) {
    handle->failed = true;
    if (!handle->callback_error.empty()) {
      set_error_fmt("Continuous DataBind record callback failed: ",
                    handle->callback_error.c_str());
    } else {
      set_error_fmt("Continuous DataBind stream finish failed: ",
                    data_bind_error_detail(bind_status, handle->error));
    }
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (handle->multiple) {
    try {
      auto result_handle = prepare_continuous_result(*handle->session_wrapper);
      return publish_continuous_result(
          std::move(result_handle),
          handle->session_wrapper->session->push_batch(handle->pending_events),
          out_result);
    } catch (std::exception const &e) {
      return map_continuous_exception(e);
    }
  }
  return push_continuous_bound_value(
      handle->session_wrapper, handle->fact_type.c_str(), handle->event_id.c_str(),
      handle->entry_point.c_str(), handle->event_time_ms, handle->value.get(), out_result);
}

ruleforge_status_t ruleforge_continuous_data_bind_stream_destroy(
    ruleforge_continuous_data_bind_stream_t stream) {
  if (!stream) {
    set_error("Continuous DataBind stream handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  auto *handle = reinterpret_cast<ruleforge_continuous_data_bind_stream_handle_s *>(stream);
  if (handle->session_wrapper && handle->session_wrapper->active_data_bind_streams != 0) {
    --handle->session_wrapper->active_data_bind_streams;
  }
  delete handle;
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

ruleforge_continuous_step_status_t ruleforge_continuous_result_get_status(
    ruleforge_continuous_result_t result) {
  if (!result) {
    set_error("Continuous result handle is NULL");
    return RULES_FORGE_CONTINUOUS_COMMITTED;
  }
  auto *handle = reinterpret_cast<ruleforge_continuous_result_handle_s *>(result);
  last_error[0] = '\0';
  return handle->result.status == ContinuousStepStatus::DrainRequired
      ? RULES_FORGE_CONTINUOUS_DRAIN_REQUIRED : RULES_FORGE_CONTINUOUS_COMMITTED;
}

uint64_t ruleforge_continuous_result_get_batch_id(ruleforge_continuous_result_t result) {
  if (!result) {
    set_error("Continuous result handle is NULL");
    return 0;
  }
  auto *handle = reinterpret_cast<ruleforge_continuous_result_handle_s *>(result);
  last_error[0] = '\0';
  return handle->result.batch_id;
}

int ruleforge_continuous_result_get_rules_fired(ruleforge_continuous_result_t result) {
  if (!result) {
    set_error("Continuous result handle is NULL");
    return -1;
  }
  auto *handle = reinterpret_cast<ruleforge_continuous_result_handle_s *>(result);
  last_error[0] = '\0';
  return handle->result.rules_fired;
}

size_t ruleforge_continuous_result_get_events_expired(ruleforge_continuous_result_t result) {
  if (!result) {
    set_error("Continuous result handle is NULL");
    return 0;
  }
  auto *handle = reinterpret_cast<ruleforge_continuous_result_handle_s *>(result);
  last_error[0] = '\0';
  return handle->result.events_expired;
}

ruleforge_status_t ruleforge_continuous_result_get_watermark(
    ruleforge_continuous_result_t result, int64_t *out_watermark_ms,
    int *out_has_watermark) {
  if (!result || !out_watermark_ms || !out_has_watermark) {
    set_error("Continuous result or watermark output is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  auto *handle = reinterpret_cast<ruleforge_continuous_result_handle_s *>(result);
  *out_has_watermark = handle->result.watermark_ms.has_value() ? 1 : 0;
  *out_watermark_ms = handle->result.watermark_ms.value_or(0);
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

int ruleforge_continuous_result_get_output_count(ruleforge_continuous_result_t result) {
  if (!result) {
    set_error("Continuous result handle is NULL");
    return -1;
  }
  auto *handle = reinterpret_cast<ruleforge_continuous_result_handle_s *>(result);
  if (handle->output_facts.size() > static_cast<size_t>(INT_MAX)) {
    set_error("Continuous result output count exceeds int range");
    return -1;
  }
  last_error[0] = '\0';
  return static_cast<int>(handle->output_facts.size());
}

ruleforge_status_t ruleforge_continuous_result_get_output(
    ruleforge_continuous_result_t result, int index, ruleforge_fact_t *out_fact) {
  if (out_fact) {
    *out_fact = nullptr;
  }
  if (!result || !out_fact || index < 0) {
    set_error("Continuous result, output fact, or index is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  auto *handle = reinterpret_cast<ruleforge_continuous_result_handle_s *>(result);
  if (static_cast<size_t>(index) >= handle->output_facts.size()) {
    set_error("Continuous output index is out of range");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  *out_fact = reinterpret_cast<ruleforge_fact_t>(&handle->output_facts[static_cast<size_t>(index)]);
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

ruleforge_status_t ruleforge_continuous_result_destroy(
    ruleforge_continuous_result_t result) {
  if (!result) {
    set_error("Continuous result handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  delete reinterpret_cast<ruleforge_continuous_result_handle_s *>(result);
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

// QueryResult wrapper for safe C++/C interop
struct QueryResultWrapper {
  std::unique_ptr<QueryResult> query_result;
};

ruleforge_status_t ruleforge_data_bind_stream_json_create(
    ruleforge_stateful_session_t session, const char *fact_type,
    ruleforge_data_bind_stream_t *out_stream) {
  std::unique_ptr<ruleforge_data_bind_stream_handle_s> handle;
  auto status = prepare_data_bind_stream(session, fact_type, false,
                                         out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_json_create(
      handle->codec.get(), fact_type, &handle->output_value, &handle->error);
  return publish_data_bind_stream(std::move(handle), stream, out_stream);
}

ruleforge_status_t ruleforge_data_bind_stream_json_all_create(
    ruleforge_stateful_session_t session, const char *fact_type,
    ruleforge_data_bind_stream_t *out_stream) {
  std::unique_ptr<ruleforge_data_bind_stream_handle_s> handle;
  auto status = prepare_data_bind_stream(session, fact_type, true,
                                         out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_json_all_create(
      handle->codec.get(), fact_type, &handle->output_value, &handle->error);
  return publish_data_bind_stream(std::move(handle), stream, out_stream);
}

ruleforge_status_t ruleforge_data_bind_stream_json_path_create(
    ruleforge_stateful_session_t session, const char *fact_type,
    const char *json_path,
    ruleforge_data_bind_stream_t *out_stream) {
  if (!json_path || json_path[0] == '\0') {
    if (out_stream) {
      *out_stream = nullptr;
    }
    set_error("JSON path is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  std::unique_ptr<ruleforge_data_bind_stream_handle_s> handle;
  auto status = prepare_data_bind_stream(session, fact_type, false,
                                         out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_json_path_create(
      handle->codec.get(), fact_type, json_path, &handle->output_value, &handle->error);
  return publish_data_bind_stream(std::move(handle), stream, out_stream);
}

ruleforge_status_t ruleforge_data_bind_stream_json_path_all_create(
    ruleforge_stateful_session_t session, const char *fact_type,
    const char *json_path,
    ruleforge_data_bind_stream_t *out_stream) {
  if (!json_path || json_path[0] == '\0') {
    if (out_stream) {
      *out_stream = nullptr;
    }
    set_error("JSON path is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  std::unique_ptr<ruleforge_data_bind_stream_handle_s> handle;
  auto status = prepare_data_bind_stream(session, fact_type, true,
                                         out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_json_path_all_create(
      handle->codec.get(), fact_type, json_path, &handle->output_value, &handle->error);
  return publish_data_bind_stream(std::move(handle), stream, out_stream);
}

ruleforge_status_t ruleforge_data_bind_stream_yaml_create(
    ruleforge_stateful_session_t session, const char *fact_type,
    ruleforge_data_bind_stream_t *out_stream) {
  std::unique_ptr<ruleforge_data_bind_stream_handle_s> handle;
  auto status = prepare_data_bind_stream(session, fact_type, false,
                                         out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_yaml_create(
      handle->codec.get(), fact_type, &handle->output_value, &handle->error);
  return publish_data_bind_stream(std::move(handle), stream, out_stream);
}

ruleforge_status_t ruleforge_data_bind_stream_yaml_all_create(
    ruleforge_stateful_session_t session, const char *fact_type,
    ruleforge_data_bind_stream_t *out_stream) {
  std::unique_ptr<ruleforge_data_bind_stream_handle_s> handle;
  auto status = prepare_data_bind_stream(session, fact_type, true,
                                         out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_yaml_all_create(
      handle->codec.get(), fact_type, &handle->output_value, &handle->error);
  return publish_data_bind_stream(std::move(handle), stream, out_stream);
}

ruleforge_status_t ruleforge_data_bind_stream_yaml_path_create(
    ruleforge_stateful_session_t session, const char *fact_type,
    const char *yaml_path,
    ruleforge_data_bind_stream_t *out_stream) {
  if (!yaml_path || yaml_path[0] == '\0') {
    if (out_stream) {
      *out_stream = nullptr;
    }
    set_error("YAML path is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  std::unique_ptr<ruleforge_data_bind_stream_handle_s> handle;
  auto status = prepare_data_bind_stream(session, fact_type, false,
                                         out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_yaml_path_create(
      handle->codec.get(), fact_type, yaml_path, &handle->output_value, &handle->error);
  return publish_data_bind_stream(std::move(handle), stream, out_stream);
}

ruleforge_status_t ruleforge_data_bind_stream_yaml_path_all_create(
    ruleforge_stateful_session_t session, const char *fact_type,
    const char *yaml_path,
    ruleforge_data_bind_stream_t *out_stream) {
  if (!yaml_path || yaml_path[0] == '\0') {
    if (out_stream) {
      *out_stream = nullptr;
    }
    set_error("YAML path is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  std::unique_ptr<ruleforge_data_bind_stream_handle_s> handle;
  auto status = prepare_data_bind_stream(session, fact_type, true,
                                         out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_yaml_path_all_create(
      handle->codec.get(), fact_type, yaml_path, &handle->output_value, &handle->error);
  return publish_data_bind_stream(std::move(handle), stream, out_stream);
}

ruleforge_status_t ruleforge_data_bind_stream_csv_all_create(
    ruleforge_stateful_session_t session, const char *fact_type,
    ruleforge_data_bind_stream_t *out_stream) {
  std::unique_ptr<ruleforge_data_bind_stream_handle_s> handle;
  auto status = prepare_data_bind_stream(session, fact_type, true,
                                         out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_csv_all_create(
      handle->codec.get(), fact_type, &handle->output_value, &handle->error);
  return publish_data_bind_stream(std::move(handle), stream, out_stream);
}

ruleforge_status_t ruleforge_data_bind_stream_csv_path_create(
    ruleforge_stateful_session_t session, const char *fact_type,
    const char *csv_path,
    ruleforge_data_bind_stream_t *out_stream) {
  if (!csv_path || csv_path[0] == '\0') {
    if (out_stream) {
      *out_stream = nullptr;
    }
    set_error("CSV path is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  std::unique_ptr<ruleforge_data_bind_stream_handle_s> handle;
  auto status = prepare_data_bind_stream(session, fact_type, true,
                                         out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_csv_path_create(
      handle->codec.get(), fact_type, csv_path, &handle->output_value, &handle->error);
  return publish_data_bind_stream(std::move(handle), stream, out_stream);
}

ruleforge_status_t ruleforge_data_bind_stream_xml_create(
    ruleforge_stateful_session_t session, const char *fact_type,
    ruleforge_data_bind_stream_t *out_stream) {
  std::unique_ptr<ruleforge_data_bind_stream_handle_s> handle;
  auto status = prepare_data_bind_stream(session, fact_type, false,
                                         out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_xml_create(
      handle->codec.get(), fact_type, &handle->output_value, &handle->error);
  return publish_data_bind_stream(std::move(handle), stream, out_stream);
}

ruleforge_status_t ruleforge_data_bind_stream_xml_path_all_create(
    ruleforge_stateful_session_t session, const char *fact_type,
    const char *xml_path,
    ruleforge_data_bind_stream_t *out_stream) {
  if (!xml_path || xml_path[0] == '\0') {
    if (out_stream) {
      *out_stream = nullptr;
    }
    set_error("XML path is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  std::unique_ptr<ruleforge_data_bind_stream_handle_s> handle;
  auto status = prepare_data_bind_stream(session, fact_type, true,
                                         out_stream, handle);
  if (status != RULES_FORGE_OK) {
    return status;
  }
  auto *stream = data_bind_stream_xml_path_all_create(
      handle->codec.get(), fact_type, xml_path, &handle->output_value, &handle->error);
  return publish_data_bind_stream(std::move(handle), stream, out_stream);
}

ruleforge_status_t ruleforge_data_bind_stream_feed(
    ruleforge_data_bind_stream_t stream, const void *data, size_t len) {
  auto *handle = reinterpret_cast<ruleforge_data_bind_stream_handle_s *>(stream);
  auto state_status = reject_unusable_data_bind_stream(handle);
  if (state_status != RULES_FORGE_OK) {
    return state_status;
  }
  if (!data && len != 0) {
    set_error("DataBind stream chunk is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindStatus status = static_cast<DataBindStatus>(
      data_bind_stream_feed(handle->stream.get(), data, len));
  if (status != DATA_BIND_OK) {
    handle->failed = true;
    set_error_fmt("DataBind stream feed failed: ",
                  data_bind_error_detail(status, handle->error));
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

ruleforge_status_t ruleforge_data_bind_stream_feed_file(
    ruleforge_data_bind_stream_t stream, const char *file_path) {
  auto *handle = reinterpret_cast<ruleforge_data_bind_stream_handle_s *>(stream);
  auto state_status = reject_unusable_data_bind_stream(handle);
  if (state_status != RULES_FORGE_OK) {
    return state_status;
  }
  if (!file_path || file_path[0] == '\0') {
    set_error("DataBind stream file path is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  DataBindStatus status = static_cast<DataBindStatus>(
      data_bind_stream_feed_file(handle->stream.get(), file_path));
  if (status != DATA_BIND_OK) {
    handle->failed = true;
    set_error_fmt("DataBind stream file feed failed: ",
                  data_bind_error_detail(status, handle->error));
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

ruleforge_status_t ruleforge_data_bind_stream_finish(
    ruleforge_data_bind_stream_t stream, ruleforge_fact_t **out_facts,
    int *out_loaded_count) {
  if (out_facts) {
    *out_facts = nullptr;
  }
  if (out_loaded_count) {
    *out_loaded_count = 0;
  }
  auto *handle = reinterpret_cast<ruleforge_data_bind_stream_handle_s *>(stream);
  auto state_status = reject_unusable_data_bind_stream(handle);
  if (state_status != RULES_FORGE_OK) {
    return state_status;
  }

  DataBindStatus bind_status = static_cast<DataBindStatus>(
      data_bind_stream_finish(handle->stream.get()));
  handle->finished = true;
  handle->value.reset(handle->output_value);
  handle->output_value = nullptr;
  if (bind_status != DATA_BIND_OK) {
    handle->failed = true;
    set_error_fmt("DataBind stream finish failed: ",
                  data_bind_error_detail(bind_status, handle->error));
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  try {
    ruleforge_status_t status;
    if (handle->multiple) {
      status = insert_data_bind_fact_list(*handle->session_wrapper->session,
                                          handle->fact_type.c_str(), handle->value.get(),
                                          out_facts, out_loaded_count);
    } else {
      ruleforge_fact_t *facts = nullptr;
      if (out_facts) {
        facts = static_cast<ruleforge_fact_t *>(std::malloc(sizeof(ruleforge_fact_t)));
        if (!facts) {
          set_error("Failed to allocate fact handle array");
          return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
        }
      }
      ruleforge_fact_t fact = nullptr;
      status = insert_data_bind_fact(*handle->session_wrapper->session,
                                     handle->fact_type.c_str(), handle->value.get(), &fact);
      if (status == RULES_FORGE_OK) {
        if (out_loaded_count) {
          *out_loaded_count = 1;
        }
        if (out_facts) {
          facts[0] = fact;
          *out_facts = facts;
        }
      } else {
        std::free(facts);
      }
    }
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (std::exception const &e) {
    set_error_fmt("Failed to insert DataBind stream result: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t ruleforge_data_bind_stream_destroy(ruleforge_data_bind_stream_t stream) {
  if (!stream) {
    set_error("DataBind stream handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  auto *handle = reinterpret_cast<ruleforge_data_bind_stream_handle_s *>(stream);
  if (handle->session_wrapper && handle->session_wrapper->active_data_bind_streams != 0) {
    --handle->session_wrapper->active_data_bind_streams;
  }
  delete handle;
  last_error[0] = '\0';
  return RULES_FORGE_OK;
}

ruleforge_status_t ruleforge_session_add_data_bind_object(
    ruleforge_stateful_session_t session, ruleforge_data_bind_object_t object,
    ruleforge_fact_t *out_fact) {
  if (out_fact) {
    *out_fact = nullptr;
  }
  auto *session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
  auto *object_handle =
      reinterpret_cast<ruleforge_data_bind_object_handle_s *>(object);
  if (!session_wrapper || !session_wrapper->session || !object_handle
      || !object_handle->object) {
    set_error("Session or DataBindObject handle is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  char const *fact_type = data_bind_object_type_name(object_handle->object.get());
  DataBindValue const *value = data_bind_object_value(object_handle->object.get());
  if (!fact_type || !value) {
    set_error("DataBindObject type name or value is unavailable");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto type_status =
        require_schema_imported_fact_type(*session_wrapper->session, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    auto status = insert_data_bind_object(
        *session_wrapper->session, object_handle->object.get(), out_fact);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (std::exception const &e) {
    set_error_fmt("Failed to add DataBindObject fact: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t ruleforge_session_add_data_bind_value(
    ruleforge_stateful_session_t session, const char *fact_type,
    const DataBindValue *value, ruleforge_fact_t *out_fact) {
  if (out_fact) {
    *out_fact = nullptr;
  }
  auto *session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
  if (!session_wrapper || !session_wrapper->session || !fact_type ||
      fact_type[0] == '\0' || !value) {
    set_error("Session, fact type, or DataBindValue is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto type_status =
        require_schema_imported_fact_type(*session_wrapper->session, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    auto status = insert_data_bind_fact(*session_wrapper->session, fact_type,
                                        value, out_fact);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (std::exception const &e) {
    set_error_fmt("Failed to add DataBindValue fact: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t
ruleforge_session_add_fact_json(ruleforge_stateful_session_t session,
                                const char *fact_type,
                                const char *fact_json,
                                ruleforge_fact_t *out_fact) {
  if (!session || !fact_type || !fact_json) {
    set_error("Session handle, fact type, or fact JSON is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (out_fact) {
    *out_fact = nullptr;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    auto type_status = require_schema_imported_fact_type(*session_wrapper->session, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    auto const kb = session_wrapper->session->get_knowledge_base();
    auto codec = kb ? get_kb_data_bind_or_set_error(*kb, fact_type)
                    : DataBindView{};
    if (!codec) {
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    DataBindObject *raw_object = nullptr;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindStatus bind_status = data_bind_object_from_json(
        codec.get(), fact_type, fact_json, std::strlen(fact_json),
        &raw_object, &error);
    DataBindObjectHandle object(raw_object);
    if (bind_status != DATA_BIND_OK) {
      set_error_fmt("DataBind JSON parse failed: ",
                    data_bind_error_detail(bind_status, error));
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto status = insert_data_bind_object(
        *session_wrapper->session, object.get(), out_fact);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (std::exception const &e) {
    set_error_fmt("Failed to add schema-bound JSON fact: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t
ruleforge_session_add_fact_json_path(ruleforge_stateful_session_t session,
                                     const char *fact_type,
                                     const char *fact_json,
                                     const char *json_path,
                                     ruleforge_fact_t *out_fact) {
  if (out_fact) {
    *out_fact = nullptr;
  }
  if (!session || !fact_type || !fact_json
      || !json_path || json_path[0] == '\0') {
    set_error("Session, fact type, JSON source, or JSON path is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto *session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    auto type_status = require_schema_imported_fact_type(*session_wrapper->session, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    auto const kb = session_wrapper->session->get_knowledge_base();
    auto codec = kb ? get_kb_data_bind_or_set_error(*kb, fact_type)
                    : DataBindView{};
    if (!codec) {
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    DataBindValue *raw_value = nullptr;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindStatus bind_status = data_bind_parse_json_path(
        codec.get(), fact_type, fact_json, std::strlen(fact_json), json_path,
        &raw_value, &error);
    DataBindValueHandle value(raw_value);
    if (bind_status != DATA_BIND_OK) {
      set_error_fmt("DataBind JSON path parse failed: ",
                    data_bind_error_detail(bind_status, error));
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto status = insert_data_bind_fact(*session_wrapper->session, fact_type,
                                        value.get(), out_fact);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (std::exception const &e) {
    set_error_fmt("Failed to add JSONPath-selected fact: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t
ruleforge_session_add_facts_json_path(ruleforge_stateful_session_t session,
                                      const char *fact_type,
                                             const char *fact_json,
                                             const char *json_path,
                                             ruleforge_fact_t **out_facts,
                                             int *out_loaded_count) {
  if (out_facts) {
    *out_facts = nullptr;
  }
  if (out_loaded_count) {
    *out_loaded_count = 0;
  }
  if (!session || !fact_type || !fact_json
      || !json_path || json_path[0] == '\0') {
    set_error("Session, fact type, JSON source, or JSON path is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto *session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    auto type_status = require_schema_imported_fact_type(*session_wrapper->session, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    auto const kb = session_wrapper->session->get_knowledge_base();
    auto codec = kb ? get_kb_data_bind_or_set_error(*kb, fact_type)
                    : DataBindView{};
    if (!codec) {
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    DataBindValue *raw_value = nullptr;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindStatus bind_status = data_bind_parse_json_path_all(
        codec.get(), fact_type, fact_json, std::strlen(fact_json), json_path,
        &raw_value, &error);
    DataBindValueHandle value(raw_value);
    if (bind_status != DATA_BIND_OK) {
      set_error_fmt("DataBind JSON path parse failed: ",
                    data_bind_error_detail(bind_status, error));
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto status = insert_data_bind_fact_list(*session_wrapper->session, fact_type,
                                             value.get(), out_facts, out_loaded_count);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (std::exception const &e) {
    set_error_fmt("Failed to add JSONPath-selected facts: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t
ruleforge_session_add_fact_yaml(ruleforge_stateful_session_t session,
                                       const char *fact_type,
                                       const char *fact_yaml,
                                       ruleforge_fact_t *out_fact) {
  if (!session || !fact_type || !fact_yaml) {
    set_error("Session handle, fact type, or fact YAML is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (out_fact) {
    *out_fact = nullptr;
  }
  try {
    auto *session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    auto type_status = require_schema_imported_fact_type(*session_wrapper->session, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    auto const kb = session_wrapper->session->get_knowledge_base();
    auto codec = kb ? get_kb_data_bind_or_set_error(*kb, fact_type)
                    : DataBindView{};
    if (!codec) {
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    DataBindObject *raw_object = nullptr;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindStatus bind_status = data_bind_object_from_yaml(
        codec.get(), fact_type, fact_yaml, std::strlen(fact_yaml),
        &raw_object, &error);
    DataBindObjectHandle object(raw_object);
    if (bind_status != DATA_BIND_OK) {
      set_error_fmt("DataBind YAML parse failed: ",
                    data_bind_error_detail(bind_status, error));
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto status = insert_data_bind_object(
        *session_wrapper->session, object.get(), out_fact);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (std::exception const &e) {
    set_error_fmt("Failed to add schema-bound YAML fact: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t
ruleforge_session_add_fact_yaml_path(ruleforge_stateful_session_t session,
                                            const char *fact_type,
                                            const char *fact_yaml,
                                            const char *yaml_path,
                                            ruleforge_fact_t *out_fact) {
  if (out_fact) {
    *out_fact = nullptr;
  }
  if (!session || !fact_type || !fact_yaml
      || !yaml_path || yaml_path[0] == '\0') {
    set_error("Session, fact type, YAML source, or YAML path is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto *session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    auto type_status = require_schema_imported_fact_type(*session_wrapper->session, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    auto const kb = session_wrapper->session->get_knowledge_base();
    auto codec = kb ? get_kb_data_bind_or_set_error(*kb, fact_type)
                    : DataBindView{};
    if (!codec) {
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    DataBindValue *raw_value = nullptr;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindStatus bind_status = data_bind_parse_yaml_path(
        codec.get(), fact_type, fact_yaml, std::strlen(fact_yaml), yaml_path,
        &raw_value, &error);
    DataBindValueHandle value(raw_value);
    if (bind_status != DATA_BIND_OK) {
      set_error_fmt("DataBind YAML path parse failed: ",
                    data_bind_error_detail(bind_status, error));
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto status = insert_data_bind_fact(*session_wrapper->session, fact_type,
                                        value.get(), out_fact);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (std::exception const &e) {
    set_error_fmt("Failed to add YPATH-selected fact: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t
ruleforge_session_add_facts_yaml_path(ruleforge_stateful_session_t session,
                                             const char *fact_type,
                                             const char *fact_yaml,
                                             const char *yaml_path,
                                             ruleforge_fact_t **out_facts,
                                             int *out_loaded_count) {
  if (out_facts) {
    *out_facts = nullptr;
  }
  if (out_loaded_count) {
    *out_loaded_count = 0;
  }
  if (!session || !fact_type || !fact_yaml
      || !yaml_path || yaml_path[0] == '\0') {
    set_error("Session, fact type, YAML source, or YAML path is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto *session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    auto type_status = require_schema_imported_fact_type(*session_wrapper->session, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    auto const kb = session_wrapper->session->get_knowledge_base();
    auto codec = kb ? get_kb_data_bind_or_set_error(*kb, fact_type)
                    : DataBindView{};
    if (!codec) {
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    DataBindValue *raw_value = nullptr;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindStatus bind_status = data_bind_parse_yaml_path_all(
        codec.get(), fact_type, fact_yaml, std::strlen(fact_yaml), yaml_path,
        &raw_value, &error);
    DataBindValueHandle value(raw_value);
    if (bind_status != DATA_BIND_OK) {
      set_error_fmt("DataBind YAML path parse failed: ",
                    data_bind_error_detail(bind_status, error));
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto status = insert_data_bind_fact_list(*session_wrapper->session, fact_type,
                                             value.get(), out_facts, out_loaded_count);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (std::exception const &e) {
    set_error_fmt("Failed to add YPATH-selected facts: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t
ruleforge_session_add_fact_binary(ruleforge_stateful_session_t session,
                                         const char *fact_type,
                                         const uint8_t *fact_data,
                                         size_t fact_len,
                                         ruleforge_fact_t *out_fact) {
  if (!session || !fact_type || !fact_data || fact_len == 0) {
    set_error("Session handle, fact type, or binary payload is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (out_fact) {
    *out_fact = nullptr;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    auto type_status = require_schema_imported_fact_type(*session_wrapper->session, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    auto const kb = session_wrapper->session->get_knowledge_base();
    auto codec = kb ? get_kb_data_bind_or_set_error(*kb, fact_type)
                    : DataBindView{};
    if (!codec) {
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    DataBindObject *raw_object = nullptr;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindStatus bind_status = data_bind_object_from_bin(
        codec.get(), fact_type, fact_data, fact_len, &raw_object, &error);
    DataBindObjectHandle object(raw_object);
    if (bind_status != DATA_BIND_OK) {
      set_error_fmt("DataBind binary parse failed: ",
                    data_bind_error_detail(bind_status, error));
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto status = insert_data_bind_object(
        *session_wrapper->session, object.get(), out_fact);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (std::exception const &e) {
    set_error_fmt("Failed to add schema-bound binary fact: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t
ruleforge_session_add_facts_csv(ruleforge_stateful_session_t session,
                                       const char *fact_type,
                                       const char *csv_source,
                                       ruleforge_fact_t **out_facts,
                                       int *out_loaded_count) {
  if (out_facts) {
    *out_facts = nullptr;
  }
  if (out_loaded_count) {
    *out_loaded_count = 0;
  }
  if (!session || !fact_type || !csv_source) {
    set_error("Session handle, fact type, or CSV source is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    auto type_status = require_schema_imported_fact_type(*session_wrapper->session, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    auto const kb = session_wrapper->session->get_knowledge_base();
    auto codec = kb ? get_kb_data_bind_or_set_error(*kb, fact_type)
                    : DataBindView{};
    if (!codec) {
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    DataBindValue *raw_value = nullptr;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindStatus bind_status = data_bind_parse_csv_all(codec.get(), fact_type, csv_source,
                                                         std::strlen(csv_source), &raw_value,
                                                         &error);
    DataBindValueHandle value(raw_value);
    if (bind_status != DATA_BIND_OK) {
      set_error_fmt("DataBind CSV parse failed: ",
                    data_bind_error_detail(bind_status, error));
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto status = insert_data_bind_fact_list(*session_wrapper->session, fact_type, value.get(),
                                             out_facts, out_loaded_count);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (std::exception const &e) {
    set_error_fmt("Failed to add schema-bound CSV facts: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t
ruleforge_session_add_facts_csv_path(ruleforge_stateful_session_t session,
                                            const char *fact_type,
                                            const char *csv_source,
                                            const char *csv_path,
                                            ruleforge_fact_t **out_facts,
                                            int *out_loaded_count) {
  if (out_facts) {
    *out_facts = nullptr;
  }
  if (out_loaded_count) {
    *out_loaded_count = 0;
  }
  if (!session || !fact_type || !csv_source
      || !csv_path || csv_path[0] == '\0') {
    set_error("Session, fact type, CSV source, or CSV path is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto *session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    auto type_status = require_schema_imported_fact_type(*session_wrapper->session, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    auto const kb = session_wrapper->session->get_knowledge_base();
    auto codec = kb ? get_kb_data_bind_or_set_error(*kb, fact_type)
                    : DataBindView{};
    if (!codec) {
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    DataBindValue *raw_value = nullptr;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindStatus bind_status = data_bind_parse_csv_path(
        codec.get(), fact_type, csv_source, std::strlen(csv_source), csv_path,
        &raw_value, &error);
    DataBindValueHandle value(raw_value);
    if (bind_status != DATA_BIND_OK) {
      set_error_fmt("DataBind CSV path parse failed: ",
                    data_bind_error_detail(bind_status, error));
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto status = insert_data_bind_fact_list(*session_wrapper->session, fact_type,
                                             value.get(), out_facts, out_loaded_count);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (std::exception const &e) {
    set_error_fmt("Failed to add CSVPath-selected facts: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

ruleforge_status_t
ruleforge_session_add_facts_xml(ruleforge_stateful_session_t session,
                                       const char *fact_type,
                                       const char *xml_source,
                                       const char *xpath,
                                       ruleforge_fact_t **out_facts,
                                       int *out_loaded_count) {
  if (out_facts) {
    *out_facts = nullptr;
  }
  if (out_loaded_count) {
    *out_loaded_count = 0;
  }
  if (!session || !fact_type || !xml_source) {
    set_error("Session handle, fact type, or XML source is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    auto type_status = require_schema_imported_fact_type(*session_wrapper->session, fact_type);
    if (type_status != RULES_FORGE_OK) {
      return type_status;
    }
    auto const kb = session_wrapper->session->get_knowledge_base();
    auto codec = kb ? get_kb_data_bind_or_set_error(*kb, fact_type)
                    : DataBindView{};
    if (!codec) {
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    DataBindValue *raw_value = nullptr;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindStatus bind_status = data_bind_parse_xml_path_all(codec.get(), fact_type, xml_source,
                                                             std::strlen(xml_source), xpath,
                                                             &raw_value, &error);
    DataBindValueHandle value(raw_value);
    if (bind_status != DATA_BIND_OK) {
      set_error_fmt("DataBind XML parse failed: ",
                    data_bind_error_detail(bind_status, error));
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto status = insert_data_bind_fact_list(*session_wrapper->session, fact_type, value.get(),
                                             out_facts, out_loaded_count);
    if (status == RULES_FORGE_OK) {
      last_error[0] = '\0';
    }
    return status;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (std::exception const &e) {
    set_error_fmt("Failed to add schema-bound XML facts: ", e.what());
    return RULES_FORGE_ERROR_FACT_INSERTION_FAILED;
  }
}

void ruleforge_fact_array_free(ruleforge_fact_t *facts) { std::free(facts); }

ruleforge_status_t ruleforge_session_fire_all_rules(ruleforge_stateful_session_t session,
                                                    int max_rules, int *out_fired_count) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    if (!session_wrapper->session->is_consistent()) {
      set_error("Session is inconsistent due to a previous failed RHS transaction. Call reset() "
                "before firing again.");
      return RULES_FORGE_ERROR_SESSION_INCONSISTENT;
    }
    int fired = session_wrapper->session->fire_all_rules(max_rules);
    if (out_fired_count) {
      *out_fired_count = fired;
    }
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (const std::exception &e) {
    set_error_fmt("Failed to fire rules: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_session_reset(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    session_wrapper->session->reset();
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to reset session: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

int ruleforge_session_get_fact_count(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return -1;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    last_error[0] = '\0';
    return static_cast<int>(session_wrapper->session->get_fact_count());
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get fact count: ", e.what());
    return -1;
  }
}

ruleforge_status_t ruleforge_session_query(ruleforge_stateful_session_t session,
                                           const char *query_name,
                                           ruleforge_query_result_t *out_query_result) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (!query_name) {
    set_error("Query name is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (!out_query_result) {
    set_error("Output Query Result pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  *out_query_result = nullptr;
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);

    // Execute query
    QueryResult query_result = session_wrapper->session->execute_query(query_name);
    if (!query_result.success()) {
      set_error(query_result.error_message().c_str());
      return RULES_FORGE_ERROR_QUERY_FAILED;
    }

    // Wrap in QueryResultWrapper for safe C interop
    auto result_wrapper = new QueryResultWrapper();
    result_wrapper->query_result = std::make_unique<QueryResult>(query_result);

    *out_query_result = reinterpret_cast<ruleforge_query_result_t>(result_wrapper);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to execute query: ", e.what());
    return RULES_FORGE_ERROR_QUERY_FAILED;
  }
}

ruleforge_status_t ruleforge_session_destroy(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    if (session_wrapper->active_data_bind_streams != 0) {
      set_error("Cannot destroy session while DataBind streams are active");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    delete session_wrapper;
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to destroy session: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_session_set_validation_mode(ruleforge_stateful_session_t session,
                                                         ruleforge_validation_mode_t mode) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  ValidationMode cpp_mode = ValidationMode::None;
  switch (mode) {
  case RULES_FORGE_VALIDATION_NONE:
    cpp_mode = ValidationMode::None;
    break;
  case RULES_FORGE_VALIDATION_WARN:
    cpp_mode = ValidationMode::Warn;
    break;
  case RULES_FORGE_VALIDATION_STRICT:
    cpp_mode = ValidationMode::Strict;
    break;
  default:
    set_error("Validation mode is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    session_wrapper->session->set_validation_mode(cpp_mode);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (std::exception const &e) {
    set_error_fmt("Failed to set validation mode: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

// Query result functions
int ruleforge_query_result_get_size(ruleforge_query_result_t query_result) {
  if (!query_result) {
    set_error("Query Result handle is NULL");
    return -1;
  }
  try {
    auto result_wrapper = reinterpret_cast<QueryResultWrapper *>(query_result);
    last_error[0] = '\0';
    return static_cast<int>(result_wrapper->query_result->size());
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get query result size: ", e.what());
    return -1;
  }
}

ruleforge_status_t ruleforge_query_result_get_fact_at_index(ruleforge_query_result_t query_result,
                                                            int row_index, const char *binding_name,
                                                            ruleforge_fact_t *out_fact) {
  if (!query_result || !binding_name || !out_fact) {
    set_error("Query Result handle, binding name, or output Fact pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (row_index < 0) {
    set_error("Row index is negative");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto result_wrapper = reinterpret_cast<QueryResultWrapper *>(query_result);

    if (row_index >= static_cast<int>(result_wrapper->query_result->size())) {
      set_error("Row index out of bounds");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    // Use iterator to access row at index
    auto it = result_wrapper->query_result->begin();
    std::advance(it, row_index);

    // Get the fact from the query result row
    auto fact_opt = (*it).get(std::string(binding_name));
    if (!fact_opt) {
      set_error("Binding name not found");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    // Return the raw pointer - ownership remains with QueryResult
    *out_fact = reinterpret_cast<ruleforge_fact_t>(*fact_opt);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get fact: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_query_result_destroy(ruleforge_query_result_t query_result) {
  if (!query_result) {
    set_error("Query Result handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto result_wrapper = reinterpret_cast<QueryResultWrapper *>(query_result);
    delete result_wrapper;
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to destroy query result: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

// Fact functions
ruleforge_status_t ruleforge_fact_get_field_as_string(ruleforge_fact_t fact, const char *field_name,
                                                      char *buffer, size_t buffer_size,
                                                      size_t *out_actual_length) {
  if (!fact || !field_name || !buffer || !out_actual_length) {
    set_error("Fact handle, field name, buffer, or actual length pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto f = reinterpret_cast<const Fact *>(fact);
    auto field_opt = f->get_field(field_name);
    if (!field_opt) {
      set_error("Field not found");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    std::optional<std::string> converted;
    if (std::holds_alternative<std::string>(*field_opt)
        || std::holds_alternative<DateTimeValue>(*field_opt)
        || std::holds_alternative<DateValue>(*field_opt)
        || std::holds_alternative<TimeValue>(*field_opt)
        || std::holds_alternative<DecimalValue>(*field_opt)
        || std::holds_alternative<BigIntValue>(*field_opt)
        || std::holds_alternative<MoneyValue>(*field_opt)) {
      converted = scalar_text(*field_opt);
    }
    if (!converted) {
      set_error("Field is not a string");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    const auto &field = *converted;

    *out_actual_length = field.length();
    if (buffer_size < field.length() + 1) {
      set_error("Buffer too small");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    memcpy(buffer, field.c_str(), field.length() + 1);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get string field: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_fact_get_field_as_double(ruleforge_fact_t fact, const char *field_name,
                                                      double *out_value) {
  if (!fact || !field_name || !out_value) {
    set_error("Fact handle, field name, or output value pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto f = reinterpret_cast<const Fact *>(fact);
    auto field_opt = f->get_field(field_name);
    if (!field_opt) {
      set_error("Field not found");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto *double_ptr = std::get_if<double>(&(*field_opt));
    if (double_ptr) {
      *out_value = *double_ptr;
    } else {
      auto *int_ptr = std::get_if<int64_t>(&(*field_opt));
      if (int_ptr) {
        *out_value = static_cast<double>(*int_ptr);
      } else {
        set_error("Field is not a numeric type");
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
      }
    }
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get double field: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_fact_get_field_as_int(ruleforge_fact_t fact, const char *field_name,
                                                   int64_t *out_value) {
  if (!fact || !field_name || !out_value) {
    set_error("Fact handle, field name, or output value pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto f = reinterpret_cast<const Fact *>(fact);
    auto field_opt = f->get_field(field_name);
    if (!field_opt) {
      set_error("Field not found");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    int64_t const *field_ptr = std::get_if<int64_t>(&(*field_opt));
    int64_t converted = 0;
    if (!field_ptr) {
      if (auto const *duration = std::get_if<DurationValue>(&(*field_opt))) {
        converted = duration->milliseconds;
        field_ptr = &converted;
      }
    }
    if (!field_ptr) {
      set_error("Field is not an int");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    *out_value = *field_ptr;
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get int field: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_fact_get_field_as_uint64(
    ruleforge_fact_t fact, const char *field_name, uint64_t *out_value) {
  if (!fact || !field_name || !out_value) {
    set_error("Fact handle, field name, or output value pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto const *typed_fact = reinterpret_cast<Fact const *>(fact);
    auto field = typed_fact->get_field(field_name);
    auto const *value = field ? std::get_if<uint64_t>(&*field) : nullptr;
    if (!value) {
      set_error(field ? "Field is not a uint64" : "Field not found");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    *out_value = *value;
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (std::exception const &e) {
    set_error_fmt("Failed to get uint64 field: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_fact_get_field_as_bytes(
    ruleforge_fact_t fact, const char *field_name, uint8_t *buffer,
    size_t buffer_size, size_t *out_actual_length) {
  if (!fact || !field_name || !out_actual_length || (!buffer && buffer_size != 0)) {
    set_error("Fact handle, field name, byte buffer, or actual length pointer is invalid");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto const *typed_fact = reinterpret_cast<Fact const *>(fact);
    auto field = typed_fact->get_field(field_name);
    auto const *value = field ? std::get_if<BytesValue>(&*field) : nullptr;
    if (!value) {
      set_error(field ? "Field is not bytes" : "Field not found");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    *out_actual_length = value->bytes.size();
    if (buffer_size < value->bytes.size()) {
      set_error("Buffer too small");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    if (!value->bytes.empty()) std::memcpy(buffer, value->bytes.data(), value->bytes.size());
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (std::exception const &e) {
    set_error_fmt("Failed to get bytes field: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_fact_get_field_as_bool(ruleforge_fact_t fact, const char *field_name,
                                                    int *out_value) {
  if (!fact || !field_name || !out_value) {
    set_error("Fact handle, field name, or output value pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto f = reinterpret_cast<const Fact *>(fact);
    auto field_opt = f->get_field(field_name);
    if (!field_opt) {
      set_error("Field not found");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    auto *field_ptr = std::get_if<bool>(&(*field_opt));
    if (!field_ptr) {
      set_error("Field is not a bool");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    *out_value = *field_ptr ? 1 : 0;
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get bool field: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_fact_get_enum_name(
    ruleforge_fact_t fact, char const *field_name, char *name_buffer,
    size_t buffer_size, size_t *out_actual_length) {
  if (!fact || !field_name || !name_buffer || !out_actual_length) {
    set_error("Fact handle, field name, name buffer, or actual length pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto const *typed_fact = reinterpret_cast<Fact const *>(fact);
    auto field = typed_fact->get_field(field_name);
    auto const *enum_value = field ? std::get_if<EnumValue>(&*field) : nullptr;
    if (!enum_value || enum_value->item_name.empty()) {
      set_error("Field is not an enum or has no schema enum name");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    *out_actual_length = enum_value->item_name.size();
    if (buffer_size <= enum_value->item_name.size()) {
      set_error("Buffer too small");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    std::memcpy(name_buffer, enum_value->item_name.c_str(), enum_value->item_name.size() + 1);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (std::exception const &e) {
    set_error_fmt("Failed to get enum name: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_fact_get_field_as_enum(
    ruleforge_fact_t fact, char const *field_name, char *name_buffer,
    size_t buffer_size, size_t *out_actual_length, int64_t *out_value) {
  if (!out_value) {
    set_error("Enum value output pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  if (!fact || !field_name) {
    set_error("Fact handle or field name is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  auto const *typed_fact = reinterpret_cast<Fact const *>(fact);
  auto field = typed_fact->get_field(field_name);
  auto const *enum_value = field ? std::get_if<EnumValue>(&*field) : nullptr;
  if (!enum_value) {
    set_error("Field is not an enum");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  int64_t value = 0;
  if (auto const *signed_value = std::get_if<int64_t>(&enum_value->value)) {
    value = *signed_value;
  } else {
    uint64_t const unsigned_value = std::get<uint64_t>(enum_value->value);
    if (unsigned_value > static_cast<uint64_t>(INT64_MAX)) {
      set_error("Enum value does not fit in int64");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    value = static_cast<int64_t>(unsigned_value);
  }
  auto name_status = ruleforge_fact_get_enum_name(
      fact, field_name, name_buffer, buffer_size, out_actual_length);
  if (name_status != RULES_FORGE_OK) {
    return name_status;
  }
  *out_value = value;
  return RULES_FORGE_OK;
}

// --- Session Observability Functions ---

ruleforge_status_t ruleforge_session_enable_tracing(ruleforge_stateful_session_t session,
                                                    int enabled) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    session_wrapper->session->enable_tracing(enabled != 0);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (SessionInconsistentException const &e) {
    return map_session_inconsistent(e);
  } catch (const std::exception &e) {
    set_error_fmt("Failed to enable tracing: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_session_get_execution_trace(ruleforge_stateful_session_t session,
                                                         int include_network, char *buffer,
                                                         size_t buffer_size,
                                                         size_t *out_actual_length) {
  if (!session || !buffer || !out_actual_length) {
    set_error("Session handle, buffer, or actual length pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    std::string trace = session_wrapper->session->get_execution_trace(include_network != 0);

    *out_actual_length = trace.length();
    if (buffer_size < trace.length() + 1) {
      set_error("Buffer too small for trace");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    memcpy(buffer, trace.c_str(), trace.length() + 1);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get execution trace: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t
ruleforge_session_get_rule_performance_summary(ruleforge_stateful_session_t session, char *buffer,
                                               size_t buffer_size, size_t *out_actual_length) {
  if (!session || !buffer || !out_actual_length) {
    set_error("Session handle, buffer, or actual length pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    std::string summary = session_wrapper->session->get_rule_performance_summary();

    *out_actual_length = summary.length();
    if (buffer_size < summary.length() + 1) {
      set_error("Buffer too small for summary");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    memcpy(buffer, summary.c_str(), summary.length() + 1);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get rule performance summary: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

ruleforge_status_t ruleforge_session_clear_trace(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    session_wrapper->session->get_tracer().clear_trace();
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to clear trace: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}

// --- Session Memory Statistics Functions ---

int64_t ruleforge_session_get_memory_used(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return -1;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    last_error[0] = '\0';
    return static_cast<int64_t>(session_wrapper->session->get_arena().memory_used());
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get memory used: ", e.what());
    return -1;
  }
}

int64_t ruleforge_session_get_memory_peak(ruleforge_stateful_session_t session) {
  if (!session) {
    set_error("Session handle is NULL");
    return -1;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    last_error[0] = '\0';
    return static_cast<int64_t>(session_wrapper->session->get_arena().memory_peak());
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get memory peak: ", e.what());
    return -1;
  }
}

ruleforge_status_t ruleforge_session_get_memory_stats(ruleforge_stateful_session_t session,
                                                      char *buffer, size_t buffer_size,
                                                      size_t *out_actual_length) {
  if (!session || !buffer || !out_actual_length) {
    set_error("Session handle, buffer, or actual length pointer is NULL");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto session_wrapper = reinterpret_cast<StatefulSessionWrapper *>(session);
    std::string stats = session_wrapper->session->get_memory_stats();

    *out_actual_length = stats.length();
    if (buffer_size < stats.length() + 1) {
      set_error("Buffer too small for stats");
      return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    memcpy(buffer, stats.c_str(), stats.length() + 1);
    last_error[0] = '\0';
    return RULES_FORGE_OK;
  } catch (const std::exception &e) {
    set_error_fmt("Failed to get memory stats: ", e.what());
    return RULES_FORGE_ERROR_GENERIC;
  }
}


