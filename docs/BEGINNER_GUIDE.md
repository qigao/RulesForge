# RulesForge Beginner Guide

If you have never used a rules engine, keep the model simple:

- facts are your input data
- rules are `when ... then ...`
- a session is the box that holds data while rules run

## The Smallest Useful Mental Model

RulesForge separates two things:

- `KnowledgeBase`: compiled rules, safe to share after setup
- `StatefulSession`: mutable runtime state, one thread at a time

That split matters more than any marketing sentence. Build rules once, create sessions many times.

## A Tiny Rule

```rfl
declare Person
    name: String
    age: int
end

query "Adults"
    $p: Person(age >= 18)
end
```

This query does not change data. It just asks the engine to find all matching facts.

## The Easiest Way To See It Work

Build the repo, then run:

```bash
./build/bin/capi_demo \
  -r docs/examples/loan-eligibility/loan-eligibility.rfl \
  -j docs/examples/loan-eligibility/loan-applications-sample.json \
  -m applications:com.bank.loan.LoanApplication \
  -q LoanDecisions \
  -b decision \
  -f applicationId,approved,approvedAmount,reason
```

Why use `capi_demo` first:

- it uses the public API in [`include/rule_forge.h`](/C:/projects/cpp/rulesforge/include/rule_forge.h)
- it ships with the repo
- it exercises the actual compile/load/fire/query path

## What You Should Learn First

1. How to declare a fact type with `declare`
2. How to write one `query`
3. How to write one `rule`
4. How to load JSON or CSV facts
5. Why sessions are not shared across threads

Do not start with plugin DLLs, custom native callbacks, or router integration unless you already have a concrete integration need.

## Recommended Reading Order

1. [`QUICKSTART.md`](/C:/projects/cpp/rulesforge/docs/QUICKSTART.md)
2. [`USER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/USER_GUIDE.md)
3. [`dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md)
4. [`examples/README.md`](/C:/projects/cpp/rulesforge/docs/examples/README.md)
