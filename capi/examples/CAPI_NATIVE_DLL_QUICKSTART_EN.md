# RulesForge C API Native/DLL Quickstart (5 Minutes)

This is the shortest path to run native extension from RHS.

## 1) Build demo targets

```bash
cmake --build <build_dir> --target native_table_plugin native_function_table_demo
```

## 2) Run demo

```bash
./native_function_table_demo
```

Expected output:

```text
loaded plugin table from: ruleforge_native_plugin.dll
[plugin_log] argc=2 arg0="S-9" arg1=35.500000
fired rules: 1
```

## 3) What just happened

1. `native_table_plugin` exports `ruleforge_get_function_table(...)`.
2. Demo loads plugin by `ruleforge_kb_load_native_function_table(...)`.
3. Rule RHS calls plugin function with `invoke`.

## 4) Minimal rule pattern

```rfl
rule "Plugin Invoke"
when
    $s : Sensor(temperature > 30)
then
    invoke pluginLog($s.id, $s.temperature)
end
```

## 5) Direct registration path (no DLL)

```c
ruleforge_kb_register_native_function(kb, "pluginLog", plugin_log, NULL);
```

## 6) Return value in assignment

```rfl
update $s { score = pluginMetric($s.temperature, 2) }
```

## 7) Next reference

- Full EN guide: `CAPI_NATIVE_DLL_EN.md`
- Full ZH guide: `CAPI_NATIVE_DLL_ZH.md`
