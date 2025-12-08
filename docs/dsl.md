# Drools-Inspired Rule Language (DRL) Grammar

## Introduction

This document specifies the grammar for a custom, Drools-inspired rule language designed for a high-performance Rete-based rule engine. The language allows users to define data structures, business rules, and queries in a declarative, SQL-like syntax.

The primary goal is to provide a powerful yet readable way to express complex conditional logic and data aggregations. This implementation is a functional subset of standard Java Drools, focusing on the most common and powerful features.

The language is composed of several top-level statements:

- `package` and `import`: For namespacing and type resolution.
- `global`: To define global variables accessible within rule consequences.
- `declare`: To define the schema for new fact types.
- `query`: To define reusable, parameterized lookups into the engine's working memory.
- `rule`: The core construct for defining conditional logic.

The consequence of a rule (the `then` block) is written in **JavaScript** (via QuickJS), providing a flexible and powerful scripting environment. A custom `drools` API is injected into the JavaScript context to allow the rule to interact with the engine (e.g., by inserting or retracting facts).

## Language Constructs

### File Structure

A DRL file is a sequence of zero or more top-level statements. The typical order is `package`, `import`s, `global`s, `declare`s, `query`s, and finally `rule`s.

```drl
package com.example.rules

import com.example.model.Customer

global java.util.List results

declare Offer
    message: String
end

rule "Example"
when
    // ... conditions
then
    // ... actions
end
```

### Type Declaration (`declare`)

The `declare` statement defines a new fact type and its fields.

**Syntax:** `declare <TypeName> [ <field_name> : <field_type> ]... end`

| Type      | Description                               |
|-----------|-------------------------------------------|
| `String`  | A string of text.                        |
| `int`     | A 64-bit signed integer.                 |
| `double`  | A 64-bit floating-point number.          |
| `boolean` | A true/false value.                      |
| Other     | Can be a previously declared custom type.|

**Example:**

```drl
declare Customer
    id: int
    name: String
    verified: boolean
end
```

### Rule Definition (`rule`)

The `rule` is the core of the language. It consists of a name, optional attributes, a `when` block (conditions), and a `then` block (actions).

**Syntax:** `rule "<RuleName>" [attributes...] when [conditions...] then [actions...] end`

#### Attributes

Attributes control the rule's execution behavior.

- `salience <integer>`: Sets the rule's priority. Higher salience rules fire first. Default is 0.
- `agenda-group "<GroupName>"`: Assigns the rule to a specific agenda group. The rule will only fire when its group has focus.
- `extends "<ParentRuleName>"`: The rule inherits all conditions from the parent rule.

#### LHS (The `when` Block)

The Left-Hand Side (LHS) contains a set of patterns that must be satisfied for the rule to activate.

- **Patterns:** A pattern matches a fact of a specific type. It can optionally bind the matched fact to a variable.
  - `$c: Customer()` - Matches a `Customer` fact and binds it to the variable `$c`.
  - `Person()` - Matches any `Person` fact without binding it.

- **Constraints:** Constraints are placed inside parentheses `()` after the fact type to filter matches.
  - `age > 18`
  - `status == "Gold"`
  - `orderId == $c.id` (Joining to another bound fact)

- **Inline Bindings:** A constraint can also bind a field's value to a variable for use later.
  - `$c: Customer( $id: id, age > 18 )` - Binds the `id` field of the matched customer to `$id`.

- **Logical Operators:**
  - **AND:** Constraints within the same pattern (separated by `,` or `&&`) or patterns on separate lines are implicitly ANDed together.
  - **OR:** The `or` keyword separates mutually exclusive blocks of patterns. The rule will fire if *any* of the `or` blocks are satisfied.

- **Conditional Elements:**
  - `not ( <pattern> )`: Succeeds only if no fact matches the nested pattern.
  - `exists ( <pattern> )`: Succeeds if at least one fact matches the nested pattern.
  - `forall ( <base_pattern>, <restriction_pattern> )`: Succeeds if for all facts that match `base_pattern`, they *also* match `restriction_pattern`.
  - `eval( <javascript_expression> )`: Executes a boolean JS expression in the context of the current match.

- **`from` Clause:** Modifies the data source for a pattern.
  - `from accumulate( <source_pattern>, <function> )`: Aggregates data from facts matching the source pattern.
    - **Functions:** `sum`, `count`, `average`, `min`, `max`, `collectList`, `collectSet`.
    - **Example:** `$s: Sum() from accumulate( $p: Purchase(), sum($p.value) )`
  - `from unnest( <collection_field> )`: Creates a match for each item in a fact's collection field.
    - **Example:** `$item: Item() from unnest( $order.items )`

#### RHS (The `then` Block)

The Right-Hand Side (RHS) contains the actions to be executed when the rule fires. The RHS is **JavaScript code**.

- **Bound Variables:** Variables bound in the `when` block (e.g., `$c`) are available in JavaScript without the `$` prefix (e.g., `c`). You can access their fields like `c.name`.

- **`drools` API:** A special `drools` object is available to interact with the engine.
  - `drools.insert({type: "...", field1: val1, ...})`: Inserts a new fact.
  - `drools.insertLogical({type: "...", ...})`: Inserts a fact that is logically dependent on the facts that activated the rule. It will be automatically retracted if the rule's conditions become false.
  - `drools.retract(fact_variable)`: Retracts a fact from memory.

**Example:**
```javascript
// Insert a new fact
drools.insert({type: "Offer", message: "Welcome, " + c.name});

// Logical insertion - auto-retracted when conditions no longer match
drools.insertLogical({type: "Alert", reason: "High value customer"});

// Retract an existing fact
drools.retract(c);
```

### Queries

Queries are named, reusable sets of patterns that can be called from C++. They can be parameterized.

**Syntax:** `query "<QueryName>" [ ( <param_type> <$param_name> ) ] [patterns...] end`

**Example:**

```drl
// A query with one parameter
query findCustomer(NameHolder $name)
    $c: Customer(name == $name.value)
end

// A query with no parameters
query findVips
    $v: VipCustomer()
end
```

---

## Formal ABNF-like Grammar

This section provides a formal definition of the language syntax using a simplified Backus-Naur Form.

```abnf
; ---------------------------------------------
; 1. Core Primitives
; ---------------------------------------------
rulelist      = *statement
statement     = package-stmt / import-stmt / global-stmt / declaration-stmt / query-stmt / rule-stmt
OWS           = *(WSP / comment) ; Optional Whitespace & Comments
S             = 1*(WSP / comment) ; Required Whitespace & Comments
WSP           = " " / HTAB / EOL
comment       = ("//" / "#") *any-char EOL / "/*" *any-char "*/"

; ---------------------------------------------
; 2. Identifiers and Literals
; ---------------------------------------------
identifier    = ALPHA *(ALPHA / DIGIT / "_")
qualified-name = identifier *("." identifier)
binding       = "$" identifier
integer       = [ "-"] 1*DIGIT
double        = [ "-"] 1*DIGIT "." 1*DIGIT
string-literal = DQUOTE *any-char-but-quote DQUOTE

; ---------------------------------------------
; 3. Top-Level Statements
; ---------------------------------------------
package-stmt  = "package" S qualified-name [";"]
import-stmt   = "import" S qualified-name ["." "*"] [";"]
global-stmt   = "global" S qualified-name S identifier [";"]

declaration-stmt = "declare" S identifier OWS *(field-def OWS) "end"
field-def        = identifier OWS ":" OWS qualified-name

query-stmt = "query" S (string-literal / identifier) [query-params] OWS lhs "end"
query-params = "(" OWS [query-param * (OWS "," OWS query-param)] OWS ")"
query-param  = qualified-name S binding

rule-stmt = [annotation-list] "rule" S string-literal OWS [attributes] OWS "when" OWS lhs OWS "then" OWS rhs OWS "end"

; ---------------------------------------------
; 4. Rule Structure
; ---------------------------------------------
attributes = 1*(attribute OWS)
attribute  = salience-attr / agenda-group-attr / extends-attr

salience-attr     = "salience" S integer
agenda-group-attr = "agenda-group" S string-literal
extends-attr      = "extends" S string-literal

; --- LHS (when block) ---
lhs = pattern-group *(OWS "or" OWS pattern-group)
pattern-group = 1*(pattern OWS)

pattern = [binding OWS ":" OWS] (std-pattern / not-pattern / exists-pattern / forall-pattern / eval-pattern)

std-pattern = qualified-name [ "(" OWS [expression] OWS ")" ] [from-clause]
not-pattern = "not" OWS "(" OWS pattern OWS ")"
exists-pattern = "exists" OWS "(" OWS pattern OWS ")"
forall-pattern = "forall" OWS "(" OWS pattern *(OWS "," OWS pattern) OWS ")"
eval-pattern = "eval" OWS "(" *any-char ")" ; content is opaque JavaScript

; --- LHS Constraints ---
expression = or-expr
or-expr    = and-expr *(OWS "||" OWS and-expr)
and-expr   = constraint *(OWS ("," / "&&") OWS constraint)
constraint = [binding ":"] constraint-field [ (comp-op OWS value) / (temporal-op S value) ]
value      = literal / constraint-field
literal    = integer / double / string-literal / "true" / "false" / "nil"
comp-op    = "==" / "!=" / ">" / "<" / ">=" / "<="
temporal-op = "after" / "before" / ("within" S duration S "of")

; --- LHS `from` clause ---
from-clause = "from" S (accumulate-clause / unnest-clause)
accumulate-clause = "accumulate" OWS "(" OWS pattern "," OWS accum-func OWS ")"
unnest-clause = "unnest" OWS "(" OWS constraint-field OWS ")"
accum-func = identifier "(" OWS constraint-field OWS ")"

; --- RHS (then block) ---
rhs = *any-char ; content is opaque JS code until 'end'