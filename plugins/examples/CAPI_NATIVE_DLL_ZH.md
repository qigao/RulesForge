# RulesForge C API：Native 函数与 DLL 函数表

这份文档说明的是 C API 里的 Native 函数扩展路径。

它适合让规则在 RHS 中通过 `invoke` 调用外部函数。

这不是 RulesForge 的标准 source/sink 插件 ABI。
标准可复用插件接口现在是 `include/rule_forge_plugin.h` 里的 vtable，真实插件实现见 `rulesforge/plugins`。

## 1. 快速理解

- 业务实现写在 C/C++ 回调里。
- 规则侧这样调用：

```rfl
then
    invoke pluginLog($s.id, $s.temperature)
end
```

- 引擎流程：
  - 解析 RHS `invoke`
  - 在 KnowledgeBase 注册表里按名字查函数
  - 计算参数
  - 调用 Native 回调

## 2. 方案 A：直接注册 Native 函数

### 2.1 回调签名

```c
typedef ruleforge_status_t (*ruleforge_native_function_t)(
    void *ctx,
    int argc,
    const char **argv,
    char **out_result
);
```

说明：

- `argv` 是参数的序列化字符串。
- `out_result` 可选；如果要返回，请用 `malloc`/`strdup` 分配。
- `out_result` 由 RulesForge 释放。

### 2.2 注册

```c
ruleforge_kb_register_native_function(kb, "pluginLog", plugin_log, NULL);
```

### 2.3 规则调用

```rfl
rule "Native Direct"
when
    $s: Sensor(temperature > 30)
then
    invoke pluginLog($s.id, $s.temperature)
end
```

## 3. 方案 B：加载 DLL/so 函数表
### 3.2 插件 ABI

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

插件必须导出：

```c
ruleforge_status_t ruleforge_get_function_table(
    ruleforge_plugin_function_table_t *out_table);
```

### 3.3 示例文件

- 插件：`native_table_plugin.cpp`
- 加载示例：`native_function_table_demo.cpp`

## 4. 返回值用于赋值

除了语句风格 `invoke`，也可以在赋值里直接调用 Native 函数：

```rfl
update $s { score = pluginMetric($s.temperature, 2) }
```

## 5. 构建与运行示例

```bash
cmake --build <build_dir> --target native_table_plugin native_function_table_demo
./native_function_table_demo
```

典型输出：

```text
loaded plugin table from: ruleforge_native_plugin.dll
[plugin_log] argc=2 arg0="S-9" arg1=35.500000
fired rules: 1
```

## 6. 实用建议

- 先注册/加载插件，再执行规则。
- 函数名要稳定，规则依赖函数名。
- 失败要返回非 0 状态码。
- 回调里避免长时间阻塞。
