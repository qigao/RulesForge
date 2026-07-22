/**
 * @file rfl_value_bridge.h
 * @brief C ABI value bridge for RulesForge fact construction
 *
 * Provides C-compatible Value API that wraps RulesForge's C++ ConstraintValue.
 * Host code and native integration layers can use this bridge to build a
 * structured Value tree and convert it to a RulesForge Fact before insertion.
 */

#ifndef RFL_VALUE_BRIDGE_H
#define RFL_VALUE_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque Value handle.
 */
typedef struct Value Value;

/**
 * @brief Create empty Value object
 * @return Value pointer, or NULL on allocation failure
 */
Value* value_create_object(void);

/**
 * @brief Free Value object
 */
void value_free(Value* obj);

/**
 * @brief Set 64-bit integer field.
 */
void value_set_field_int(Value* obj, const char* name, int64_t val);

/**
 * @brief Set 32-bit integer field.
 */
void value_set_field_int32(Value* obj, const char* name, int32_t val);

/**
 * @brief Set 64-bit integer field.
 */
void value_set_field_int64(Value* obj, const char* name, int64_t val);

/**
 * @brief Set an exact unsigned 64-bit field.
 */
void value_set_field_uint64(Value* obj, const char* name, uint64_t val);

/**
 * @brief Set boolean field. Zero is false; any non-zero value is true.
 */
void value_set_field_bool(Value* obj, const char* name, int val);

/**
 * @brief Set double field
 */
void value_set_field_double(Value* obj, const char* name, double val);

/**
 * @brief Set string field (null-terminated)
 */
void value_set_field_string(Value* obj, const char* name, const char* val);

/**
 * @brief Set bytes field
 */
void value_set_field_bytes(Value* obj, const char* name, const uint8_t* data, size_t len);

/**
 * @brief Set nested object field
 * @note The nested value is consumed by this call.
 */
void value_set_field_object(Value* obj, const char* name, Value* nested);

/**
 * @brief Create a list value.
 */
Value* value_create_list(void);

void value_add_list_item_int(Value* list, int32_t val);
void value_add_list_item_int64(Value* list, int64_t val);
void value_add_list_item_uint64(Value* list, uint64_t val);
void value_add_list_item_bool(Value* list, int val);
void value_add_list_item_double(Value* list, double val);
void value_add_list_item_string(Value* list, const char* val);

/**
 * @brief Append an object value to a list.
 * @note The object value is consumed by this call.
 */
void value_add_list_item_object(Value* list, Value* obj);

/**
 * @brief Set list field.
 * @note The list value is consumed by this call.
 */
void value_set_field_list(Value* obj, const char* name, Value* list);

/**
 * @brief Create a set value.
 */
Value* value_create_set(void);

void value_add_set_item_int(Value* set, int32_t val);
void value_add_set_item_uint64(Value* set, uint64_t val);
void value_add_set_item_bool(Value* set, int val);
void value_add_set_item_double(Value* set, double val);
void value_add_set_item_string(Value* set, const char* val);

/**
 * @brief Set set field.
 * @note The set value is consumed by this call.
 */
void value_set_field_set(Value* obj, const char* name, Value* set);

/**
 * @brief Create a map value.
 */
Value* value_create_map(void);

void value_add_map_entry_string_string(Value* map, const char* key, const char* val);
void value_add_map_entry_string_int(Value* map, const char* key, int32_t val);
void value_add_map_entry_string_uint64(Value* map, const char* key, uint64_t val);
void value_add_map_entry_string_bool(Value* map, const char* key, int val);
void value_add_map_entry_string_double(Value* map, const char* key, double val);

/**
 * @brief Set map field.
 * @note The map value is consumed by this call.
 */
void value_set_field_map(Value* obj, const char* name, Value* map);

/**
 * @brief Get integer field
 * @return Field value, or 0 if not found
 */
int64_t value_get_field_int(const Value* obj, const char* name);

/**
 * @brief Get an exact unsigned 64-bit field.
 * @return Field value, or 0 if missing or not uint64.
 */
uint64_t value_get_field_uint64(const Value* obj, const char* name);

/**
 * @brief Copy an exact bytes field.
 * @return 1 on success, 0 if missing, wrong type, or the buffer is too small.
 */
int value_get_field_bytes(const Value* obj, const char* name, uint8_t* buffer,
                          size_t buffer_size, size_t* out_length);

/**
 * @brief Get boolean field.
 * @return 1 for true, or 0 for false, a missing field, or a non-boolean field
 */
int value_get_field_bool(const Value* obj, const char* name);

/**
 * @brief Get double field
 * @return Field value, or 0.0 if not found
 */
double value_get_field_double(const Value* obj, const char* name);

/**
 * @brief Get string field
 * @return Field value, or empty string if not found
 * @note Returned pointer is valid until Value is modified or freed
 */
const char* value_get_field_string(const Value* obj, const char* name);

/**
 * @brief Get nested object field
 * @return Field value, or NULL if not found
 * @note Caller owns the returned Value and must call value_free().
 */
Value* value_get_field_object(const Value* obj, const char* name);

/**
 * @brief Convert Value to RulesForge Fact
 * @param obj Value object
 * @param type_name Fact type name (must match declared type)
 * @return Fact pointer, or NULL on failure
 * @note Caller owns the returned Fact and must call value_free_fact().
 */
struct Fact* value_to_fact(Value* obj, const char* type_name);

/**
 * @brief Free a Fact returned by value_to_fact().
 */
void value_free_fact(struct Fact* fact);

#ifdef __cplusplus
}
#endif

#endif /* RFL_VALUE_BRIDGE_H */
