# Factory Pattern Refactoring for data_bind

## Architecture

### Class Hierarchy

```
ICodec (Abstract Interface)
├── BinaryCodec (MIR JIT)
├── JsonCodec (TurboNet::Parser)
└── CsvCodec (TODO)

CodecFactory (Factory)
└── create(format, declarations, api) -> ICodec*
```

### Files Created

1. **codec_interface.h** - Abstract codec interface
2. **codec_factory.cpp** - Factory implementation
3. **formats/binary/binary_codec.h** - Binary codec class (header only)
4. **formats/json/json_codec.h** - JSON codec class (updated)
5. **formats/json/json_codec.cpp** - JSON codec implementation (updated)
6. **data_bind_new.cpp** - New unified API using factory

### Current Status

✅ **Completed:**
- Abstract ICodec interface
- CodecFactory implementation
- JsonCodec class (fully implemented and tested)
- New data_bind.cpp using factory pattern

⏳ **In Progress:**
- BinaryCodec class (need to refactor data_bind_binary_impl.cpp)

### Next Steps

**Option 1: Complete Binary Refactoring**
1. Convert data_bind_binary_impl.cpp to BinaryCodec class
2. Update CMakeLists.txt to use new files
3. Test both binary and JSON formats
4. Remove old files

**Option 2: Hybrid Approach**
1. Keep current binary implementation as-is
2. Add a thin wrapper in BinaryCodec that calls old functions
3. Gradually refactor later

**Option 3: Pause and Document**
1. Document current progress
2. Create migration plan
3. Continue later

## Benefits of Factory Pattern

1. **Open/Closed Principle**: Easy to add new formats without modifying existing code
2. **Single Responsibility**: Each codec handles one format
3. **Dependency Inversion**: High-level code depends on ICodec interface
4. **Testability**: Easy to mock codecs for testing
5. **Maintainability**: Clear separation of concerns

## Usage Example

```cpp
// Binary format (backward compatible)
DataBind* codec = data_bind_create("schema.rfl", &api);
Value* v = data_bind_parse(codec, "Order", binary_data, len);

// JSON format (new)
DataBind* codec = data_bind_create_ex("schema.rfl", DATA_BIND_FORMAT_JSON, &api);
Value* v = data_bind_parse_string(codec, "Order", "{\"id\": 42}");

// Both use same API!
data_bind_free(codec);
```

## Migration Path

### Phase 1: Add JSON Support ✅
- Implement JsonCodec
- Test JSON parsing
- **Status: DONE**

### Phase 2: Refactor Binary (Current)
- Convert to BinaryCodec class
- Maintain backward compatibility
- **Status: IN PROGRESS**

### Phase 3: Unified API
- Replace old data_bind.cpp with data_bind_new.cpp
- Update all tests
- **Status: PENDING**

### Phase 4: Remove FactBuilder
- Migrate all tests to use data_bind
- Delete fact_builder.hpp
- **Status: PENDING**
