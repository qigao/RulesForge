# RulesForge Product Readiness

RulesForge is positioned as a Drools-like rule scripting engine: it supports RETE-based inference, dynamic schema/data binding, and JIT-backed dynamic script execution.

Architecture details remain layered: RFL describes facts, rules, queries, and RHS actions; TurboScript::DataBind handles schema-aware data binding; RETE owns propagation planning; and the JIT-backed runtime owns supported predicates, rule kernels, and dynamic script paths.

This document defines what must be true before a RulesForge build can be described as product-ready.

## Release Gate

### HIGH

- Public C API contracts are documented and versioned.
- Non-empty knowledge bases fail build/compile when the JIT execution plan cannot be built or self-checked.
- Unsupported DSL semantics fail with observable parse, semantic, lowering, compile, or runtime errors; they do not fall back to AST tree-walking or old executors.
- RHS actions execute through the TurboScript command/event path, with transaction rollback on runtime failure.
- Host callbacks are explicit boundary operations with documented ownership and error semantics.
- Release smoke tests pass on every supported build preset.

### MED

- Error codes and lowering reasons are documented with user-facing remediation.
- C API examples cover schema-bound JSON, CSV, XML or binary facts, query output, validation, tracing, and memory reporting.
- Benchmarks have a recorded baseline for build time, session creation, fire throughput, RHS actions, query, and data binding.
- Packaging verifies install rules, exported targets, public headers, and shared-library loading.

### LOW

- Product-facing docs consistently describe RulesForge as a Drools-like rule scripting engine with RETE inference, dynamic data binding, and JIT-backed dynamic script execution.
- Debug and explain output identify which predicates and RHS actions are compiled, which fail, and why.
- Examples are checked against current parser/runtime syntax.

## Supported Runtime Architecture

- RFL/parser state is the source of rule semantics.
- `KnowledgeBase::build()` derives the runtime predicate plan, RHS backend plan, and RETE network from that source.
- RETE owns fact/token/window/query/accumulate orchestration and lifecycle.
- TurboScript owns RHS command/event execution, RHS expressions, control flow, and external side-effect boundaries.
- JIT-backed execution owns supported predicate, filter, projection, comparison, collection, temporal, query, aggregation, and dynamic script kernels.
- Host callbacks exist as an integration boundary and must be explicitly registered.

## Data Binding Contract

- RFL schema imports use `import "name.schema"`.
- Installed `TurboScript::DataBind` owns schema reflection and schema-aware parsing for JSON, CSV, XML, and binary TBE payloads.
- RulesForge engine sessions remain fact-only.
- Schema-aware C API helpers bridge external payloads into session-owned facts before insertion.

## Release Smoke Command

Use the release smoke target after configuring with tests enabled:

```bash
cmake --build build --target rulesforge_release_smoke
```

The target runs the core engine, parser, RHS, C API, and demo smoke tests through CTest.

## Benchmarks

Benchmarks are not part of the default release smoke because they are longer-running. Build and run them explicitly when establishing or checking a performance baseline:

```bash
cmake --build build --target fact_rule_benchmark.RulesForge agenda_benchmark.RulesForge token_pool_benchmark.RulesForge
```

Benchmark CTest entries are labeled `benchmark;long` and are disabled by default unless `RULESFORGE_ENABLE_BENCHMARK_TESTS` is enabled.
