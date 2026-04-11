#ifndef CSV_CODEC_H
#define CSV_CODEC_H

#include "data_bind.h"
#include <turbo_parser.h>
#include <string>
#include <vector>

#include "core/constraint_types.hpp"

/**
 * @brief CSV codec implementation using TurboNet::Parser
 */
class CsvCodec {
public:
  CsvCodec(const std::vector<ParsedDeclaration> &declarations,
           const std::vector<ParsedEnum> &enums,
           const DataBindValueApi *api);
  ~CsvCodec() = default;

  Value *parse(const char *type_name, const uint8_t *data, size_t len);
  Value *parse_string(const char *type_name, const char *data);
  const char *get_error() const;
  bool is_valid() const { return error_[0] == '\0'; }
  DataBindFormat get_format() const { return DATA_BIND_FORMAT_CSV; }

private:
  std::vector<ParsedDeclaration> declarations_;
  std::vector<ParsedEnum> enums_;
  DataBindValueApi api_;
  mutable char error_[256];
};

#endif /* CSV_CODEC_H */
