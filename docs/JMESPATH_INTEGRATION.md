# JMESPath Integration with Drills Rules Engine

## Overview

The Drills Rules Engine now integrates **full** JMESPath functionality, supporting powerful JSON data querying and transformation operations within JavaScript code. We have replaced the underlying implementation with the robust `jsoncons` library, ensuring complete functionality and stability.

## How JMESPath Makes Your Life Easier

When dealing with complex JSON data, manually writing JavaScript code to extract, filter, or transform data can become exceptionally cumbersome, error-prone, and difficult to maintain. JMESPath was created to solve these pain points.

Imagine you need to extract specific information from a deeply nested JSON object, or filter elements from an array that meet certain conditions, or even perform aggregation calculations on a field. Without JMESPath, you might need:

-   **Verbose dot notation chains**: `data.users[0].profile.address.city`
-   **Complex loops and conditional statements**: Iterating through arrays, using `if` statements for filtering.
-   **Manual aggregation**: Writing loops to calculate `sum`, `max`, `min`, etc.

JMESPath allows you to accomplish these tasks with concise, declarative expressions, significantly reducing code volume, improving readability, and lowering the probability of errors. It's like "SQL" for JSON data, enabling you to interact with data in a more intuitive way.

## Currently Supported Features

### ✅ Implemented JMESPath Features (based on jsoncons)

Drills now supports most features of the JMESPath specification, including:

1.  **Basic property access**: `user.name`, `user.profile.age`
2.  **Array projection**: `products[*].price`
3.  **Array filtering**: `orders[?total > 100]`, `customers[?membership.tier == 'premium']`
4.  **Array slicing**: `items[1:3]`
5.  **Object values**: `user.profile.*`
6.  **Multi-select lists**: `[name, age]`
7.  **Multi-select hashes**: `{name: name, age: age}`
8.  **Pipe expressions**: `items | length(@)`
9.  **Built-in functions**: `length(@)`, `sum(@)`, `max(@)`, `min(@)`, `avg(@)`, `keys(@)`, `values(@)`, `join(' ', @)` etc.
10. **Conditional expressions**: `foo || bar`, `foo && bar`
11. **Comparison operators**: `==`, `!=`, `<`, `<=`, `>`, `>=`
12. **Literals**: Supports string, number, boolean, and null literals.
13. **Basic error handling**: Invalid queries or JSON format errors will throw JavaScript exceptions.

## Basic Usage

### `jmespath` function in JavaScript

```javascript
// Basic syntax
var result = jmespath(json_string, query_expression);
```

`json_string` must be a valid JSON string. `query_expression` must be a valid JMESPath expression.

## Supported Query Examples

### 1. Basic Property Access and Nesting

```javascript
// JSON Data
var jsonData = '{"user": {"profile": {"age": 25, "name": "Alice", "address": {"city": "New York"}}}}';

// Extract nested properties
var age = jmespath(jsonData, 'user.profile.age');   // Result: 25
var name = jmespath(jsonData, 'user.profile.name'); // Result: "Alice"
var city = jmespath(jsonData, 'user.profile.address.city'); // Result: "New York"
```

### 2. Array Projection and Filtering

```javascript
// JSON Data
var productsData = '[{"name": "Laptop", "price": 1200}, {"name": "Mouse", "price": 25}, {"name": "Keyboard", "price": 75}]';

// Array projection: Extract prices of all products
var prices = jmespath(productsData, '[*].price'); // Result: [1200, 25, 75]

// Array filtering: Find products with price greater than 100
var expensiveProducts = jmespath(productsData, '[?price > `100`]'); 
// Result: [{"name": "Laptop", "price": 1200}]
```

### 3. Built-in Functions and Pipe Operations

```javascript
// JSON Data
var ordersData = '{"orders": [{"amount": 100}, {"amount": 200}, {"amount": 50}]}';

// Pipe operation and length function
var orderCount = jmespath(ordersData, 'orders | length(@)'); // Result: 3

// Pipe operation and sum function
var totalAmount = jmespath(ordersData, 'orders[*].amount | sum(@)'); // Result: 350
```

### 4. Complex Query Examples

For more complex JMESPath query examples, please refer to the [Official JMESPath Documentation](https://jmespath.org/examples.html) or `docs/examples/JMESPATH_EXAMPLES.md` in the project.

## Practical Application Examples

### Usage in Rules

```drl
declare TestFact
    name: String
    payload: String
end

rule "Comprehensive JMESPath Usage"
when
    $fact : TestFact(name == "jmespath_demo")
then
    // Assume payload contains complex JSON data
    var complexJson = fact.payload; 

    // Example 1: Extract high-value orders (filtering)
    // Scenario: Find all orders with a total amount greater than 1000
    var highValueOrders = jmespath(complexJson, 'orders[?total > `1000`]');
    console.log("High-value orders:", JSON.stringify(highValueOrders));

    // Example 2: Calculate total revenue (aggregation)
    // Scenario: Calculate the total amount of all orders
    var totalRevenue = jmespath(complexJson, 'orders[*].amount | sum(@)');
    console.log("Total revenue:", totalRevenue);

    // Example 3: Find a specific customer's email (filtering + projection + index)
    // Scenario: Find the email address of the customer with ID "CUST-001"
    var customerEmail = jmespath(complexJson, 'customers[?id == `CUST-001`].email | [0]');
    console.log("Customer email:", customerEmail);

    // Example 4: Extract all product categories (projection + deduplication)
    // Scenario: Get a list of unique categories for all products
    var productCategories = jmespath(complexJson, 'products[*].category | unique(@)');
    console.log("Product categories:", JSON.stringify(productCategories));

    // Example 5: Find low-stock product names (filtering + projection)
    // Scenario: Find the names of all products with inventory less than 10
    var lowStockItems = jmespath(complexJson, 'products[?inventory < `10`].name');
    console.log("Low-stock items:", JSON.stringify(lowStockItems));
end
```

## Error Handling

### Query Failure Cases

Now, when a JMESPath query encounters invalid JSON input or syntax errors, it will throw a JavaScript exception. You can use `try-catch` blocks to catch these errors.

```javascript
try {
    // Invalid JSON input
    var result1 = jmespath("invalid json", 'user.name');
    console.log("Result 1:", result1);
} catch (e) {
    console.error("Caught JSON parsing error:", e.message);
}

try {
    // Invalid JMESPath expression
    var result2 = jmespath('{"valid": "json"}', 'invalid[[[syntax');
    console.log("Result 2:", result2);
} catch (e) {
    console.error("Caught JMESPath syntax error:", e.message);
}
```

## Performance Considerations

1.  **JSON Parsing Overhead**: Each call to `jmespath()` parses the JSON string. For frequent queries of the same JSON data, it is recommended to parse the JSON once in JavaScript (`JSON.parse()`) and then pass the JavaScript object to the `jmespath` function (if the `jmespath` function supports directly receiving JS objects, otherwise it still needs to be converted to a string).
2.  **Memory Usage**: Large JSON data will consume corresponding memory. After processing, ensure that references to JavaScript objects are released promptly for garbage collection.
3.  **Query Complexity**: Although `jsoncons` provides an efficient implementation, overly complex or deeply nested queries can still affect performance.

## Best Practices

To fully utilize JMESPath and maintain clear and efficient code, follow these best practices:

1.  **Prioritize JMESPath expressions**: Whenever possible, encapsulate data extraction and transformation logic within JMESPath expressions, rather than manually writing loops and conditions in JavaScript. This will make your rules more concise and readable.
2.  **Understand data structure**: Before writing JMESPath expressions, ensure you have a clear understanding of the JSON data's structure.
3.  **Test your expressions**: For complex JMESPath expressions, it is recommended to test them in independent tools (such as online JMESPath debuggers) to ensure they work as expected.
4.  **Handle `null` results**: JMESPath queries typically return `null` when no match is found. In your JavaScript code, always check if the return value of the `jmespath()` function is `null` or `undefined`.
5.  **Performance optimization**: For JMESPath queries frequently executed in rules, consider whether the JSON data can be pre-processed, or optimize the query expression to reduce computation.

## Version History

### v1.0 - Basic Implementation (Deprecated)
-   ✅ Basic property access
-   ✅ Array length query
-   ✅ Basic error handling

### v2.0 - Full JMESPath Support (Current)
-   ✅ Integrated `jsoncons` library, providing full JMESPath specification support.
-   ✅ Supports all JMESPath built-in functions, array projection, filtering, slicing, etc.
-   ✅ Error handling mechanism updated to throw JavaScript exceptions.
-   ✅ Improved query stability and performance.
