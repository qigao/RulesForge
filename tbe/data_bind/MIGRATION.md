# Data Bind Migration to RulesForge DSL

## Summary

Successfully migrated `data_bind` from TBE schema format to RulesForge DSL (.rfl files).

## Changes Made

### 1. Created Independent Parser Library

**New: `rulesforge/src/parser/` (rfl_parser static library)**
- Contains core parsing functionality (lexer + grammar + implementation)
- Shared by both data_bind and RulesForge engine
- Includes necessary type implementations:
  - `rfl_rete_defs.cpp` - Type definitions and copy constructors
  - `expression_evaluator.cpp` - Expression evaluation support

**Files:**
```
rulesforge/src/parser/
├── CMakeLists.txt          (NEW)
├── rfl_lexer.re
├── rfl_lexer.hpp
├── rfl_grammar_lemon.y
├── rfl_parser_impl.cpp
├── rfl_parser_impl.hpp
├── rfl_token.hpp
├── expression_evaluator.cpp
└── ../core/rfl_rete_defs.cpp
```

### 2. Refactored data_bind

**Changed:**
- `data_bind.c` → `data_bind.cpp` (C to C++ for RulesForge types)
- `tbe_helpers.c` → `tbe_helpers.cpp` (added extern "C" linkage)
- API now accepts `.rfl` files instead of `.tbe` files

**Removed:**
- `tbe_to_rfl_types.cpp/h` (~200 lines)
- `rfl_declarations_parser.cpp/h` (unused stub)
- `test_debug.c`, `minimal_test.rfl`, `test_schema.rfl` (temporary files)

**Key Code Changes:**
```cpp
// Before: Used TBE schema parser
TbeSchemaTypes* schema = load_and_parse_schema(tbe_path);

// After: Direct RFL parsing
#include "rfl_parser_impl.hpp"
parser_state state = rfl_parse_lemon(rfl_content, rfl_path, errors);
codec->declarations = std::move(state.parsed_declarations);
```

### 3. Updated Tests

**Test Structure (tinytest BDD):**
```c
given("description") {
    write_schema("file.rfl", "declare Type\n    field: int\nend\n");

    when("action") {
        DataBind* codec = data_bind_create("file.rfl", &test_api);

        then("codec should be created") {
            check_not_null(codec);
        }

        then("should test functionality") {
            if (codec) {
                // Test code here
            }
        }

        if (codec) data_bind_free(codec);
    }

    remove("file.rfl");
}
```

**Test Files:**
- `test_data_bind.c` - Full test suite (9 sections, all passing)
- `test_data_bind_simple.c` - Minimal reference test
- `test_minimal.c` - tinytest framework verification

### 4. RulesForge DSL Syntax

**Declaration Format:**
```rfl
declare TypeName
    field1: int
    field2: long
    field3: double
    field4: string
end
```

**Type Mapping:**
- `int` → 4 bytes (int32_t)
- `long` → 8 bytes (int64_t)
- `float` → 4 bytes
- `double` → 8 bytes
- `boolean` → 1 byte

**Note:** Enums are not needed for binary parsing - enum fields are just `int` fields.

## Benefits

1. **Unified Type System**: data_bind and RulesForge now share the same type definitions
2. **Eliminated Duplication**: No more double parsing or type conversion
3. **Cleaner Code**: Removed ~2000+ lines of obsolete TBE parser code
4. **Better Maintainability**: Single source of truth for DSL parsing
5. **Improved Error Reporting**: Direct access to RulesForge parser errors

## Build Dependencies

```cmake
target_link_libraries(
    data_bind
    PRIVATE mir_static rfl_parser
    PUBLIC TurboNet::Utils
)
```

## Migration Complete ✅

- All tests passing
- No breaking changes to data_bind API (except file extension .tbe → .rfl)
- Ready for production use

## Next Steps (Optional)

1. Update documentation to reflect .rfl usage
2. Consider migrating `tbe/tbe/` library if needed
3. Add more comprehensive .rfl examples
