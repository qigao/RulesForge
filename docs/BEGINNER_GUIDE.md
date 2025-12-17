# Drills Rules Engine - Absolute Beginner's Guide

*Never used a rules engine before? Start here.*

## What is a Rules Engine?

Imagine you run an online store and have business rules like:
- "Customers who spend over $1000 become VIP members"
- "Send welcome email to new customers"
- "Apply 10% discount if cart has 5+ items"

Instead of hardcoding these in if-statements scattered across your application, a **rules engine** lets you write them in a simple, readable format and execute them efficiently.

## Why Use Drills?

```cpp
// Without Drills - scattered business logic
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

```drl
// With Drills - all business logic in one place
rule "Promote to VIP"
when
    $c: Customer(balance > 1000, orderCount > 5)
then
    console.log(`${c.name} is now VIP!`);
    drools.update(c, {status: "VIP"});
    drools.insert({type: "Notification", message: "Welcome to VIP!"});
end

rule "Age Verification"
when
    $o: Order(), $c: Customer(id == $o.customerId, age < 18)
then
    drools.insert({type: "OrderRejection", reason: "Age verification required"});
end

rule "Account Security"
when
    $c: Customer(failedLogins > 3)
then
    drools.update(c, {accountLocked: true});
end
```

## Your First 10-Minute Example

### Step 1: Install and Build

```bash
# Clone the project
git clone <your-repo-url>
cd drills

# Build (requires CMake and C++20)
cmake --preset=default
cmake --build build
```

### Step 2: Create Your First Rules File

Create `my_first_rules.drl`:

```drl
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
    // This is JavaScript code that runs when the rule matches
    console.log($person.name + " can drive!");

    // Add new facts to the system
    drools.insert({
        type: "CanDrive",
        name: person.name,
        reason: "Age " + person.age + " and has license"
    });
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
    std::string rules = read_file("my_first_rules.drl");
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
g++ -std=c++20 -I./drills/include -L./build/lib test_driving.cpp -ldrills -o test_driving

# Run
./test_driving
```

**Expected Output:**
```
Alice can drive!
Executed 1 rules
Found 1 people who can drive
```

Bob didn't qualify because he's only 15!

## Key Concepts Explained Simply

### Facts = Your Data
Think of facts as rows in a database table:

```cpp
// This is a "Person" fact
{
  type: "Person",
  name: "Alice",
  age: 17,
  hasLicense: true
}
```

### Rules = Your Business Logic
Rules have two parts:
- **WHEN** (conditions): What pattern to look for
- **THEN** (actions): What to do when you find it

```drl
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

```drl
declare Adult
    name: String
end

rule "Find Adults"
when
    $person: Person(age >= 18)
then
    drools.insert({type: "Adult", name: person.name});
end
```

### Pattern 2: Validation
"Check if data is valid"

```drl
declare ValidationError
    field: String
    message: String
end

rule "Validate Email"
when
    $person: Person($email: email)
    eval($email == null || $email == "" || !$email.includes("@"))
then
    drools.insert({
        type: "ValidationError",
        field: "email",
        message: "Email is required and must contain @"
    });
end
```

### Pattern 3: Calculations
"Calculate derived values"

```drl
declare CreditScore
    name: String
    score: int
end

rule "Calculate Credit Score"
when
    $person: Person(age > 0, income > 0)
then
    let score = Math.min(850, person.income / 1000 + person.age * 10);
    drools.insert({
        type: "CreditScore",
        name: person.name,
        score: score
    });
end
```

## Next Steps

Once you're comfortable with the basics:

1. **Try More Examples**: Look in the `/drills/example/` directory
2. **Learn More Patterns**: Check out the [Quick Start Guide](QUICKSTART.md)
3. **Advanced Features**: Move to the [User Guide](USER_GUIDE.md)
4. **Real Projects**: See the [Professional Guide](PROFESSIONAL_GUIDE.md)

## Common Beginner Mistakes

### ❌ Forgetting to declare types
```drl
// Wrong - no declaration
rule "Bad Rule"
when
    $p: Person(age > 18)  // What is Person?
```

```drl
// Correct - declare first
declare Person
    age: int
end

rule "Good Rule"
when
    $p: Person(age > 18)
```

### ❌ Wrong field types
```drl
declare Person
    age: String  // Wrong - age should be int
end
```

### ❌ JavaScript syntax in conditions
```drl
rule "Wrong"
when
    $p: Person(age >= 18 && income > 1000)  // JavaScript syntax doesn't work here
then
    // JavaScript goes here
end
```

```drl
rule "Correct"
when
    $p: Person(age >= 18, income > 1000)  // Use comma, not &&
then
    // JavaScript goes here
end
```

## Troubleshooting

### "Rules don't fire"
1. Check that your facts match the `declare` statements exactly
2. Verify field types (int vs String vs boolean)
3. Add `console.log()` statements to see what's happening

### "Compilation errors"
1. Make sure every `declare` block has matching field types
2. Check for typos in field names
3. Ensure DRL syntax is correct (commas between constraints)

### "Runtime errors"
1. Check JavaScript syntax in `then` blocks
2. Verify that variables exist before using them
3. Use `try/catch` blocks for debugging

## Help and Resources

- 📖 **More Examples**: `/drills/example/` directory
- 🐛 **Issues**: Report bugs on GitHub
- 💡 **Questions**: Check existing documentation first
- ⚡ **Performance**: Worry about this later - get it working first!

---

*Ready to level up? Check out the [Quick Start Guide](QUICKSTART.md) for more realistic examples!*
