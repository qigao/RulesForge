/**
 * @file rfl_value_bridge.h
 * @brief C API bridge for binary codec integration with RulesForge
 *
 * Provides C-compatible Value API that wraps RulesForge's C++ ConstraintValue.
 * Used by auto-generated binary codec code.
 */

#ifndef RFL_VALUE_BRIDGE_H
#define RFL_VALUE_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque Value handle (wraps std::map<string, ConstraintValue>)
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
 * @brief Set integer field
 */
void value_set_field_int(Value* obj, const char* name, int64_t val);

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
 */
void value_set_field_object(Value* obj, const char* name, Value* nested);

/**
 * @brief Get integer field
 * @return Field value, or 0 if not found
 */
int64_t value_get_field_int(const Value* obj, const char* name);

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
 * @note Returned pointer is owned by parent Value object
 */
Value* value_get_field_object(const Value* obj, const char* name);

/**
 * @brief Convert Value to RulesForge Fact
 * @param obj Value object
 * @param type_name Fact type name (must match declared type)
 * @return Fact pointer, or NULL on failure
 * @note Caller owns the returned Fact
 */
struct Fact* value_to_fact(Value* obj, const char* type_name);

#ifdef __cplusplus
}
#endif

#endif /* RFL_VALUE_BRIDGE_H */
