# RulesForge Deployment Guide

This guide focuses on the supported product shape: a Drools-like rule scripting engine with RETE inference, dynamic schema/data binding, and JIT-backed dynamic script execution.

## 1. Build Once, Reuse Often

The intended production shape is:

- compile rules into one `KnowledgeBase`
- register host callbacks during setup only when rules need external side effects
- create many short-lived or pooled `StatefulSession` instances from that knowledge base

Do not recompile the same rules for every request.

## 2. Thread Safety

Current contract:

- `KnowledgeBase`: safe to share only after setup is complete
- `StatefulSession`: not thread-safe

Practical rule:

- use one session per worker thread, request, or message flow

## 3. Data Ingestion Choices

Choose one simple path for each integration boundary:

- JSON for general service integrations
- CSV for batch/offline loads
- XML when upstream systems already publish XML payloads
- binary TBE payloads when you control the schema/payload and need throughput

RulesForge engine sessions receive facts. Schema-aware C API helpers can use `TurboScript::DataBind` to bind JSON, CSV, XML, or binary payloads into session-owned facts before insertion.

## 4. Validation And Failure Mode

RulesForge exposes session validation mode and per-call status results through the C API.

Production advice:

- reject malformed facts early
- keep representative sample payloads in CI
- do not hide rule compilation errors behind fallback behavior

The right failure mode is usually “fail fast during load or test”, not “invent magic defaults”.

## 5. Host Callback Boundary

RulesForge keeps extension code behind explicit host callback boundaries:

- RHS host callbacks registered directly into a knowledge base
- helper predicates registered for supported `eval(native.name(...))` expressions

Operational rules:

- keep callbacks deterministic
- limit external I/O in rule-triggered code
- version callback code with the rule pack that needs it

## 6. Observability

The C++ runtime exposes:

- rule execution tracing
- rule performance summaries
- session metrics exporters

Use them for diagnostics and profiling, not as an excuse to leave noisy tracing enabled in normal production traffic.

## 7. Packaging

Public install surface from this repo includes:

- C headers under `include/`
- `rule_forge` shared library from `capi/`
- exported CMake package files under `lib/cmake/RulesForge`

If you are shipping RulesForge as a product dependency, treat that surface as the contract and keep private headers out of downstream integration docs.

## 8. Recommended Release Checklist

- confirm build inputs for `TurboNet`, `TurboScript`, and `vcpkg`
- compile rules in CI
- run `ctest --output-on-failure`
- exercise at least one schema-bound JSON or CSV example
- verify host callback registration if your rules depend on callbacks
- document the exact rule pack, callback implementation, and app version together

## 9. Related Docs

- onboarding: [`QUICKSTART.md`](/C:/projects/cpp/rulesforge/docs/QUICKSTART.md)
- product guide: [`USER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/USER_GUIDE.md)
- exact DSL reference: [`dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md)
- data binding ownership: [`TURBOSCRIPT_DATABIND_PARSER_COMPARISON.md`](/C:/projects/cpp/rulesforge/docs/TURBOSCRIPT_DATABIND_PARSER_COMPARISON.md)
