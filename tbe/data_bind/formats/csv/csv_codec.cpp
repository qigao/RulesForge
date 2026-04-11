#include "csv_codec.h"
#include "core/rfl_parser_state.hpp"
#include <fmt.h>
#include <charconv>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <string>
#include <string_view>

static bool require_callback(bool ok, char* error, size_t error_sz, const char* message) {
  if (ok) return true;
  fmt(error, error_sz, "{}", message);
  return false;
}

static void destroy_value(const DataBindValueApi& api, Value* value) {
  if (value) api.destroy_value(value);
}

static std::string_view trim_ascii(std::string_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return text.substr(begin, end - begin);
}

static bool parse_csv_int32(std::string_view text, int32_t* out) {
  auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), *out);
  return ec == std::errc() && ptr == text.data() + text.size();
}

static bool parse_csv_int64(std::string_view text, int64_t* out) {
  auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), *out);
  return ec == std::errc() && ptr == text.data() + text.size();
}

static bool parse_csv_double(std::string_view text, double* out) {
  std::string owned(text);
  char* end = nullptr;
  *out = std::strtod(owned.c_str(), &end);
  return end == owned.c_str() + owned.size();
}

static bool equals_ascii_ci(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) return false;
  for (size_t i = 0; i < left.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(left[i]))
        != std::tolower(static_cast<unsigned char>(right[i]))) {
      return false;
    }
  }
  return true;
}

static bool parse_csv_bool(std::string_view text, bool* out) {
  if (text == "1" || equals_ascii_ci(text, "true")) {
    *out = true;
    return true;
  }
  if (text == "0" || equals_ascii_ci(text, "false")) {
    *out = false;
    return true;
  }
  return false;
}

static bool validate_csv_field_api(const ParsedField& field, const std::string& full_name,
                                   const DataBindValueApi* api, char* error, size_t error_sz) {
  switch (field.type) {
    case FT_String:
    case FT_Unknown:
      return require_callback(api->set_field_string != nullptr, error, error_sz,
                              ("CSV field '" + full_name + "' requires set_field_string").c_str());
    case FT_Int:
    case FT_Boolean:
      return require_callback(api->set_field_int != nullptr, error, error_sz,
                              ("CSV field '" + full_name + "' requires set_field_int").c_str());
    case FT_Long:
      return require_callback(api->set_field_int64 != nullptr, error, error_sz,
                              ("CSV field '" + full_name + "' requires set_field_int64").c_str());
    case FT_Double:
    case FT_Float:
    case FT_Number:
      return require_callback(api->set_field_double != nullptr, error, error_sz,
                              ("CSV field '" + full_name + "' requires set_field_double").c_str());
    default:
      return require_callback(false, error, error_sz,
                              ("Unsupported CSV field type for '" + full_name + "'").c_str());
  }
}

static bool validate_api_for_declarations(const std::vector<ParsedDeclaration>& declarations,
                                          const DataBindValueApi* api, char* error, size_t error_sz) {
  if (!require_callback(api != nullptr, error, error_sz, "Value API must not be null")) return false;
  if (!require_callback(api->create_list != nullptr, error, error_sz, "CSV codec requires create_list")) return false;
  if (!require_callback(api->create_object != nullptr, error, error_sz, "CSV codec requires create_object")) return false;
  if (!require_callback(api->destroy_value != nullptr, error, error_sz, "CSV codec requires destroy_value")) {
    return false;
  }
  if (!require_callback(api->add_list_item_object != nullptr, error, error_sz,
                        "CSV codec requires add_list_item_object")) {
    return false;
  }

  for (const auto& decl : declarations) {
    for (const auto& field : decl.fields) {
      const std::string full_name = decl.type_name + "." + field.name;
      if (!validate_csv_field_api(field, full_name, api, error, error_sz)) {
        return false;
      }
    }
  }
  return true;
}

CsvCodec::CsvCodec(const std::vector<ParsedDeclaration> &declarations,
                   const std::vector<ParsedEnum> &enums,
                   const DataBindValueApi *api)
    : declarations_(declarations), enums_(enums), api_(*api) {
  error_[0] = '\0';
  validate_api_for_declarations(declarations_, api, error_, sizeof(error_));
}

const char *CsvCodec::get_error() const { return error_; }

Value *CsvCodec::parse(const char *type_name, const uint8_t *data, size_t len) {
  error_[0] = '\0';

  if (!type_name || !data) {
    fmt(error_, sizeof(error_), "Invalid arguments");
    return nullptr;
  }

  const ParsedDeclaration *decl = nullptr;
  for (const auto &d : declarations_) {
    if (d.type_name == type_name) {
      decl = &d;
      break;
    }
  }

  if (!decl) {
    fmt(error_, sizeof(error_), "Declaration not found for type: {}", type_name);
    return nullptr;
  }

  turbo_csv_options_t opts;
  opts.has_header = true;
  opts.delimiter = ',';
  opts.quote = '"';
  opts.skip_empty_rows = true;

  turbo_csv_doc_t* doc = nullptr;
  if (turbo_parse_csv_opts(data, len, &opts, &doc) != 0) {
    fmt(error_, sizeof(error_), "Failed to parse CSV");
    return nullptr;
  }

  size_t row_count = turbo_csv_row_count(doc);

  Value* list_val = api_.create_list();
  if (!list_val) {
    fmt(error_, sizeof(error_), "Failed to create CSV result list");
    turbo_free_csv(&doc);
    return nullptr;
  }

  for (size_t r = 0; r < row_count; r++) {
      Value* row_val = api_.create_object();
      if (!row_val) {
        fmt(error_, sizeof(error_), "Failed to create row object at row {}", r);
        destroy_value(api_, list_val);
        turbo_free_csv(&doc);
        return nullptr;
      }

      for (const auto &field : decl->fields) {
          size_t c = turbo_csv_find_column(doc, field.name.c_str());
          if (c == (size_t)-1) continue;
          const char* cell = turbo_csv_get(doc, r, c);
          const std::string_view raw_cell = cell ? std::string_view(cell) : std::string_view{};
          const std::string_view trimmed_cell = trim_ascii(raw_cell);

          if (field.type == FT_String || field.type == FT_Unknown) {
              if (cell) {
                  api_.set_field_string(row_val, field.name.c_str(), cell);
              }
          } else if (field.type == FT_Int) {
              int32_t num = 0;
              if (trimmed_cell.empty()) continue;
              if (!parse_csv_int32(trimmed_cell, &num)) {
                  fmt(error_, sizeof(error_), "Invalid int for CSV field '{}.{}' at row {}: '{}'",
                      decl->type_name, field.name, r, raw_cell);
                  destroy_value(api_, row_val);
                  destroy_value(api_, list_val);
                  turbo_free_csv(&doc);
                  return nullptr;
              }
              api_.set_field_int(row_val, field.name.c_str(), num);
          } else if (field.type == FT_Long) {
              int64_t num = 0;
              if (trimmed_cell.empty()) continue;
              if (!parse_csv_int64(trimmed_cell, &num)) {
                  fmt(error_, sizeof(error_), "Invalid long for CSV field '{}.{}' at row {}: '{}'",
                      decl->type_name, field.name, r, raw_cell);
                  destroy_value(api_, row_val);
                  destroy_value(api_, list_val);
                  turbo_free_csv(&doc);
                  return nullptr;
              }
              api_.set_field_int64(row_val, field.name.c_str(), num);
          } else if (field.type == FT_Double || field.type == FT_Float || field.type == FT_Number) {
              double num = 0.0;
              if (trimmed_cell.empty()) continue;
              if (!parse_csv_double(trimmed_cell, &num)) {
                  fmt(error_, sizeof(error_), "Invalid double for CSV field '{}.{}' at row {}: '{}'",
                      decl->type_name, field.name, r, raw_cell);
                  destroy_value(api_, row_val);
                  destroy_value(api_, list_val);
                  turbo_free_csv(&doc);
                  return nullptr;
              }
              api_.set_field_double(row_val, field.name.c_str(), num);
          } else if (field.type == FT_Boolean) {
              bool b = false;
              if (trimmed_cell.empty()) continue;
              if (!parse_csv_bool(trimmed_cell, &b)) {
                  fmt(error_, sizeof(error_), "Invalid boolean for CSV field '{}.{}' at row {}: '{}'",
                      decl->type_name, field.name, r, raw_cell);
                  destroy_value(api_, row_val);
                  destroy_value(api_, list_val);
                  turbo_free_csv(&doc);
                  return nullptr;
              }
              api_.set_field_int(row_val, field.name.c_str(), b ? 1 : 0);
          } else {
              fmt(error_, sizeof(error_), "Unsupported CSV field type for '{}.{}'", decl->type_name, field.name);
              destroy_value(api_, row_val);
              destroy_value(api_, list_val);
              turbo_free_csv(&doc);
              return nullptr;
          }
      }

      api_.add_list_item_object(list_val, row_val);
  }

  turbo_free_csv(&doc);
  error_[0] = '\0';
  return list_val;
}

Value *CsvCodec::parse_string(const char *type_name, const char *data) {
  if (!type_name || !data) {
    fmt(error_, sizeof(error_), "Invalid arguments");
    return nullptr;
  }
  return parse(type_name, reinterpret_cast<const uint8_t *>(data), std::strlen(data));
}
