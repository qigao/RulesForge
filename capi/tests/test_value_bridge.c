/**
 * @file test_value_bridge.c
 * @brief Test C API bridge for binary codec integration
 */

#include "rfl_value_bridge.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>

int main(void) {
    printf("=== RFL Value Bridge Test ===\n\n");

    /* Test 1: Create and set fields */
    printf("Test 1: Create Value and set fields\n");
    Value* obj = value_create_object();
    assert(obj != NULL);

    value_set_field_int(obj, "id", 42);
    value_set_field_bool(obj, "active", 1);
    value_set_field_double(obj, "price", 99.99);
    value_set_field_string(obj, "name", "TestProduct");
    value_set_field_uint64(obj, "counter", UINT64_MAX);
    const uint8_t raw[] = {'A', 'z'};
    value_set_field_bytes(obj, "raw", raw, sizeof(raw));

    printf("  Created Value with 4 fields\n");
    printf("  PASSED\n\n");

    /* Test 2: Get fields */
    printf("Test 2: Get field values\n");
    int64_t id = value_get_field_int(obj, "id");
    int active = value_get_field_bool(obj, "active");
    double price = value_get_field_double(obj, "price");
    const char* name = value_get_field_string(obj, "name");
    uint64_t counter = value_get_field_uint64(obj, "counter");
    uint8_t raw_copy[2] = {0};
    size_t raw_length = 0;

    printf("  id = %" PRId64 " (expected 42)\n", id);
    printf("  active = %d (expected 1)\n", active);
    printf("  price = %.2f (expected 99.99)\n", price);
    printf("  name = %s (expected TestProduct)\n", name);

    assert(id == 42);
    assert(active == 1);
    assert(value_get_field_bool(obj, "id") == 0);
    assert(price == 99.99);
    assert(strcmp(name, "TestProduct") == 0);
    if (counter != UINT64_MAX) return 1;
    if (!value_get_field_bytes(obj, "raw", raw_copy, sizeof(raw_copy), &raw_length)) return 1;
    if (raw_length != 2 || memcmp(raw, raw_copy, 2) != 0) return 1;

    printf("  PASSED\n\n");

    /* Test 3: Get non-existent field */
    printf("Test 3: Get non-existent field\n");
    int64_t missing = value_get_field_int(obj, "nonexistent");
    printf("  missing = %" PRId64 " (expected 0)\n", missing);
    assert(missing == 0);
    printf("  PASSED\n\n");

    /* Test 4: Free */
    printf("Test 4: Free Value\n");
    value_free(obj);
    printf("  PASSED\n\n");

    printf("=== All Tests Passed ===\n");
    return 0;
}
