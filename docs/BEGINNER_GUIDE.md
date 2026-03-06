# RulesForge Rules Engine - Absolute Beginner's Guide

*Never used a rules engine before? Start here.*

## What is a Rules Engine?

Imagine you run an online store and have business rules like:
- "Customers who spend over $1000 become VIP members"
- "Send welcome email to new customers"
- "Apply 10% discount if cart has 5+ items"

Instead of hardcoding these in if-statements scattered across your application, a **rules engine** lets you write them in a simple, readable format and execute them efficiently.

## Why Use RulesForge?

```cpp
// Without RulesForge - scattered business logic
if (customer.balance > 1000 && customer.orders.size() > 5) {
    customer.status = "VIP";
    send_notification(customer, "Welcome to VIP!");
    apply_discount(customer, 0.15);
}

// Check somewhere else in the code...
if (customer.age < 18) {
    reject_order(order, "Age verification required");
}

// And somewhere else...
if (customer.failed_logins > 3) {
    lock_account(customer);
}
```

```rfl
// With RulesForge - all business logic in one place
rule "Promote to VIP"
when
    $c: Customer(balance > 1000, orderCount > 5)
    not VipCustomer(customerId == $c.id)
then
    update $c { status = "VIP" }
    insert Notification { customerId = $c.id, message = "Welcome to VIP!" }
end

rule "Age Verification"
when
    $o: Order()
    $c: Customer(id == $o.customerId, age < 18)
then
    insert OrderRejection { orderId = $o.id, reason = "Age verification required" }
end

rule "Account Security"
when
    $c: Customer(failedLogins > 3, accountLocked == false)
then
    update $c { accountLocked = true }
end
```

## Your First 10-Minute Example

### Step 1: Install and Build

```bash
# Clone the project
git clone <your-repo-url>
cd rulesforge

# Build (requires CMake and C++20)
cmake --preset=default
cmake --build build
```

### Step 2: Create Your First Rules File

Create `my_first_rules.rfl`:

```rfl
// Tell the system what data looks like
declare Person
    name: String
    age: int
    hasLicense: boolean
end

declare CanDrive
    name: String
    reason: String
end

// The actual business rule
rule "Can Drive Check"
when
    // Find a person who is 16 or older and has a license
    $person: Person(age >= 16, hasLicense == true)

    // Make sure we haven't already processed them
    not CanDrive(name == $person.name)
then
    // Native actions that run when the rule matches
    insert CanDrive {
        name = $person.name,
        reason = "eligible"
    }
end
```

### Step 3: Use It in C++ Code

Create `test_driving.cpp`:

```cpp
#include "knowledge_base.hpp"
#include "stateful_session.hpp"
#include <iostream>
#include <fstream>

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main() {
    // 1. Load the rules
    std::string rules = read_file("my_first_rules.rfl");
    ParsingResult result;
    auto knowledge_base = build_knowledge_base(rules, result);

    if (!result.success) {
        std::cerr << "Rules failed to compile!" << std::endl;
        return 1;
    }

    // 2. Create a session (think of it as your workspace)
    auto session = knowledge_base->create_session();

    // 3. Add some people
    auto person1 = std::make_shared<Fact>();
    person1->type = "Person";
    person1->fields["name"] = "Alice";
    person1->fields["age"] = static_cast<int64_t>(17);
    person1->fields["hasLicense"] = true;
    session->add_fact(person1);

    auto person2 = std::make_shared<Fact>();
    person2->type = "Person";
    person2->fields["name"] = "Bob";
    person2->fields["age"] = static_cast<int64_t>(15);
    person2->fields["hasLicense"] = true;
    session->add_fact(person2);

    // 4. Run the rules!
    int rules_fired = session->fire_all_rules();
    std::cout << "Executed " << rules_fired << " rules" << std::endl;

    // 5. Check what happened
    auto drivers = session->get_facts_of_type("CanDrive");
    std::cout << "Found " << drivers.size() << " people who can drive" << std::endl;

    return 0;
}
```

### Step 4: Compile and Run

```bash
# Compile (adjust paths as needed)
g++ -std=c++20 -I./rulesforge/include -L./build/lib test_driving.cpp -lruleforge -o test_driving

# Run
./test_driving
```

**Expected Output:**
```
Executed 1 rules
Found 1 people who can drive
```

Bob didn't qualify because he's only 15!

## Key Concepts Explained Simply

### Facts = Your Data
Think of facts as rows in a database table:

```cpp
// This is a "Person" fact
auto person = std::make_shared<Fact>();
person->type = "Person";
person->fields["name"] = "Alice";
person->fields["age"] = static_cast<int64_t>(17);
person->fields["hasLicense"] = true;
```

### Rules = Your Business Logic
Rules have two parts:
- **WHEN** (conditions): What pattern to look for
- **THEN** (actions): What to do when you find it

```rfl
rule "Rule Name"
when
    // Conditions: when this pattern matches...
then
    // Actions: do this!
end
```

### Sessions = Your Workspace
- Add facts to a session
- Run rules against those facts
- Rules can create new facts or modify existing ones

```cpp
session->add_fact(my_data);     // Put data in
session->fire_all_rules();      // Process it
auto results = session->get_facts_of_type("Result"); // Get results out
```

## Common Beginner Patterns

### Pattern 1: Simple Filtering
"Find all adults"

```rfl
declare Adult
    name: String
end

rule "Find Adults"
when
    $person: Person(age >= 18)
    not Adult(name == $person.name)
then
    insert Adult { name = $person.name }
end
```

### Pattern 2: Validation
"Check if data is valid"

```rfl
declare ValidationError
    field: String
    message: String
end

rule "Validate Age"
when
    $person: Person(age < 0)
then
    insert ValidationError {
        field = "age",
        message = "Age must be non-negative"
    }
end
```

### Pattern 3: Calculations
"Calculate derived values"

```rfl
declare CreditScore
    name: String
    score: double
end

rule "Calculate Credit Score"
when
    $person: Person(age > 0, income > 0)
    not CreditScore(name == $person.name)
then
    insert CreditScore {
        name = $person.name,
        score = min(850, $person.income / 1000 + $person.age * 10)
    }
end
```

## Next Steps

Once you're comfortable with the basics:

1. **Try More Examples**: Look in the `/rulesforge/example/` directory
2. **Learn More Patterns**: Check out the [Quick Start Guide](QUICKSTART.md)
3. **Advanced Features**: Move to the [User Guide](USER_GUIDE.md)
4. **Real Projects**: See the [Professional Guide](PROFESSIONAL_GUIDE.md)

## Common Beginner Mistakes

### ❌ Forgetting to declare types
```rfl
// Wrong - no declaration
rule "Bad Rule"
when
    $p: Person(age > 18)  // What is Person?
```

```rfl
// Correct - declare first
declare Person
    age: int
end

rule "Good Rule"
when
    $p: Person(age > 18)
```

### ❌ Wrong field types
```rfl
declare Person
    age: String  // Wrong - age should be int
end
```

### ❌ Using && in conditions
```rfl
rule "Wrong"
when
    $p: Person(age >= 18 && income > 1000)  // && doesn't work here
then
    // ...
end
```

```rfl
rule "Correct"
when
    $p: Person(age >= 18, income > 1000)  // Use comma to separate constraints
then
    // ...
end
```

## Troubleshooting

### "Rules don't fire"
1. Check that your facts match the `declare` statements exactly
2. Verify field types (int vs String vs boolean)
3. Enable tracing: `session->enable_tracing(true)`

### "Compilation errors"
1. Make sure every `declare` block has matching field types
2. Check for typos in field names
3. Ensure RFL syntax is correct (commas between constraints)

### "Infinite loop"
1. `update` triggers RETE re-evaluation — make sure LHS no longer matches after update
2. Add a guard constraint (e.g., `status == "pending"`) and change it in the RHS

## Help and Resources

- 📖 **More Examples**: `/rulesforge/example/` directory
- 🐛 **Issues**: Report bugs on GitHub
- 💡 **Questions**: Check existing documentation first
- ⚡ **Performance**: Worry about this later - get it working first!

---

*Ready to level up? Check out the [Quick Start Guide](QUICKSTART.md) for more realistic examples!*
