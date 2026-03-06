# RulesForge - C++ Rete Rule Engine with JavaScript Integration

[中文文档](./docs/zh-CN/README.md)

This is a sophisticated, high-performance C++ rules engine implementing the **Rete algorithm** with **JavaScript (QuickJS) scripting** for rule consequences, inspired by Java RulesForge but designed specifically for C++ environments.

## Core Architecture

**Rete Algorithm Implementation**:

- Full Rete network with alpha/beta nodes (`rulesforge/include/rete/rete_node.hpp:15175 lines`)
- Immutable `KnowledgeBase` containing compiled rules (`rulesforge/include/knowledge_base.hpp:74`)
- Mutable `StatefulSession` managing working memory (`rulesforge/include/stateful_session.hpp:141`)

**JavaScript Integration**:

- `JSScriptingManager` bridges C++ and JavaScript (`rulesforge/src/rfl_js_manager.cpp`)
- QuickJS library for seamless C++/JavaScript binding (`vcpkg.json:90-92`)
- Custom `rfl` API exposed to JavaScript for fact manipulation

## Key Features

1. **RFL Language**: RulesForge-like syntax with comprehensive grammar ([docs/dsl.md](./docs/dsl.md), [docs/USER_GUIDE.md](./docs/USER_GUIDE.md))
2. **Advanced Conditional Logic**: Support for `not`, `exists`, `forall` patterns
3. **Data Aggregation**: Built-in accumulators (`sum`, `count`, `average`, etc.)
4. **Truth Maintenance System (TMS)**: Logical assertions with automatic dependency tracking
5. **Complex Event Processing**: Temporal operators for CEP scenarios
6. **Serializable Networks**: Fast startup through network serialization

## Technology Stack

- **C++20** standard with modern practices
- **QuickJS** for JavaScript integration
- **TinyTest** for testing
- **jsoncons** for json path query
- **CMake** build system with vcpkg dependency management

## Project Structure

```
rulesforge/
├── include/           # Headers (AST, parser, Rete nodes, JavaScript manager)
├── src/              # Implementation files
├── test/             # Comprehensive test suite
docs/                 # Documentation
├── dsl.md           # Grammar specification
├── USER_GUIDE.md    # User guide
└── README.md        # Quick start guide (Chinese)
```

## WebAssembly Demo 🐳

Experience the power of RulesForge in your browser! We've created a complete **WebAssembly demo** that runs the C++ engine directly in web browsers.

### 🎮 Live Demo Features

- ✅ **Browser-native Rule Engine**: Run C++ RulesForge engine directly via WebAssembly
- ✅ **Interactive Demo**: Complete web interface for testing rule functionality
- ✅ **Real-time Performance**: Live metrics and console logging
- ✅ **Sample Business Rules**: Pre-built customer classification rules
- ✅ **JSON I/O**: Native JSON fact handling and query results
- ✅ **Memory Monitoring**: Real-time memory usage tracking

### 🚀 Try It Now

```bash
# Build the WebAssembly demo (requires Emscripten)
cd wasm
./build.sh

# Serve the demo locally
cd build
python3 -m http.server 8000

# Open http://localhost:8000 in your browser
```

### 📱 Demo Workflow

1. **Initialize Engine** - Load the WebAssembly module in your browser
2. **Load Sample Rules** - Import business rules for customer classification
3. **Insert Customer Data** - Add JSON facts (Alice: 35yo, $120k income)
4. **Fire Rules** - Execute rules and see automatic VIP classification
5. **Execute Queries** - Retrieve processed results with applied discounts
6. **Monitor Performance** - Real-time metrics and memory usage

### 🔧 Technical Highlights

```cpp
// WebAssembly bindings expose full C API
EMSCRIPTEN_BINDINGS(ruleforge_module) {
    class_<RulesForgeEngine>("RulesForgeEngine")
        .constructor()
        .function("init", &RulesForgeEngine::init)
        .function("createKnowledgeBase", &RulesForgeEngine::createKnowledgeBase)
        .function("createSession", &RulesForgeEngine::createSession)
        .function("insertFact", &RulesForgeEngine::insertFact)
        .function("fireRules", &RulesForgeEngine::fireRules)
        .function("query", &RulesForgeEngine::query);
}
```

The demo demonstrates:

- **Client-side Processing**: Rules execute in browser, data stays local
- **Zero Server Dependencies**: Pure static files
- **Cross-platform Compatibility**: Modern browsers with WebAssembly support
- **Educational Value**: Perfect for learning rule engine concepts

## Quick Start

Here's a simple example to get you up and running.

### 1. Write your rules in a RFL file

**`my_rules.rfl`**

```rfl
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
    insert CanVote { name = $p.name }
end

// A query to find all people who can vote
query "find_voters"
    $cv : CanVote()
end
```

### 2. Use the engine in your C++ application

```cpp
#include "rfl_parser.hpp"
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
    // 1. Load and compile the RFL file into a KnowledgeBase
    std::string rfl_content = read_file("my_rules.rfl");
    ParsingResult result;
    std::shared_ptr<KnowledgeBase> kb = build_knowledge_base(rfl_content, result);

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

This is a production-ready rules engine suitable for complex business rule scenarios requiring both high performance and flexible rule authoring through the combination of declarative RFL syntax and JavaScript scripting capabilities.

## Documentation

For a complete reference on the RFL syntax, features, and advanced design patterns, please see the

- [RFL Language Guide](./docs/USER_GUIDE.md)
- [RFL Grammar Guide](./docs/dsl.md)
