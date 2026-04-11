# File Source Plugin

## Overview

File DataSource plugin using `turbo_fs.h` and `turbo_parser.h` for incremental CSV delivery.

## Features

- **Default path from config** when `query` is NULL
- **CSV header skipping** (configurable)
- **Configurable buffer size** (default 8KB)
- **CSV row filtering** via `turbo_csv_stream_processor`
- **Support for CSV and JSON formats**
- **Path reload on query change**

The runtime model is now split by format:

- JSON paths still use `turbo_fs_read_file()` and emit the whole document once
- CSV paths use `turbo_fs_open()` / `turbo_fs_read()` and emit one logical message per `fetch()`
- when a header or filter is present, `turbo_csv_stream_processor` validates header/filter state incrementally
- when a header or filter is present, each emitted CSV message is self-contained: `header + row`
- when no header is expected (`skip_header=false` with no filter), rows are emitted as raw CSV lines

This keeps the existing router-facing `fetch()` contract while fixing the old "load everything once and never reload a new query path" behavior.

## Configuration

```json
{
  "path": "data/sensors.csv",
  "format": "csv",
  "skip_header": true,
  "buffer_size": 8192,
  "filter": "value_n >= 100"
}
```

## Usage Example

```c
// Register file source plugin
extern const ruleforge_datasource_vtable_t* file_source_get_vtable();

route_engine_register_source(
    engine,
    "file_source",
    file_source_get_vtable(),
    "{\"path\":\"data/sensors.csv\",\"format\":\"csv\",\"skip_header\":true,\"filter\":\"value_n >= 100\"}"
);

// Start routing from file
route_engine_run(
    engine,
    "file_source",
    "data/sensors.csv",  // Can override path in query
    "SensorReading",
    -1
);
```

## Implementation Details

### Context Structure

```c
typedef struct {
    char* file_data;
    size_t file_size;
    DataBindFormat format;
    int skip_header;
    char* line_buffer;
    size_t line_capacity;
    char* read_buffer;
    size_t read_capacity;
    size_t buffered_length;
    char default_path[512];
    char active_path[512];
    char filter_expr[512];
    turbo_csv_stream_processor_t* csv_stream;
    turbo_file_t file_handle;
    int header_consumed;
    int end_of_file;
    int json_emitted;
} file_source_ctx_t;
```

### Filter Notes

`turbo_csv_stream_processor` follows TurboNet's typed-header convention:

- numeric columns: `xxx_n`
- string columns: `xxx_s`

Example:

```json
{
  "path": "orders.csv",
  "filter": "price_n >= 100 and symbol_s == \"AAPL\""
}
```

Notes:

- filtering assumes the first emitted CSV line is the header
- `skip_header=true` means the source consumes the file header internally and re-emits it together with each matching row
- if `skip_header` is `false` and no filter is configured, the source treats the file as raw row data
- changing `query` to a different file path resets internal state and starts reading the new file from the beginning
- end of file returns `RULES_FORGE_STATUS_END_OF_STREAM`

## Error Handling

```c
// File exhausted
return RULES_FORGE_STATUS_END_OF_STREAM;
```

## Memory Management

- **Line buffer**: holds the current returned row
- **Read buffer**: holds unread streamed file bytes between fetch calls
- **JSON content**: loaded once per active path and freed on path switch or cleanup
- **CSV stream processor**: created lazily for header/filter mode and freed on path switch or cleanup
- **Returned data**: borrowed pointer into plugin-owned memory

## Building

```bash
cd plugins/file_source
mkdir build
cd build
cmake ..
make
```

## Dependencies

- TurboNet::Utils (turbo_fs.h)
- RulesForge C API
- DataBind

## Testing

```c
// test_file_source.c

#include "file_source_plugin.c"

void test_file_source() {
    void* ctx;
    ruleforge_status_t status = file_source_init("{}", &ctx);
    assert(status == RULES_FORGE_OK);

    DataBindFormat format;
    const uint8_t* data;
    size_t len;

    status = file_source_fetch(ctx, "test.csv", "Test", &format, &data, &len);
    assert(status == RULES_FORGE_OK);
    assert(format == DATA_BIND_FORMAT_CSV);
    assert(len > 0);

    file_source_cleanup(ctx);
}
```

## Future Enhancements

1. Support true push-style source APIs so CSV does not need pull-driven `fetch()` loops.
2. Support emitting filtered batches instead of one row per fetch.
3. Add JSON path extraction / projection.
4. Support compressed files.

## License

Same as RulesForge project.
