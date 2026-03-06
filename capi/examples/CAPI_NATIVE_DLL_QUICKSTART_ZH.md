# RulesForge C API Native/DLL 快速上手（5 分钟）

这是从 RHS 调用 Native 扩展的最短路径。

## 1）构建示例目标

```bash
cmake --build <build_dir> --target native_table_plugin native_function_table_demo
```

## 2）运行示例

```bash
./native_function_table_demo
```

预期输出：

```text
loaded plugin table from: ruleforge_native_plugin.dll
[plugin_log] argc=2 arg0="S-9" arg1=35.500000
fired rules: 1
```

## 3）刚刚发生了什么

1. `native_table_plugin` 导出 `ruleforge_get_function_table(...)`。
2. demo 通过 `ruleforge_kb_load_native_function_table(...)` 加载插件。
3. 规则 RHS 用 `invoke` 调用插件函数。

## 4）最小规则模板

```rfl
rule "Plugin Invoke"
when
    $s : Sensor(temperature > 30)
then
    invoke pluginLog($s.id, $s.temperature)
end
```

## 5）不走 DLL 的直接注册方式

```c
ruleforge_kb_register_native_function(kb, "pluginLog", plugin_log, NULL);
```

## 6）返回值用于赋值

```rfl
update $s { score = pluginMetric($s.temperature, 2) }
```

## 7）进阶文档

- 完整 EN：`CAPI_NATIVE_DLL_EN.md`
- 完整 ZH：`CAPI_NATIVE_DLL_ZH.md`
