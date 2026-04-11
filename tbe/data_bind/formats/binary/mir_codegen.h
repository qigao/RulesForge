/**
 * @file mir_codegen.h
 * @brief Direct MIR IR generation for binary parse functions
 *
 * Replaces the Mustache→C→c2mir pipeline with direct MIR API calls.
 * For each ParsedDeclaration, emits a parse_<TypeName> function that
 * reads fields from a binary buffer and calls Value API callbacks.
 */

#ifndef MIR_CODEGEN_H
#define MIR_CODEGEN_H

#include <mir.h>
#include <vector>
#include <string>

struct ParsedDeclaration;
struct ParsedEnum;

struct BinaryAbiRequirements {
    bool set_int = false;
    bool set_int64 = false;
    bool set_dbl = false;
    bool set_str = false;
    bool read_varstr = false;
    bool free_string = false;
    bool create_list = false;
    bool add_list_int = false;
    bool add_list_int64 = false;
    bool add_list_dbl = false;
    bool add_list_str = false;
    bool set_list = false;
    bool create_set = false;
    bool add_set_int = false;
    bool add_set_int64 = false;
    bool add_set_dbl = false;
    bool add_set_str = false;
    bool set_set = false;
    bool create_map = false;
    bool add_map_str_str = false;
    bool add_map_str_int = false;
    bool add_map_str_int64 = false;
    bool add_map_str_dbl = false;
    bool set_map = false;
};

bool collect_binary_abi_requirements(const std::vector<ParsedDeclaration>& decls,
                                     const std::vector<ParsedEnum>& enums,
                                     BinaryAbiRequirements& requirements,
                                     std::string* error_message = nullptr);

/**
 * @brief Generate MIR module with parse functions for all declarations.
 *
 * Creates a MIR module containing one parse_<TypeName> function per declaration.
 * Each function has signature: void* (const uint8_t* buf, size_t len)
 *
 * @param ctx MIR context (must be initialized)
 * @param decls Parsed declarations from RFL schema
 * @param enums Parsed enums from RFL schema
 * @param has_set_bytes Whether the Value API supports set_field_bytes
 * @return The generated MIR module, or nullptr on failure
 */
MIR_module_t mir_generate_parsers(MIR_context_t ctx,
                                   const std::vector<ParsedDeclaration>& decls,
                                   const std::vector<ParsedEnum>& enums,
                                   std::string* error_message = nullptr);

#endif /* MIR_CODEGEN_H */
