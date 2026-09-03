# RulesForge Deployment Guide

RulesForge is an embedded library, not a network service. The embedding process
owns isolation, scheduling, durable state, and delivery to external systems.

## Build and Package

The supported installed surface is:

- `include/rules_forge.h` for the C ABI;
- the `RulesForge` shared library;
- exported CMake package files under `lib/cmake/RulesForge`.

Build and test with the repository CMake presets. Package the header and binary
from the same build; verify `ruleforge_get_version()` during startup when the
host has a strict version requirement.

## Lifecycle

Compile each rule pack into a knowledge base once. Configure execution mode and
native predicates before sharing it, then stop mutating it. Create separate
stateful or continuous sessions for independent workloads.

Never share a mutable session concurrently. If an async framework moves work
between threads, serialize all calls for a session and its DataBind streams on
one executor.

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

For continuous sessions, configure all capacity fields from expected event and
output rates. A `DRAIN_REQUIRED` result is backpressure: drain before accepting
more input. Acknowledge result batches in order so pending-result capacity is
released.

## Failure Model

- Check every returned status and capture the last error before another API call.
- Treat compilation and schema errors as deployment failures.
- Destroy failed DataBind streams; do not retry them in place.
- Recreate an inconsistent session from authoritative input.
- Persist continuous input outside RulesForge when restart recovery is required.
- Do not introduce fallback parsing with a different schema or semantics.

## External Side Effects

Rules filter and derive facts; they do not call external services. Read queries
or continuous output snapshots after a successful commit, then perform side
effects in host code. Use result batch IDs or domain IDs as idempotency keys
when delivery may be retried.

## Release Verification

- build the installable library and examples from a clean preset;
- run focused parser, engine, C API, DataBind, and continuous tests;
- compile a pure C consumer against the installed header;
- run every shipped example command;
- validate rule packs and representative payloads in CI;
- verify resource limits with peak-sized inputs;
- run sanitizer builds for changes touching ownership or stream lifecycle;
- record the RulesForge version, rule-pack version, and schema version together.

## Related Documents

- [User guide](./USER_GUIDE.md)
- [Data ingestion](./DATA_INGESTION.md)
- [Continuous engine](./CONTINUOUS_RULE_ENGINE_DESIGN.md)
- [DSL reference](./dsl.md)
