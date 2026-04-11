/**
 * @file binary_codec.h
 * @brief Binary format codec with MIR JIT compilation
 */

#ifndef BINARY_CODEC_H
#define BINARY_CODEC_H

#include "data_bind.h"
#include <mir.h>
#include <vector>
#include <string>
#include <unordered_map>

// Forward declarations
struct ParsedDeclaration;
struct ParsedEnum;

/**
 * @brief Binary codec implementation using MIR JIT
 */
class BinaryCodec {
public:
    BinaryCodec(const std::vector<ParsedDeclaration>& declarations,
                const std::vector<ParsedEnum>& enums,
                const DataBindValueApi* api);
    ~BinaryCodec();

    Value* parse(const char* type_name, const uint8_t* data, size_t len);
    const char* get_error() const;
    bool is_valid() const { return error_[0] == '\0'; }
    DataBindFormat get_format() const { return DATA_BIND_FORMAT_BINARY; }

private:
    MIR_module_t compile_and_link();
    void register_parse_functions(MIR_module_t module);

    MIR_context_t ctx_;
    std::vector<ParsedDeclaration> declarations_;
    std::vector<ParsedEnum> enums_;
    std::unordered_map<std::string, void*> func_map_;
    DataBindValueApi api_;
    mutable char error_[256];
};

#endif /* BINARY_CODEC_H */
