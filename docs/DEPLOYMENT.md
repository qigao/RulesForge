# RulesForge Deployment Guide

RulesForge is an embedded library, not a network service. The embedding process
owns isolation, scheduling, durable state, and delivery to external systems.

## Build and Package

The supported installed surface is:

- `rulesforge/include/rules_forge.h` for the C ABI;
- the `RulesForge` shared library;
- exported CMake package files under `lib/cmake/RulesForge`.

Clients link the exported `RulesForge::RulesForge` target and call the public C
ABI. The current LLVM-MinGW artifact is `libRulesForge.dll` with import library
`libRulesForge.dll.a`; use the matching profile's SDK filenames. The
[development guide](DEVELOPMENT_GUIDE.md#sdk与dll接入) explains the client and
provider deployment boundary.

Build and test with the repository CMake presets. Package the header and binary
from the same build; verify `ruleforge_get_version()` during startup when the
host has a strict version requirement.

## Lifecycle

Compile each rule pack into a knowledge base once, including its MIR scalar
programs, before sharing it. Then stop mutating it. Create separate stateful or
continuous sessions for independent workloads. For Programs, the existing bundle
`mode` selects Toad interpreter/JIT execution; it does not configure a second
core rule backend or expose a C API for registering native LHS predicates.

Never share a mutable session concurrently. Pin a session and its DataBind
streams to one owner thread when using an async framework. Program sessions
must be created, used and destroyed on that thread. See [Script Programs](SCRIPT_PROGRAM.md)
for the native provider and external Toad runtime deployment requirements.

## Input Boundary

Treat `.schema` and RFL files as one versioned rule pack. Validate both during
CI using representative JSON/YAML/CSV/XML/binary payloads. Reject a deployment when
schema imports, rule compilation, or sample binding fails.

Choose complete-document APIs for bounded payloads already in memory. Choose
incremental streams for chunked input. The stream API does not provide an event
loop; see [Data ingestion](./DATA_INGESTION.md).

## Capacity and Backpressure

For stateful sessions, bound `fire_all_rules` and the number of inserted facts
at the host boundary. Destroy or explicitly clear sessions according to the
application lifecycle.

Budget rule-loading compilation and generated code separately from per-session
facts. Core expression source/node limits bound individual programs; they are not
a process RSS limit. For Programs, generation-pinned sessions keep their original
code alive until released. Budget for the current and retained retiring generation.

For continuous sessions, configure all capacity fields from expected event and
output rates. A `DRAIN_REQUIRED` result is backpressure: drain before accepting
more input. Acknowledge result batches in order so pending-result capacity is
released.

## Failure Model

- Check every returned status and capture the last error before another API call.
- Treat compilation and schema errors as deployment failures.
- Destroy failed DataBind streams; do not retry them in place.
- After failed rollback or nontransactional network propagation, do not continue
  firing, querying or mutating the inconsistent stateful session. Explicitly reset it
  successfully, or recreate it from authoritative input; reset clears its facts.
- Persist continuous input outside RulesForge when restart recovery is required.
- Do not introduce fallback parsing with a different schema or semantics.

## External Side Effects

Rules filter and derive facts; they do not call external services. Read queries
or continuous output snapshots after a successful commit, then perform side
effects in host code. Use result batch IDs or domain IDs as idempotency keys
when delivery may be retried.

The [scenario host contract](SCENARIOS.md#ownership-and-mock-apis) demonstrates
copying query results before session destruction, atomic mock persistence and
explicit idempotent delivery. Its sink records all decisions, including WAIT;
it does not execute domain commands. Its delivered flags are in-memory state.
Replacing the mock requires a host-owned transaction and delivery contract;
neither RHS rollback nor a successful mock commit establishes durable external
delivery. See [production adapter boundaries](SCENARIOS.md#failure-and-production-adapters).

## Release Verification

- build the installable library and examples from a clean preset;
- run focused rulesforge/parser, engine, C API, DataBind, and continuous tests;
- run the existing formal C API tests, including their C translation units;
- run every shipped example command;
- validate rule packs and representative payloads in CI;
- verify resource limits with peak-sized inputs;
- run sanitizer builds for changes touching ownership or stream lifecycle;
- record the RulesForge version, rule-pack version, and schema version together.

Record benchmark qualification separately from correctness qualification. The
[dated local snapshot](./architecture/core-ownership-and-dispatch.md#benchmark-snapshot)
includes its workload, sample counts, environment and limits; it does not establish
production throughput or qualify platforms that were not run.

## Related Documents

- [User guide](./USER_GUIDE.md)
- [Runnable scenarios](./SCENARIOS.md)
- [Data ingestion](./DATA_INGESTION.md)
- [Continuous engine](./CONTINUOUS_RULE_ENGINE_DESIGN.md)
- [DSL reference](./dsl.md)
