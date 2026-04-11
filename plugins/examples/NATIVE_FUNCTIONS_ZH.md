# RulesForge 中的 Native 函数注册（C API）

本文档说明如何在 RulesForge 规则中注册并使用 C Native 函数。

相关完整指南：

- EN: `CAPI_NATIVE_DLL_EN.md`
- ZH: `CAPI_NATIVE_DLL_ZH.md`
- 本页英文版：`NATIVE_FUNCTIONS.md`

## 概述

RulesForge 支持注册自定义 C 函数，并在规则 RHS 表达式中调用。常见用途：

- **自定义计算**：用 C 实现领域内数学或业务逻辑
- **外部系统集成**：调用任意 C/C++ 库或系统 API
- **性能敏感逻辑**：将重计算部分放到 C 实现

## API 参考

### 函数签名

```c
typedef ruleforge_status_t (*ruleforge_native_function_t)(
    void *ctx,           // 用户上下文
    int argc,            // 参数个数
    const char **argv,   // JSON 编码参数字符串数组
    char **out_result    // 输出结果（JSON 字符串，由调用方 free）
);
```

### 注册接口

```c
ruleforge_status_t ruleforge_kb_register_native_function(
    ruleforge_knowledge_base_t kb,
    const char *function_name,
    ruleforge_native_function_t callback,
    void *user_data
);
```

### DLL 函数表注册

你也可以把多个 Native 函数打包在插件 DLL/.so 中，一次性加载：

```c
ruleforge_status_t ruleforge_kb_load_native_function_table(
    ruleforge_knowledge_base_t kb,
    const char *library_path,
    const char *symbol_name /* 传 NULL 使用默认符号 */
);
```

插件导出约定：

```c
ruleforge_status_t ruleforge_get_function_table(ruleforge_plugin_function_table_t *out_table);
```

**参数说明：**
- `kb`：知识库句柄
- `function_name`：规则中使用的函数名
- `callback`：C 函数指针
- `user_data`：传给回调的可选上下文（可为 NULL）

**返回值：** 成功返回 `RULES_FORGE_OK`

## 使用示例

### 1. 定义 Native 函数

```c
ruleforge_status_t calculate_tax(void *ctx, int argc, const char **argv, char **out_result) {
    if (argc < 1) {
        *out_result = strdup("{\"error\": \"requires amount\"}");
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    double amount = atof(argv[0]);
    double tax = amount * 0.08;

    char buffer[256];
    fmt(buffer, sizeof(buffer), "{:.2f}", tax);
    *out_result = strdup(buffer);
    return RULES_FORGE_OK;
}
```

### 2. 注册函数

```c
ruleforge_knowledge_base_t kb;
ruleforge_kb_create(&kb);

ruleforge_kb_register_native_function(kb, "calculateTax", calculate_tax, nullptr);
```

### 3. 在规则中使用

Native 函数可以作为 RHS 中的赋值表达式值：

```rfl
rule "Apply Tax"
when
    $order : Order(status == "pending")
then
    update $order {
        tax = calculateTax($order.subtotal),
        total = $order.subtotal + calculateTax($order.subtotal)
    }
end
```

## 进阶示例

### 带上下文的函数

```c
struct DatabaseContext {
    void *db_connection;
    const char *table_name;
};

ruleforge_status_t lookup_rate(void *ctx, int argc, const char **argv, char **out_result) {
    DatabaseContext *db_ctx = (DatabaseContext *)ctx;

    if (argc < 1) {
        *out_result = strdup("0.0");
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    // 通过上下文访问数据库
    double rate = db_lookup_rate(db_ctx->db_connection, argv[0]);

    char buffer[64];
    fmt(buffer, sizeof(buffer), "{:.4f}", rate);
    *out_result = strdup(buffer);
    return RULES_FORGE_OK;
}

// 带上下文注册
DatabaseContext db_ctx = {my_db_connection, "rates"};
ruleforge_kb_register_native_function(kb, "lookupRate", lookup_rate, &db_ctx);
```

**规则中使用：**

```rfl
rule "Apply Dynamic Rate"
when
    $loan : Loan(status == "active")
then
    update $loan {
        rate = lookupRate($loan.category),
        payment = $loan.principal * lookupRate($loan.category) / 12
    }
end
```

### 多参数计算

```c
ruleforge_status_t weighted_score(void *ctx, int argc, const char **argv, char **out_result) {
    if (argc < 3) {
        *out_result = strdup("0.0");
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    double score1 = atof(argv[0]);
    double score2 = atof(argv[1]);
    double weight = atof(argv[2]);

    double result = score1 * weight + score2 * (1.0 - weight);

    char buffer[64];
    fmt(buffer, sizeof(buffer), "{:.2f}", result);
    *out_result = strdup(buffer);
    return RULES_FORGE_OK;
}
```

**规则中使用：**

```rfl
rule "Calculate Final Score"
when
    $student : Student()
    not FinalScore(studentId == $student.id)
then
    insert FinalScore {
        studentId = $student.id,
        score = weightedScore($student.exam, $student.homework, 0.7)
    }
end
```

## 重要说明

### 内存管理

1. **输入参数（`argv`）**：只读，由 RulesForge 管理，不要释放
2. **输出结果（`out_result`）**：
   - 使用 `malloc()` 或 `strdup()` 分配
   - RulesForge 会调用 `free()` 释放
   - 不需要返回值时可设为 `nullptr`

### 参数格式

- 参数以 JSON 编码字符串传入
- 复杂结构建议用 JSON 解析库处理
- 简单值可用 `atoi()`、`atof()` 等解析

### 返回值

- 成功返回 `RULES_FORGE_OK`（0）
- 失败返回错误码
- 失败时可将错误信息写入 `*out_result`

### 线程安全

- Native 函数可能在多线程环境调用
- 如果有共享状态，你的实现必须自行保证线程安全
- 可通过 `ctx` 传递线程隔离数据

## 完整示例

参考 `native_functions_demo.cpp`。

## 构建示例

```bash
cmake --build build --target native_functions_demo
./build/bin/native_functions_demo
```

## 错误处理

始终检查返回值并处理错误：

```c
if (ruleforge_kb_register_native_function(kb, "myFunc", my_func, ctx) != RULES_FORGE_OK) {
    fprintf(stderr, "Failed to register function: %s\n",
            ruleforge_get_last_error_message());
    return 1;
}
```

## 最佳实践

1. **保持函数简单**：Native 函数应聚焦单一职责
2. **严格校验输入**：检查 `argc` 和参数有效性
3. **错误信息清晰**：便于定位问题
4. **覆盖测试**：验证不同输入组合
5. **关注性能**：函数会在规则执行路径中被频繁调用

## 限制

- 函数名必须是合法标识符
- 参数以字符串（JSON 编码）传递
- 函数注册粒度是 KnowledgeBase（不是 Session）
- 建议在加载规则前完成注册
- Native 函数只能用于 RHS 表达式上下文（例如 `insert`/`update` 字段值），不能作为独立语句调用

