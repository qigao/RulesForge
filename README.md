# Drills - C++ Rete Rule Engine with JavaScript Integration

This is a sophisticated, high-performance C++ rules engine implementing the **Rete algorithm** with **JavaScript (QuickJS) scripting** for rule consequences, inspired by Java Drools but designed specifically for C++ environments.

## Core Architecture

**Rete Algorithm Implementation**: 
- Full Rete network with alpha/beta nodes (`drills/include/rete/rete_node.hpp:15175 lines`)
- Immutable `KnowledgeBase` containing compiled rules (`drills/include/knowledge_base.hpp:74`)
- Mutable `StatefulSession` managing working memory (`drills/include/stateful_session.hpp:141`)

**JavaScript Integration**:
- `JSScriptingManager` bridges C++ and JavaScript (`drills/src/drools_js_manager.cpp`)
- QuickJS library for seamless C++/JavaScript binding (`vcpkg.json:90-92`)
- Custom `drools` API exposed to JavaScript for fact manipulation

## Key Features

1. **DRL Language**: Drools-like syntax with comprehensive grammar (`drills/dsl.md`, `drills/guide.md`)
2. **Advanced Conditional Logic**: Support for `not`, `exists`, `forall` patterns
3. **Data Aggregation**: Built-in accumulators (`sum`, `count`, `average`, etc.)
4. **Truth Maintenance System (TMS)**: Logical assertions with automatic dependency tracking
5. **Complex Event Processing**: Temporal operators for CEP scenarios
6. **Serializable Networks**: Fast startup through network serialization

## Technology Stack

- **C++20** standard with modern practices
- **QuickJS** for JavaScript integration
- **PEGTL** for parsing (`vcpkg.json:66-68`)
- **Catch2** for testing
- **Magic Enum**, **nlohmann-json**, **spdlog** for utilities
- **CMake** build system with vcpkg dependency management

## Project Structure

```
drills/
├── include/           # Headers (AST, parser, Rete nodes, Lua manager)
├── src/              # Implementation files
├── test/             # Comprehensive test suite
├── docs/             # Documentation
├── examples/         # Usage examples (currently empty)
├── dsl.md           # Grammar specification
├── guide.md         # User guide
└── readme.md        # Quick start guide
```

## Quick Start

Here's a simple example to get you up and running.

### 1. Write your rules in a DRL file

**`my_rules.drl`**
```drl
// Define the data model for our facts
declare Person
    name : String
    age : int
end

declare CanVote
    name : String
end

// A rule that finds adults and asserts they can vote
rule "Identify Voters"
when
    // Match a Person fact where the age is 18 or greater
    // and bind it to the variable $p
    $p : Person( age >= 18 )

    // Ensure we haven't already processed this person
    not ( CanVote( name == $p.name ) )
then
    // The 'then' block is JavaScript.
    // The $p variable is available as 'p'.
    console.log("Granting voting rights to: " + p.name);
    drools.insert({ type: "CanVote", name: p.name });
end

// A query to find all people who can vote
query "find_voters"
    $cv : CanVote()
end
```

### 2. Use the engine in your C++ application

```cpp
#include "drools_parser.hpp"
#include "knowledge_base.hpp"
#include "stateful_session.hpp"
#include "query_result.hpp"
#include "errors.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

// Helper to read a file into a string
std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main() {
    // 1. Load and compile the DRL file into a KnowledgeBase
    std::string drl_content = read_file("my_rules.drl");
    ParsingResult result;
    std::shared_ptr<KnowledgeBase> kb = build_knowledge_base(drl_content, result);

    if (!result.success) {
        for (const auto& err : result.errors) {
            std::cerr << err.to_string() << std::endl;
        }
        return 1;
    }

    // 2. Create a stateful session from the KnowledgeBase
    std::unique_ptr<StatefulSession> session = kb->create_session();

    // 3. Add facts to the session's working memory
    auto person1 = std::make_shared<Fact>();
    person1->type = "Person";
    person1->fields["name"] = "Alice";
    person1->fields["age"] = (int64_t)30;
    session->add_fact(person1);

    auto person2 = std::make_shared<Fact>();
    person2->type = "Person";
    person2->fields["name"] = "Bob";
    person2->fields["age"] = (int64_t)16;
    session->add_fact(person2);

    // 4. Fire the rules
    int fired_count = session->fire_all_rules();
    std::cout << "\nFired " << fired_count << " rule(s).\n";

    // 5. Query the results
    QueryResult query_results = session->execute_query("find_voters");
    std::cout << "Found " << query_results.size() << " person/people who can vote.\n";
    for (const auto& row : query_results) {
        auto name = row.getFieldAs<std::string>("$cv", "name");
        if (name) {
            std::cout << " - " << *name << std::endl;
        }
    }

    // Expected Output:
    // Granting voting rights to: Alice
    //
    // Fired 1 rule(s).
    // Found 1 person/people who can vote.
    //  - Alice

    return 0;
}
```

## Strengths

- **Performance-oriented**: Rete algorithm optimized for high-throughput scenarios
- **Clean separation**: Immutable knowledge base vs. mutable sessions
- **Comprehensive**: Full feature set including TMS, CEP, aggregations
- **Well-documented**: Extensive guides and examples
- **Modern C++**: Leverages C++20 features and best practices
- **Testing**: Thorough test coverage

This is a production-ready rules engine suitable for complex business rule scenarios requiring both high performance and flexible rule authoring through the combination of declarative DRL syntax and JavaScript scripting capabilities.

## Documentation

For a complete reference on the DRL syntax, features, and advanced design patterns, please see the
- [DRL Language Guide](./drills/guide.md)
- [DRL Grammar Guide](./drills/dsl.md)