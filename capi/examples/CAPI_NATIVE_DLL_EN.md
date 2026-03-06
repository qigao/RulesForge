# RulesForge C API: Native Functions and DLL Function Table

This guide explains two extension paths in C API:

1. Register native functions directly from host code.
2. Load a DLL/so plugin that exports a function table.

Both are executed from RHS with `invoke`.

## 1. Quick Model

- Extension implementation lives in C/C++ functions.
- Rule side calls extensions via:

```rfl
then
    invoke pluginLog($s.id, $s.temperature)
end
```

- Engine behavior:
  - Parse RHS `invoke`.
  - Resolve function by name from KnowledgeBase registry.
  - Evaluate args.
  - Call native callback.

## 2. Option A: Register Native Function Directly

### 2.1 Callback Signature

```c
typedef ruleforge_status_t (*ruleforge_native_function_t)(
    void *ctx,
    int argc,
    const char **argv,
    char **out_result
);
```

Notes:

- `argv` contains serialized argument values (string form).
- `out_result` is optional; allocate with `malloc`/`strdup` if used.
- RulesForge will free `out_result`.

### 2.2 Register

```c
ruleforge_kb_register_native_function(kb, "pluginLog", plugin_log, NULL);
```

### 2.3 Rule Call

```rfl
rule "Native Direct"
when
    $s: Sensor(temperature > 30)
then
    invoke pluginLog($s.id, $s.temperature)
end
```

## 3. Option B: Load DLL/so Function Table

### 3.1 API

```c
ruleforge_status_t ruleforge_kb_load_native_function_table(
    ruleforge_knowledge_base_t kb,
    const char *library_path,
    const char *symbol_name
);
```

- `library_path`: plugin library path.
- `symbol_name`: exported function symbol.
  - pass `NULL` to use default: `ruleforge_get_function_table`.

### 3.2 Plugin ABI

```c
#define RULEFORGE_PLUGIN_ABI_V1 1u

typedef struct {
  const char *name;
  ruleforge_native_function_t callback;
  void *user_data;
} ruleforge_plugin_function_entry_t;

typedef struct {
  uint32_t abi_version;
  uint32_t function_count;
  const ruleforge_plugin_function_entry_t *functions;
} ruleforge_plugin_function_table_t;

typedef ruleforge_status_t (*ruleforge_get_function_table_t)(
    ruleforge_plugin_function_table_t *out_table);
```

Required export in plugin:

```c
ruleforge_status_t ruleforge_get_function_table(
    ruleforge_plugin_function_table_t *out_table);
```

### 3.3 Example Files

- Plugin: `native_table_plugin.cpp`
- Loader demo: `native_function_table_demo.cpp`

## 4. Use Return Value in Assignment

Besides statement-style `invoke`, native call can be used as assignment value:

```rfl
update $s { score = pluginMetric($s.temperature, 2) }
```

## 5. Build and Run Example

```bash
cmake --build <build_dir> --target native_table_plugin native_function_table_demo
./native_function_table_demo
```

Typical output:

```text
loaded plugin table from: ruleforge_native_plugin.dll
[plugin_log] argc=2 arg0="S-9" arg1=35.500000
fired rules: 1
```

## 6. Practical Rules

- Register/load plugins before firing rules.
- Keep function names stable; rules depend on names.
- Return non-zero status on failure.
- Avoid long blocking operations in callbacks.
