# RulesForge

**Embedded RETE rule execution for the Salts ecosystem.**

RulesForge is an embedded rule engine for C and C++ applications. It compiles RFL rule packs, consumes typed facts, evaluates RETE matches, manages session/agenda state, and exposes stateful and continuous execution through a stable C ABI.

RulesForge is deliberately not an application server or hidden workflow runtime. The host owns external I/O, scheduling, persistence, and side effects; RulesForge owns rule matching, session facts, agenda execution, queries, event-time state, and bounded continuous-rule coordination.

**Tags:** C · C++ · RETE · rule-engine · inference · event-time · streaming · DataBind · CMeta · embedded-systems

## Built on Salts

RulesForge participates in the [Salts](https://github.com/qigao/salts) ecosystem rather than defining an independent type and data-binding stack.

Its current build uses:

- **Salts** for shared systems/runtime foundations and C-facing semantic infrastructure.
- **SaltsUtils** for DataBind, concrete parser components, and utilities. Its DataBind component provides schema-defined external facts, native/dynamic values, parsing, and incremental ingestion.

The target ecosystem boundary is:

```text
Salts
  ├── salts-utils (including DataBind schema / compiler / binding)
  └── salts-net
        ↓
    RulesForge
```

DataBind is part of SaltsUtils. Its source, build, installation, and exported targets belong to SaltsUtils.

## What RulesForge owns

RulesForge owns:

- RFL parsing and compiled rule representation;
- RETE matching and agenda execution;
- knowledge bases;
- stateful sessions;
- continuous sessions;
- rule queries;
- event-time and watermark state;
- bounded deduplication, retention, result batching, and replay recovery;
- conversion from bound DataBind values into session-owned facts.

RulesForge does **not** own:

- network transports;
- brokers;
- background event loops;
- arbitrary host-function execution;
- persistence policy;
- external service calls;
- application-side side effects.

That separation keeps the rule engine deterministic and embeddable.

## Public contract

The installed public C API is:

```text
include/rules_forge.h
```

C and C++ applications integrate through this C ABI and the installed RulesForge library. The C++ implementation headers under `rulesforge/include` are internal and are not the public compatibility boundary.

## Execution models

### Stateful session

Use a stateful session for finite request/batch evaluation where the host explicitly decides when to fire rules:

```text
create session
  -> insert facts
  -> fire rules
  -> query results
  -> destroy session
```

### Continuous session

Use a continuous session when correctness depends on:

- stable event IDs;
- duplicate rejection;
- named entry points;
- event time and watermarks;
- allowed lateness;
- bounded retained events;
- bounded pending outputs;
- deterministic recovery after a failed step.

```text
create
  -> push event
  -> inspect result
  -> acknowledge
  -> advance watermark
```

A continuous session is caller-driven. It does not create its own thread, queue, broker client, clock thread, or event loop.

## Typed data ingestion

External JSON, YAML, CSV, XML, and binary values are described by DataBind schema declarations imported by the RFL rule pack.

```text
payload / incremental chunks
  -> structural selection
     JSONPath / YPath / CSVPath / XMLPath
  -> DataBind schema validation and binding
  -> session-owned typed facts
  -> RFL constraints / rules / queries
```

RulesForge treats imported schemas as the authoritative external fact contract. Session insertion APIs do not reload schemas for each fact.

Complete-document and incremental-stream APIs are both supported. Incremental streams are synchronous and designed to be driven from the host's async callbacks; RulesForge itself does not own the async runtime.

See [Data ingestion](docs/DATA_INGESTION.md) for the full API matrix and ownership rules.

## DataBind compatibility boundary

The current build requires:

```text
DataBind >= 3.0.0
DataBind ABI == 9
```

Configuration fails if the required version/ABI contract is not available.

This is intentional: RulesForge depends on the DataBind public boundary rather than silently adapting to incompatible schema/value behavior.

## Example lifecycle

```c
ruleforge_knowledge_base_t kb = NULL;
if (ruleforge_kb_create(&kb) != RULES_FORGE_OK) {
    return 1;
}

const char *search_dirs[] = {"rules"};
if (ruleforge_kb_load_drl_file(
        kb, "rules/main.rfl", search_dirs, 1) != RULES_FORGE_OK) {
    ruleforge_kb_destroy(kb);
    return 1;
}
```

Create a session, insert schema-bound facts, fire with an explicit budget, query results, and destroy every owned handle through its matching API.

See [User guide](docs/USER_GUIDE.md) for the complete integration flow.

## Build

Requirements include:

- CMake 3.20+
- C17 compiler support for the C boundary
- C++20 compiler support for the engine implementation
- an installed Salts SDK
- an installed SaltsUtils SDK with DataBind 3 matching ABI 9 and the required parser/utility components
- OpenSSL through the configured dependency environment

The dependency ownership contract is Salts from `SALTS_ROOT` and SaltsUtils, including DataBind, from `SALTS_UTILS_ROOT`.

## Design principles

- **Host-controlled side effects.** Rules match and mutate facts; external I/O remains outside the engine.
- **Typed external facts.** Schema/DataBind contracts define external data before it enters working memory.
- **Bounded continuous state.** Event retention, deduplication, result queues, replay steps, and firing budgets are explicit.
- **Explicit acknowledgement.** Releasing a result handle is not the same as acknowledging its batch.
- **Deterministic recovery.** Failed continuous steps recover from the last committed state through bounded replay.
- **No hidden runtime.** The host drives scheduling, I/O, event delivery, persistence, and lifecycle.
- **Fail-fast compatibility.** Incompatible DataBind ABI/version combinations fail during configuration.

## Documentation

| Need | Document |
| --- | --- |
| Integrate the runtime | [User guide](docs/USER_GUIDE.md) |
| Bind JSON/YAML/CSV/XML/binary input | [Data ingestion](docs/DATA_INGESTION.md) |
| Continuous/event-time processing | [Continuous engine](docs/CONTINUOUS_RULE_ENGINE_DESIGN.md) |
| RFL syntax | [DSL reference](docs/dsl.md) |
| Production deployment | [Deployment guide](docs/DEPLOYMENT.md) |
| Example rule packs | [Examples](docs/examples/README.md) |
| C API example | [C API example](capi/examples/readme.md) |

---

**Salts provides the systems foundation. SaltsUtils DataBind provides typed external data. RulesForge provides deterministic rule execution.**
