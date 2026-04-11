/**
 * @file data_bind.h
 * @brief Multi-format data binding with high-performance parsing
 */

#ifndef DATA_BIND_H
#define DATA_BIND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef struct DataBind DataBind;
typedef struct Value Value;

/**
 * @brief Supported data formats
 */
typedef enum {
    DATA_BIND_FORMAT_BINARY,  /* Binary format with MIR JIT compilation */
    DATA_BIND_FORMAT_JSON,    /* JSON format */
    DATA_BIND_FORMAT_CSV      /* CSV format */
} DataBindFormat;

/**
 * @brief Value API function pointers (provided by host application)
 *
 * Callbacks are required by the schema and format being parsed.
 * Primitive-only schemas can omit container callbacks; schemas using List/Set/Map/Object
 * must provide the matching hooks. `destroy_value` is required by Binary, JSON, and CSV codecs
 * so parse failures can clean up partial values instead of leaking them.
 */
typedef struct DataBindValueApi {
    /* Core API (required) */
    Value* (*create_object)(void);
    void   (*set_field_int)(Value* obj, const char* name, int32_t val);
    void   (*set_field_int64)(Value* obj, const char* name, int64_t val);
    void   (*set_field_double)(Value* obj, const char* name, double val);
    void   (*set_field_string)(Value* obj, const char* name, const char* val);
    void   (*set_field_bytes)(Value* obj, const char* name, const uint8_t* data, size_t len);

    /* Extended API for schema features introduced in v1.1 (required when schema uses them) */
    Value* (*create_list)(void);
    void   (*add_list_item_int)(Value* list, int32_t val);
    void   (*add_list_item_int64)(Value* list, int64_t val);
    void   (*add_list_item_double)(Value* list, double val);
    void   (*add_list_item_string)(Value* list, const char* val);
    void   (*add_list_item_object)(Value* list, Value* obj);
    void   (*set_field_list)(Value* obj, const char* name, Value* list);
    void   (*set_field_object)(Value* obj, const char* name, Value* child);

    /* Extended API for Set support introduced in v1.2 (required when schema uses Set) */
    Value* (*create_set)(void);
    void   (*add_set_item_int)(Value* set, int32_t val);
    void   (*add_set_item_double)(Value* set, double val);
    void   (*add_set_item_string)(Value* set, const char* val);
    void   (*set_field_set)(Value* obj, const char* name, Value* set);

    /* Extended API for Map support introduced in v1.2 (required when schema uses Map) */
    Value* (*create_map)(void);
    void   (*add_map_entry_string_string)(Value* map, const char* key, const char* val);
    void   (*add_map_entry_string_int)(Value* map, const char* key, int32_t val);
    void   (*add_map_entry_string_double)(Value* map, const char* key, double val);
    void   (*set_field_map)(Value* obj, const char* name, Value* map);

    /* Extended API for int64 support introduced in v1.3 (required when schema uses int64 Set/Map values) */
    void   (*add_set_item_int64)(Value* set, int64_t val);
    void   (*add_map_entry_string_int64)(Value* map, const char* key, int64_t val);

    /* Cleanup hook for temporary values created during parse failure paths */
    void   (*destroy_value)(Value* value);
} DataBindValueApi;

/**
 * @brief Create codec from RulesForge DSL file with format specification
 * @param rfl_path Path to .rfl file
 * @param format Data format to parse
 * @param api Value API function pointers (must remain valid for codec lifetime)
 * @return Codec instance, or NULL on failure
 */
 DataBind* data_bind_create_ex(const char* rfl_path, DataBindFormat format, const DataBindValueApi* api);

/**
 * @brief Create codec from RulesForge DSL file (binary format, backward compatible)
 * @param rfl_path Path to .rfl file
 * @param api Value API function pointers (must remain valid for codec lifetime)
 * @return Codec instance, or NULL on failure
 */
 DataBind* data_bind_create(const char* rfl_path, const DataBindValueApi* api);

/**
 * @brief Create codec from in-memory declarations (zero-copy, no file I/O)
 * @param declarations Array of ParsedDeclaration pointers (cast to void*)
 * @param count Number of declarations
 * @param format Data format to parse
 * @param api Value API function pointers (must remain valid for codec lifetime)
 * @return Codec instance, or NULL on failure
 */
 DataBind* data_bind_create_from_declarations(
    const void** declarations,
    size_t count,
    DataBindFormat format,
    const DataBindValueApi* api);

/**
 * @brief Create codec from in-memory declarations and enums
 * @param declarations Array of ParsedDeclaration pointers (cast to void*)
 * @param declaration_count Number of declarations
 * @param enums Array of ParsedEnum pointers (cast to void*), may be NULL
 * @param enum_count Number of enums
 * @param format Data format to parse
 * @param api Value API function pointers (must remain valid for codec lifetime)
 * @return Codec instance, or NULL on failure
 */
 DataBind* data_bind_create_from_declarations_and_enums(
    const void** declarations,
    size_t declaration_count,
    const void** enums,
    size_t enum_count,
    DataBindFormat format,
    const DataBindValueApi* api);

/**
 * @brief Free codec
 */
 void data_bind_free(DataBind* codec);

/**
 * @brief Parse data to Value object (binary format)
 * @param codec Codec instance
 * @param type_name Message type name (e.g. "Order")
 * @param buf Data buffer
 * @param len Data length
 * @return Value object, or NULL on failure
 */
 Value* data_bind_parse(DataBind* codec, const char* type_name,
                        const uint8_t* buf, size_t len);

/**
 * @brief Parse string data to Value object (JSON/CSV format)
 * @param codec Codec instance
 * @param type_name Message type name (e.g. "Order")
 * @param data String data (null-terminated)
 * @return Value object, or NULL on failure
 */
 Value* data_bind_parse_string(DataBind* codec, const char* type_name, const char* data);

/**
 * @brief Get last error message
 */
 const char* data_bind_get_error(DataBind* codec);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_H */
