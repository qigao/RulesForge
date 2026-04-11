# RulesForge Plugins

This directory documents the current plugin surfaces that actually exist in this repository.

There are two different extension models:

- source/sink plugins using the public vtable ABI in [`include/rule_forge_plugin.h`](/C:/projects/cpp/rulesforge/include/rule_forge_plugin.h)
- native RHS function examples under [`plugins/examples`](/C:/projects/cpp/rulesforge/plugins/examples), which are not the same thing

Do not mix them up.

## 1. Public Plugin ABI

The stable public contract is:

- `ruleforge_datasource_vtable_t`
- `ruleforge_datasink_vtable_t`
- `ruleforge_route_envelope_t`

All are declared in [`include/rule_forge_plugin.h`](/C:/projects/cpp/rulesforge/include/rule_forge_plugin.h).

## 2. What This Repo Actually Builds

Enabled by [`plugins/CMakeLists.txt`](/C:/projects/cpp/rulesforge/plugins/CMakeLists.txt):

- `file_source_plugin`
- `http_sink_plugin`
- extension demos under `plugins/examples`

Present in tree but currently not enabled by default:

- `freeswitch_source_plugin`

Not present in this repository as buildable modules:

- MQTT source
- Kafka sink
- PostgreSQL source

If a doc claims those are bundled here, that doc is wrong.

## 3. Existing Plugins

### File Source Plugin

Path: [`plugins/file_source`](/C:/projects/cpp/rulesforge/plugins/file_source)

Purpose:

- load data from files for source-side integration

Key docs:

- [`plugins/file_source/README.md`](/C:/projects/cpp/rulesforge/plugins/file_source/README.md)

### HTTP Sink Plugin

Path: [`plugins/http_sink`](/C:/projects/cpp/rulesforge/plugins/http_sink)

Purpose:

- push routing decisions or envelopes to HTTP endpoints

Build dependency:

- `TurboNet::HttpClient`

### FreeSWITCH Source Plugin

Path: [`plugins/freeswitch_source`](/C:/projects/cpp/rulesforge/plugins/freeswitch_source)

Status:

- code and tests exist
- not enabled in the top-level plugin build by default

Use it only if your build explicitly adds it.

## 4. Native Function Examples

The following docs and demos cover RHS native functions and DLL function-table loading:

- [`plugins/examples/README.md`](/C:/projects/cpp/rulesforge/plugins/examples/README.md)
- [`plugins/examples/NATIVE_FUNCTIONS.md`](/C:/projects/cpp/rulesforge/plugins/examples/NATIVE_FUNCTIONS.md)
- [`plugins/examples/CAPI_NATIVE_DLL_EN.md`](/C:/projects/cpp/rulesforge/plugins/examples/CAPI_NATIVE_DLL_EN.md)

These examples use:

- `ruleforge_kb_register_native_function()`
- `ruleforge_kb_load_native_function_table()`

Those are C API extension hooks for rules, not router source/sink plugins.

## 5. Configuration Rules

Source/sink plugins use JSON config through their `init(const char* config_json, void** out_ctx)` hook.

Shared helper:

- [`plugins/plugin_config.h`](/C:/projects/cpp/rulesforge/plugins/plugin_config.h)

Expected behavior:

- `NULL` or empty config means default configuration
- malformed JSON returns `RULES_FORGE_ERROR_INVALID_ARGUMENT`

## 6. Build Notes

Plugins are built as shared libraries from the main repository build. Do not invent a second standalone build story unless you actually maintain it.

Typical flow:

```bash
cmake -S . -B build \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DTURBONET_ROOT=/path/to/TurboNet \
  -DTURBOSCRIPT_ROOT=/path/to/TurboScript \
  -DTURBO_UTILS=/path/to/TurboNet

cmake --build build
ctest --test-dir build --output-on-failure
```

## 7. Practical Advice

- use the vtable ABI for reusable ingestion/routing components
- use native-function registration for rule-local callbacks
- keep plugin docs tied to what the repository actually ships
- do not document imaginary plugins as product features
