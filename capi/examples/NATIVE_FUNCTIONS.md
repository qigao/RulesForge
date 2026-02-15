# Native Function Registration in RuleForge

This document explains how to register and use native C functions in RuleForge rules.

## Overview

RuleForge allows you to register custom C functions that can be called directly from rule actions (RHS). This is useful for:

- **MQTT Integration**: Republish messages to different topics
- **HTTP Webhooks**: Send alerts or notifications to external services
- **Custom Logging**: Implement application-specific logging
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

**Parameters:**
- `kb`: Knowledge base handle
- `function_name`: Name to use in rules (e.g., "republish", "webhook")
- `callback`: C function pointer
- `user_data`: Optional context passed to callback (can be NULL)

**Returns:** `RULES_FORGE_OK` on success

## Usage Example

### 1. Define Your Native Function

```c
ruleforge_status_t log_function(void *ctx, int argc, const char **argv, char **out_result) {
    // Print all arguments
    printf("[LOG] ");
    for (int i = 0; i < argc; i++) {
        printf("%s ", argv[i]);
    }
    printf("\n");

    // No return value needed
    *out_result = nullptr;
    return RULES_FORGE_OK;
}
```

### 2. Register the Function

```c
ruleforge_knowledge_base_t kb;
ruleforge_kb_create(&kb);

// Register the function
ruleforge_kb_register_native_function(kb, "log", log_function, nullptr);
```

### 3. Use in Rules

```javascript
rule "Example Rule"
when
    $sensor : Sensor(temperature > 25)
then
    // Call your native function
    log("High temperature:", $sensor.temperature);
end
```

## Advanced Examples

### MQTT Republish

```c
ruleforge_status_t republish_function(void *ctx, int argc, const char **argv, char **out_result) {
    if (argc < 2) {
        *out_result = strdup("{\"error\": \"requires topic and message\"}");
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    const char *topic = argv[0];
    const char *message = argv[1];

    // Your MQTT publish code here
    mqtt_publish(topic, message);

    *out_result = strdup("{\"published\": true}");
    return RULES_FORGE_OK;
}
```

**Usage in rules:**
```javascript
rule "Republish Alert"
when
    $alert : Alert(severity == "high")
then
    republish("alerts/high", JSON.stringify($alert));
end
```

### HTTP Webhook

```c
ruleforge_status_t webhook_function(void *ctx, int argc, const char **argv, char **out_result) {
    if (argc < 2) {
        *out_result = strdup("{\"error\": \"requires url and payload\"}");
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    const char *url = argv[0];
    const char *payload = argv[1];

    // Make HTTP POST request
    int status_code = http_post(url, payload);

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "{\"status\": %d}", status_code);
    *out_result = strdup(buffer);

    return RULES_FORGE_OK;
}
```

**Usage in rules:**
```javascript
rule "Send Webhook"
when
    $event : Event(type == "critical")
then
    var response = webhook("https://api.example.com/alert",
                          JSON.stringify($event));
    log("Webhook response:", JSON.stringify(response));
end
```

### Function with Context

```c
struct DatabaseContext {
    void *db_connection;
    const char *table_name;
};

ruleforge_status_t save_to_db(void *ctx, int argc, const char **argv, char **out_result) {
    DatabaseContext *db_ctx = (DatabaseContext *)ctx;

    if (argc < 1) {
        *out_result = strdup("{\"error\": \"requires data\"}");
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    // Save to database using context
    bool success = db_insert(db_ctx->db_connection,
                            db_ctx->table_name,
                            argv[0]);

    *out_result = strdup(success ? "{\"saved\": true}" : "{\"saved\": false}");
    return RULES_FORGE_OK;
}

// Registration with context
DatabaseContext db_ctx = {my_db_connection, "events"};
ruleforge_kb_register_native_function(kb, "saveToDb", save_to_db, &db_ctx);
```

## Important Notes

### Memory Management

1. **Input Arguments (`argv`)**: Read-only, managed by RuleForge. Do not free.
2. **Output Result (`out_result`)**:
   - Allocate with `malloc()` or `strdup()`
   - RuleForge will call `free()` on it
   - Can be `nullptr` if no return value needed

### Argument Format

- Arguments are passed as JSON-encoded strings
- Use JSON parsing library to extract complex data
- Simple values can be parsed with `atoi()`, `atof()`, etc.

### Return Values

- Return `RULES_FORGE_OK` (0) for success
- Return error codes for failures
- Set `*out_result` to error message JSON on failure

### Thread Safety

- Native functions may be called from multiple threads
- Ensure your implementation is thread-safe if needed
- Use context (`ctx`) to pass thread-local data

## Complete Example

See `native_functions_demo.cpp` for a complete working example demonstrating:
- Simple logging
- Webhook simulation
- MQTT republish simulation
- Custom calculations with context

## Building the Example

```bash
cmake --build build --target native_functions_demo
./build/capi/examples/native_functions_demo
```

## Integration with MQTT Rules Plugin

For MQTT integration, register functions like:

```c
// Register MQTT-specific functions
ruleforge_kb_register_native_function(kb, "republish", mqtt_republish, mqtt_client);
ruleforge_kb_register_native_function(kb, "webhook", http_webhook, http_client);
ruleforge_kb_register_native_function(kb, "log", custom_log, log_context);
```

Then use in rules:

```javascript
rule "Process Sensor Data"
when
    $sensor : SensorData(temperature > threshold)
then
    // Log the event
    log("Threshold exceeded:", $sensor.temperature);

    // Republish to alert topic
    republish("sensors/alerts", JSON.stringify({
        id: $sensor.id,
        temp: $sensor.temperature,
        timestamp: Date.now()
    }));

    // Send webhook notification
    webhook("https://monitoring.example.com/alert", JSON.stringify($sensor));
end
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
2. **Use async operations**: For I/O operations, consider async patterns
3. **Validate inputs**: Always check `argc` and validate arguments
4. **Return meaningful errors**: Use JSON error objects with descriptive messages
5. **Document your functions**: Provide clear documentation for rule authors
6. **Test thoroughly**: Test with various input combinations
7. **Consider performance**: Native functions are called during rule execution

## Limitations

- Function names must be valid JavaScript identifiers
- Arguments are passed as strings (JSON-encoded)
- Return values must be JSON-serializable
- Functions are registered per knowledge base (not per session)
- Registration must happen before loading rules

## Future Enhancements

Potential future improvements:
- Async function support
- Streaming/callback-based functions
- Type-safe argument passing
- Automatic JSON serialization/deserialization
- Function overloading
