# Native Function Registration in RulesForge

This document explains how to register and use native C functions in RulesForge rules.

Related complete guides:

- EN: `CAPI_NATIVE_DLL_EN.md`
- ZH: `CAPI_NATIVE_DLL_ZH.md`
- This page in Chinese: `NATIVE_FUNCTIONS_ZH.md`

## Overview

RulesForge allows you to register custom C functions that can be called from rule RHS expressions. This is useful for:

- **Custom Calculations**: Implement domain-specific math or logic in C
- **External System Integration**: Call any C/C++ library or system API
- **Performance-Critical Operations**: Implement computationally intensive operations in C

## API Reference

### Function Signature

```c
typedef ruleforge_status_t (*ruleforge_native_function_t)(
    void *ctx,           // User-provided context
    int argc,            // Number of arguments
    const char **argv,   // Array of JSON-encoded argument strings
    char **out_result    // Output result (JSON string, caller must free with free())
);
```

### Registration

```c
ruleforge_status_t ruleforge_kb_register_native_function(
    ruleforge_knowledge_base_t kb,
    const char *function_name,
    ruleforge_native_function_t callback,
    void *user_data
);
```

### DLL Function Table Registration

You can also ship multiple native functions from a plugin DLL/.so and load them in one call:

```c
ruleforge_status_t ruleforge_kb_load_native_function_table(
    ruleforge_knowledge_base_t kb,
    const char *library_path,
    const char *symbol_name /* pass NULL for default symbol */
);
```

Plugin export contract:

```c
ruleforge_status_t ruleforge_get_function_table(ruleforge_plugin_function_table_t *out_table);
```

**Parameters:**
- `kb`: Knowledge base handle
- `function_name`: Name to use in rules
- `callback`: C function pointer
- `user_data`: Optional context passed to callback (can be NULL)

**Returns:** `RULES_FORGE_OK` on success

## Usage Example

### 1. Define Your Native Function

```c
ruleforge_status_t calculate_tax(void *ctx, int argc, const char **argv, char **out_result) {
    if (argc < 1) {
        *out_result = strdup("{\"error\": \"requires amount\"}");
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    double amount = atof(argv[0]);
    double tax = amount * 0.08;

    char buffer[256];
    fmt(buffer, sizeof(buffer), "{:.2f}", tax);
    *out_result = strdup(buffer);
    return RULES_FORGE_OK;
}
```

### 2. Register the Function

```c
ruleforge_knowledge_base_t kb;
ruleforge_kb_create(&kb);

ruleforge_kb_register_native_function(kb, "calculateTax", calculate_tax, nullptr);
```

### 3. Use in Rules

Native functions can be used in RHS expressions where a value is expected:

```rfl
rule "Apply Tax"
when
    $order : Order(status == "pending")
then
    update $order {
        tax = calculateTax($order.subtotal),
        total = $order.subtotal + calculateTax($order.subtotal)
    }
end
```

## Advanced Examples

### Function with Context

```c
struct DatabaseContext {
    void *db_connection;
    const char *table_name;
};

ruleforge_status_t lookup_rate(void *ctx, int argc, const char **argv, char **out_result) {
    DatabaseContext *db_ctx = (DatabaseContext *)ctx;

    if (argc < 1) {
        *out_result = strdup("0.0");
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    // Look up rate from database using context
    double rate = db_lookup_rate(db_ctx->db_connection, argv[0]);

    char buffer[64];
    fmt(buffer, sizeof(buffer), "{:.4f}", rate);
    *out_result = strdup(buffer);
    return RULES_FORGE_OK;
}

// Registration with context
DatabaseContext db_ctx = {my_db_connection, "rates"};
ruleforge_kb_register_native_function(kb, "lookupRate", lookup_rate, &db_ctx);
```

**Usage in rules:**

```rfl
rule "Apply Dynamic Rate"
when
    $loan : Loan(status == "active")
then
    update $loan {
        rate = lookupRate($loan.category),
        payment = $loan.principal * lookupRate($loan.category) / 12
    }
end
```

### Calculation with Multiple Arguments

```c
ruleforge_status_t weighted_score(void *ctx, int argc, const char **argv, char **out_result) {
    if (argc < 3) {
        *out_result = strdup("0.0");
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    double score1 = atof(argv[0]);
    double score2 = atof(argv[1]);
    double weight = atof(argv[2]);

    double result = score1 * weight + score2 * (1.0 - weight);

    char buffer[64];
    fmt(buffer, sizeof(buffer), "{:.2f}", result);
    *out_result = strdup(buffer);
    return RULES_FORGE_OK;
}
```

**Usage in rules:**

```rfl
rule "Calculate Final Score"
when
    $student : Student()
    not FinalScore(studentId == $student.id)
then
    insert FinalScore {
        studentId = $student.id,
        score = weightedScore($student.exam, $student.homework, 0.7)
    }
end
```

## Important Notes

### Memory Management

1. **Input Arguments (`argv`)**: Read-only, managed by RulesForge. Do not free.
2. **Output Result (`out_result`)**:
   - Allocate with `malloc()` or `strdup()`
   - RulesForge will call `free()` on it
   - Can be `nullptr` if no return value needed

### Argument Format

- Arguments are passed as JSON-encoded strings
- Use JSON parsing library to extract complex data
- Simple values can be parsed with `atoi()`, `atof()`, etc.

### Return Values

- Return `RULES_FORGE_OK` (0) for success
- Return error codes for failures
- Set `*out_result` to error message on failure

### Thread Safety

- Native functions may be called from multiple threads
- Ensure your implementation is thread-safe if needed
- Use context (`ctx`) to pass thread-local data

## Complete Example

See `native_functions_demo.cpp` for a complete working example.

## Building the Example

```bash
cmake --build build --target native_functions_demo
./build/bin/native_functions_demo
```

## Error Handling

Always check return values and handle errors:

```c
if (ruleforge_kb_register_native_function(kb, "myFunc", my_func, ctx) != RULES_FORGE_OK) {
    fprintf(stderr, "Failed to register function: %s\n",
            ruleforge_get_last_error_message());
    return 1;
}
```

## Best Practices

1. **Keep functions simple**: Native functions should be lightweight
2. **Validate inputs**: Always check `argc` and validate arguments
3. **Return meaningful errors**: Use descriptive error messages
4. **Test thoroughly**: Test with various input combinations
5. **Consider performance**: Native functions are called during rule execution

## Limitations

- Function names must be valid identifiers
- Arguments are passed as strings (JSON-encoded)
- Functions are registered per knowledge base (not per session)
- Registration must happen before loading rules
- Native functions can only be used in RHS expressions (e.g., field values in `insert`/`update`), not as standalone statements

