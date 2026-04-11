# RulesForge Deployment Guide

This guide focuses on what the current code supports, not on theoretical architecture.

## 1. Build Once, Reuse Often

The intended production shape is:

- compile rules into one `KnowledgeBase`
- register native functions and codecs during setup
- create many short-lived or pooled `StatefulSession` instances from that knowledge base

Do not recompile the same rules for every request unless you enjoy burning CPU for no reason.

## 2. Thread Safety

Current contract:

- `KnowledgeBase`: safe to share only after setup is complete
- `StatefulSession`: not thread-safe

Practical rule:

- one session per worker thread, request, or message flow

## 3. Data Ingestion Choices

Choose one boring path for each boundary:

- JSON for general service integrations
- CSV for batch/offline loads
- binary only when you already control codecs and care about throughput

If your integration does not need all three, do not document or ship all three.

## 4. Validation And Failure Mode

RulesForge exposes session validation mode and per-call status results through the C API.

Production advice:

- reject malformed facts early
- keep representative sample payloads in CI
- do not hide rule compilation errors behind fallback behavior

The right failure mode is usually “fail fast during load or test”, not “invent magic defaults”.

## 5. Native Extensions

There are two extension surfaces:

- native RHS functions loaded into a knowledge base
- source/sink plugins following [`include/rule_forge_plugin.h`](/C:/projects/cpp/rulesforge/include/rule_forge_plugin.h)

Operational rules:

- keep callbacks deterministic
- limit external I/O in rule-triggered code
- version your plugin binaries with the rule pack that needs them

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

If you are shipping RulesForge as a product dependency, treat that surface as the contract and keep private headers out of your downstream integration docs.

## 8. Recommended Release Checklist

- confirm build inputs for `TurboNet`, `TurboScript`, `TurboNet`, and `vcpkg`
- compile rules in CI
- run `ctest --output-on-failure`
- exercise one JSON example and one CSV example
- verify plugin binaries load if your rules depend on them
- document the exact rule pack, plugin pack, and app version together

## 9. Related Docs

- onboarding: [`QUICKSTART.md`](/C:/projects/cpp/rulesforge/docs/QUICKSTART.md)
- product guide: [`USER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/USER_GUIDE.md)
- exact DSL reference: [`dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md)
- data loading practices: [`PRODUCTION_DATABIND_BEST_PRACTICES.md`](/C:/projects/cpp/rulesforge/docs/PRODUCTION_DATABIND_BEST_PRACTICES.md)
