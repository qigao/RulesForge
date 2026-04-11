# RulesForge 插件说明

此页为中文入口；详细英文说明在 [`plugins/README.md`](/C:/projects/cpp/rulesforge/plugins/README.md)。

## 先分清两种扩展

RulesForge 里有两条不同扩展路径：

- source/sink 插件：走公开 vtable ABI
- native RHS 函数：走 `ruleforge_kb_register_native_function()` 或 DLL 函数表

这两者不是一回事。

## 当前仓库真正有的插件

默认构建启用：

- `file_source_plugin`
- `http_sink_plugin`

仓内有代码，但默认未启用：

- `freeswitch_source_plugin`

仓内并不存在可直接构建的这些模块：

- MQTT source
- Kafka sink
- PostgreSQL source

若旧文档写它们“已内置”，那就是错的。

## 公开 ABI 在哪里

插件 ABI 定义于：

- [`include/rule_forge_plugin.h`](/C:/projects/cpp/rulesforge/include/rule_forge_plugin.h)

主要结构：

- `ruleforge_datasource_vtable_t`
- `ruleforge_datasink_vtable_t`
- `ruleforge_route_envelope_t`

## Native 函数示例在哪里

- [`plugins/examples/README.md`](/C:/projects/cpp/rulesforge/plugins/examples/README.md)
- [`plugins/examples/NATIVE_FUNCTIONS_ZH.md`](/C:/projects/cpp/rulesforge/plugins/examples/NATIVE_FUNCTIONS_ZH.md)
- [`plugins/examples/CAPI_NATIVE_DLL_ZH.md`](/C:/projects/cpp/rulesforge/plugins/examples/CAPI_NATIVE_DLL_ZH.md)

## 配置约定

source/sink 插件通过 `init(const char* config_json, void** out_ctx)` 接收 JSON 配置。

共用辅助头：

- [`plugins/plugin_config.h`](/C:/projects/cpp/rulesforge/plugins/plugin_config.h)

约定：

- `NULL` 或空字符串表示使用默认配置
- 非法 JSON 应返回 `RULES_FORGE_ERROR_INVALID_ARGUMENT`

## 继续阅读

- 英文插件总览：[`../../plugins/README.md`](/C:/projects/cpp/rulesforge/plugins/README.md)
- 中文使用指南：[`USER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/zh-CN/USER_GUIDE.md)
