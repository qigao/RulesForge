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

// Forward declarations
struct ParsedDeclaration;

typedef struct mir_func_node {
    char* type_name;
    void* parse_fn;  /* JIT: Value* (*)(const uint8_t*, size_t) */
    struct mir_func_node* next;
} mir_func_node;

/**
 * @brief Binary codec implementation using MIR JIT
 */
class BinaryCodec {
public:
    BinaryCodec(const std::vector<ParsedDeclaration>& declarations,
                const DataBindValueApi* api);
    ~BinaryCodec();

    Value* parse(const char* type_name, const uint8_t* data, size_t len);
    const char* get_error() const;
    DataBindFormat get_format() const { return DATA_BIND_FORMAT_BINARY; }

private:
    bool compile_and_link(const char* c_source);
    void register_parse_functions(MIR_module_t module);

    MIR_context_t ctx_;
    std::vector<ParsedDeclaration> declarations_;
    mir_func_node* func_head_;
    DataBindValueApi api_;
    mutable char error_[256];
};

#endif /* BINARY_CODEC_H */
