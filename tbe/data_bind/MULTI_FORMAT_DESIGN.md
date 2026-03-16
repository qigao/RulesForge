# Multi-Format Data Bind Architecture

## Goal
Extend data_bind to support multiple data formats (Binary, JSON, CSV, etc.) with high-performance JIT compilation where applicable.

## Architecture

### Core Components

```
tbe/data_bind/
├── data_bind.h              # Unified API
├── data_bind.cpp            # Core implementation
├── formats/
│   ├── binary/
│   │   ├── binary_codec.cpp # MIR JIT binary parser (current)
│   │   └── binary_codec.h
│   ├── json/
│   │   ├── json_codec.cpp   # JSON parser
│   │   └── json_codec.h
│   └── csv/
│       ├── csv_codec.cpp    # CSV parser
│       └── csv_codec.h
└── CMakeLists.txt
```

### Unified API Design

```c
// Format types
typedef enum {
    DATA_BIND_FORMAT_BINARY,
    DATA_BIND_FORMAT_JSON,
    DATA_BIND_FORMAT_CSV
} DataBindFormat;

// Create codec with format specification
DataBind* data_bind_create_ex(
    const char* schema_path,
    DataBindFormat format,
    const DataBindValueApi* api
);

// Backward compatibility
DataBind* data_bind_create(
    const char* schema_path,
    const DataBindValueApi* api
) {
    return data_bind_create_ex(schema_path, DATA_BIND_FORMAT_BINARY, api);
}

// Parse data (format-agnostic)
Value* data_bind_parse(
    DataBind* codec,
    const char* type_name,
    const uint8_t* data,
    size_t len
);

// Format-specific parse (for string formats)
Value* data_bind_parse_string(
    DataBind* codec,
    const char* type_name,
    const char* data
);
```

### Internal Structure

```c
struct DataBind {
    DataBindFormat format;
    DataBindValueApi api;

    union {
        struct {
            MIR_context_t ctx;
            mir_func_node* func_head;
            std::vector<ParsedDeclaration> declarations;
        } binary;

        struct {
            // JSON parser state
            std::vector<ParsedDeclaration> declarations;
        } json;

        struct {
            // CSV parser state
            std::vector<ParsedDeclaration> declarations;
            char delimiter;
        } csv;
    } impl;

    char error[256];
};
```

## Implementation Plan

### Phase 1: Refactor Binary Codec ✅
- [x] Current binary codec works with RulesForge DSL
- [x] All tests passing

### Phase 2: Add JSON Support
1. Implement JSON codec using a JSON library (e.g., jsoncons, already in dependencies)
2. Parse RFL schema to understand field types
3. Map JSON values to Value API calls
4. Add JSON tests

### Phase 3: Add CSV Support
1. Implement CSV parser
2. Use RFL schema for column mapping
3. Support header row for field names
4. Add CSV tests

### Phase 4: Migrate Tests
1. Rewrite `rfl_fact.cpp` tests to use JSON format
2. Rewrite benchmarks to use binary format (pre-serialized data)
3. Verify all tests pass

### Phase 5: Remove FactBuilder
1. Delete `fact_builder.hpp`
2. Delete `typed_fact_builders.hpp`
3. Update documentation

## Format Comparison

| Format | Performance | Use Case | JIT Compiled |
|--------|-------------|----------|--------------|
| Binary | Fastest | Network protocols, IPC | Yes (MIR) |
| JSON | Medium | REST APIs, config files | No |
| CSV | Medium | Bulk data import | No |

## Example Usage

### Binary (existing)
```cpp
DataBind* codec = data_bind_create("schema.rfl", &api);
uint8_t buf[] = {42, 0, 0, 0, 100, 0, 0, 0};
Value* fact = data_bind_parse(codec, "Order", buf, sizeof(buf));
```

### JSON (new)
```cpp
DataBind* codec = data_bind_create_ex("schema.rfl", DATA_BIND_FORMAT_JSON, &api);
const char* json = "{\"id\": 42, \"quantity\": 100}";
Value* fact = data_bind_parse_string(codec, "Order", json);
```

### CSV (new)
```cpp
DataBind* codec = data_bind_create_ex("schema.rfl", DATA_BIND_FORMAT_CSV, &api);
const char* csv = "42,100\n43,200\n";
Value* fact = data_bind_parse_string(codec, "Order", csv);
```

## Benefits

1. **Unified Interface**: Single API for all formats
2. **High Performance**: JIT compilation for binary, optimized parsers for others
3. **Type Safety**: RFL schema validates all formats
4. **Easy Testing**: JSON format makes tests readable
5. **Production Ready**: Binary format for performance-critical paths

## Next Steps

Start with Phase 2: Implement JSON codec
