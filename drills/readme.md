 # C++ Rete Rule Engine

This project is a high-performance, C++-native rule engine that implements the Rete algorithm. It features a declarative rule language inspired by Drools (DRL) and uses Lua for scripting rule consequences, offering a powerful combination of performance and flexibility.

The engine is designed with a clean separation between the immutable, thread-safe `KnowledgeBase` (the compiled rules) and the mutable `StatefulSession` (the working memory), making it suitable for demanding, concurrent applications.

## Key Features

- **Declarative DRL Syntax**: Write powerful and readable business rules using a language inspired by industry-standard Drools.
- **High-Performance Rete Algorithm**: Efficiently evaluate rules against large sets of facts.
- **Lua Scripting for Actions**: Use the flexible and fast Lua language for rule consequences (`then` blocks). A simple `drools` API is provided for engine interaction.
- **Advanced Conditional Logic**: Full support for `not`, `exists`, and `forall` conditional elements.
- **Data Aggregation**: Built-in support for `from accumulate` with functions like `sum`, `count`, `average`, `min`, `max`, `collectList`, and `collectSet`.
- **Collection Handling**: Iterate over nested collections within facts using `from unnest`.
- **Logical Assertions (TMS)**: Use `insertLogical` to create facts that are automatically managed by a Truth Maintenance System.
- **Stateful & Serializable Sessions**: Compile rules once and create multiple, independent sessions. The compiled network is serializable for fast startup.
- **Typed Facts**: Define fact schemas using the `declare` keyword for compile-time validation.
- **Temporal Reasoning**: Built-in temporal operators like `after` and `within` for Complex Event Processing (CEP).

## Quick Start

Here’s a simple example to get you up and running.

#### 1. Write your rules in a DRL file

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
    // The 'then' block is Lua.
    // The $p variable is available as 'p'.
    print("Granting voting rights to: " .. p.name)
    drools.insert({ type="CanVote", name=p.name })
end

// A query to find all people who can vote
query "find_voters"
    $cv : CanVote()
end
```

#### 2. Use the engine in your C++ application

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

## Documentation

For a complete reference on the DRL syntax, features, and advanced design patterns, please see the
#### [DRL Language Guide](./guide.md)
#### [DRL Grammar Guide](./dsl.md)