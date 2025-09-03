# JMESPath Integration with Drills Rules Engine

## 概述

Drills rules engine 集成了 **基础的** JMESPath 功能，支持在 JavaScript 代码中进行简单的 JSON 查询操作。

**重要提示**：当前实现是**最小化版本**，只支持最基础的功能。

## 当前支持的功能

### ✅ 已实现的 JMESPath 功能

1. **基本属性访问**: `user.name`, `user.profile.age`
2. **数组长度查询**: `items | length(@)`, `orders | length(@)`
3. **基础错误处理**: 无效路径返回 `null`

### ❌ **未实现** 的功能

以下功能在当前版本中**不支持**：
- ❌ 数组投影 (`products[*].price`) 
- ❌ 数组过滤 (`products[?price > 100]`)
- ❌ 数组索引 (`items[0]`)
- ❌ 除 `length(@)` 外的其他内置函数
- ❌ 复杂的管道操作
- ❌ 条件表达式和比较操作符

## 基本用法

### JavaScript 中的 jmespath 函数

```javascript
// 基本语法
var result = jmespath(json_string, query_expression);
```

## 支持的查询示例

### 1. 基本属性访问

```javascript
// JSON 数据
var jsonData = '{"user": {"profile": {"age": 25, "name": "Alice"}}}';

// 提取嵌套属性
var age = jmespath(jsonData, 'user.profile.age');   // 结果: 25
var name = jmespath(jsonData, 'user.profile.name'); // 结果: "Alice"
```

### 2. 数组长度查询

```javascript  
// JSON 数据
var jsonData = '{"items": [1, 2, 3, 4, 5], "orders": [{"id": 1}, {"id": 2}]}';

// 获取数组长度
var itemCount = jmespath(jsonData, 'items | length(@)');   // 结果: 5
var orderCount = jmespath(jsonData, 'orders | length(@)'); // 结果: 2
```

## 实际应用示例

### 规则中的使用

```drl
declare TestFact
    name: String
end

rule "Basic JMESPath Usage"
when
    $fact : TestFact(name == "test")
then
    // 基础属性访问
    var jsonData = '{"user": {"profile": {"age": 25, "name": "Alice"}}}';
    var age = jmespath(jsonData, 'user.profile.age');
    var name = jmespath(jsonData, 'user.profile.name');
    
    console.log("Age:", age);
    console.log("Name:", name);
    
    // 数组长度查询
    var arrayData = '{"items": [1, 2, 3, 4, 5]}';
    var count = jmespath(arrayData, 'items | length(@)');
    console.log("Item count:", count);
end
```

## 支持的语法参考

### 当前支持的查询语法

| 语法 | 描述 | 示例 | 状态 |
|------|------|------|------|
| `obj.field` | 属性访问 | `user.name` | ✅ 支持 |
| `obj \| length(@)` | 数组长度 | `items \| length(@)` | ✅ 支持 |

### 不支持的语法

| 语法 | 描述 | 示例 | 状态 |
|------|------|------|------|
| `obj[*]` | 数组投影 | `products[*].price` | ❌ 不支持 |
| `obj[?condition]` | 数组过滤 | `orders[?total > 100]` | ❌ 不支持 |
| `obj[index]` | 数组索引 | `items[0]` | ❌ 不支持 |
| 其他内置函数 | sum, max, min 等 | `prices \| sum(@)` | ❌ 不支持 |

## 错误处理

### 查询失败情况

```javascript
// 无效路径返回 null
var result = jmespath(data, 'nonexistent.field');
console.log(result); // 输出: null

// JSON 解析错误返回 null  
var result = jmespath('invalid json', 'any.query');
console.log(result); // 输出: null
```

### 推荐的错误检查

```javascript
var result = jmespath(jsonData, 'user.profile.age');
if (result !== null && result !== undefined) {
    console.log("Age found:", result);
} else {
    console.log("Age not found or query failed");
}
```

## 性能注意事项

1. **JSON 解析开销**：每次调用 `jmespath()` 都会解析 JSON，避免重复查询相同数据
2. **内存使用**：大型 JSON 数据会占用相应内存，处理完后及时释放引用
3. **查询复杂度**：深层嵌套查询性能随层级增加而降低

## 限制和约束

1. **功能限制**：仅支持基础属性访问和数组长度查询
2. **类型支持**：支持 JSON 基础类型（string, number, boolean, null, object, array）
3. **错误恢复**：查询失败返回 `null`，不会抛出异常
4. **安全模式**：禁用了可能不稳定的复杂查询功能

## 版本历史

### v1.0 - 基础实现
- ✅ 基本属性访问
- ✅ 数组长度查询  
- ✅ 基础错误处理
- ✅ 内存安全修复

## 未来扩展计划

如果需要完整的 JMESPath 功能，可以考虑：
- 集成完整的 JMESPath 库（如 jmespath.cpp）
- 实现数组投影和过滤功能
- 添加更多内置函数

当前版本专注于**稳定性和安全性**，而不是功能完整性。