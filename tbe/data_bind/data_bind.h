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
 * New API functions (v1.1) are optional - set to NULL if not supported.
 * This allows backward compatibility with existing implementations.
 */
typedef struct DataBindValueApi {
    /* Core API (required) */
    Value* (*create_object)(void);
    void   (*set_field_int)(Value* obj, const char* name, int32_t val);
    void   (*set_field_double)(Value* obj, const char* name, double val);
    void   (*set_field_string)(Value* obj, const char* name, const char* val);
    void   (*set_field_bytes)(Value* obj, const char* name, const uint8_t* data, size_t len);

    /* Extended API for complex types (optional, v1.1) */
    Value* (*create_list)(void);
    void   (*add_list_item_int)(Value* list, int32_t val);
    void   (*add_list_item_double)(Value* list, double val);
    void   (*add_list_item_string)(Value* list, const char* val);
    void   (*add_list_item_object)(Value* list, Value* obj);
    void   (*set_field_list)(Value* obj, const char* name, Value* list);
    void   (*set_field_object)(Value* obj, const char* name, Value* child);

    /* Extended API for Set (optional, v1.2) */
    Value* (*create_set)(void);
    void   (*add_set_item_int)(Value* set, int32_t val);
    void   (*add_set_item_double)(Value* set, double val);
    void   (*add_set_item_string)(Value* set, const char* val);
    void   (*set_field_set)(Value* obj, const char* name, Value* set);

    /* Extended API for Map (optional, v1.2) */
    Value* (*create_map)(void);
    void   (*add_map_entry_string_string)(Value* map, const char* key, const char* val);
    void   (*add_map_entry_string_int)(Value* map, const char* key, int32_t val);
    void   (*add_map_entry_string_double)(Value* map, const char* key, double val);
    void   (*set_field_map)(Value* obj, const char* name, Value* map);
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
