/**
 * @file json_codec.h
 * @brief JSON format codec using TurboNet::Parser
 */

#ifndef JSON_CODEC_H
#define JSON_CODEC_H

#include "data_bind.h"
#include "turbo_parser.h"
#include <string>
#include <vector>

// Forward declarations
struct ParsedDeclaration;
struct ParsedField;
struct ParsedEnum;

/**
 * @brief JSON codec implementation using TurboNet::Parser
 */
class JsonCodec {
public:
  JsonCodec(const std::vector<ParsedDeclaration> &declarations,
            const std::vector<ParsedEnum> &enums,
            const DataBindValueApi *api);
  ~JsonCodec() = default;

  Value *parse(const char *type_name, const uint8_t *data, size_t len);
  Value *parse_string(const char *type_name, const char *data);
  const char *get_error() const;
  bool is_valid() const { return error_[0] == '\0'; }
  DataBindFormat get_format() const { return DATA_BIND_FORMAT_JSON; }

  Value *parse_nested_object(const json_value_t *json_obj, const std::string &type_name);

private:
  std::vector<ParsedDeclaration> declarations_;
  std::vector<ParsedEnum> enums_;
  DataBindValueApi api_;
  mutable char error_[256];
};

#endif /* JSON_CODEC_H */
