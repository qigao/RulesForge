# RulesForge

**Embedded RETE rule execution for the Salts ecosystem.**

RulesForge is an embedded rule engine for C and C++ applications. It compiles RFL rule packs, consumes typed facts, evaluates RETE matches, manages session/agenda state, and exposes stateful and continuous execution through a stable C ABI.

Downstream applications consume the RulesForge shared library through
`RulesForge::RulesForge` and the public C header. Start with the
[RulesForge + Toad development guide](docs/DEVELOPMENT_GUIDE.md) to build a DLL
client and deploy your own schema, rules, script modules and native computations.

RulesForge is deliberately not an application server or hidden workflow runtime. The host owns external I/O, scheduling, persistence, and side effects; RulesForge owns rule matching, session facts, agenda execution, queries, event-time state, and bounded continuous-rule coordination.

**Tags:** C · C++ · RETE · rule-engine · inference · event-time · streaming · DataBind · CMeta · embedded-systems

## Built on Salts

RulesForge participates in the [Salts](https://github.com/qigao/salts) ecosystem rather than defining an independent type and data-binding stack.

The build selects exact stable releases in the parent environment or local `.env`:
[SaltsBase 0.1.8](https://github.com/qigao/salts-base/releases/tag/v0.1.8),
[CMeta 0.1.7](https://github.com/qigao/cmeta/releases/tag/v0.1.7),
[SaltsUtils 0.1.4](https://github.com/qigao/salts-utils/releases/tag/v0.1.4),
[CNet 0.1.12](https://github.com/qigao/salts-cnet/releases/tag/v0.1.12), and
[Toad 0.1.3](https://github.com/qigao/TurboScript/releases/tag/v0.1.3), plus
[vcpkg-cache 0.1.3](https://github.com/qigao/vcpkg-cache/releases/tag/v0.1.3).

Base owns system services, logging, memory and UUIDs. CMeta owns reflection and
CSTL. Utils owns DataBind and concrete parsers; CNet owns networking/crypto.
Toad executes explicitly bound RHS computations, with ACE managing complete
Program generations. See the [script Program guide](docs/SCRIPT_PROGRAM.md).
These are independently published SDKs. The old aggregated `Salts.Native` SDK
and floating NuGet versions are no longer build inputs. No upstream SDK is
built from source during restoration.

## Upstream dependency versions

The existing restore entry point verifies release asset sizes and SHA-256.
Base and CMeta are also checked against their release manifests and successful
producer CI, including exact Base/cache constraints and the selected triplet.
Shared vcpkg-cache `resolve-release` produces a build-directory URL/hash lock;
`locked` restores it. The returned receipt is the installation-root fact source.
Lemon and re2c use the versioned host-tool packages from the same release.

`.env.example` lists exact selectors. Optional existing SDK/cache/tool roots may
be shared with another project when version, compiler, profile and receipts
match. Existing installations are never overwritten or used after a receipt
mismatch. Without explicit roots, restoration creates versioned directories
under ignored `build/dependencies`. Keep SDKs and cache from one compatible
compiler/triplet composition. CMake neither reads `.env` nor downloads packages.

Splitting dependency owners preserves the public C API and DataBind ABI 10.
UUID values in the internal C++ variant now own a 16-byte `std::array` and use
Base's parse/format/equality API; copying, hashing and lexicographic ordering
retain their value semantics. The removed CMeta UUID type has no compatibility
shim. Reverting this migration requires the earlier source checkout and its
matching published SDK environment, followed by a fresh configure; the build
contains no automatic old-SDK fallback.

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
rulesforge/include/rules_forge.h
```

C and C++ applications integrate through this C ABI and the installed RulesForge library. The C++ implementation headers under `core/include` are internal and are not the public compatibility boundary.

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

## Runnable application scenarios

The [scenario guide](docs/SCENARIOS.md) covers production quality, energy dispatch,
cold-chain screening, production routing, release admission, reconciliation and
service entitlements. Each runs through the public C ABI with schema-bound mock
input, Toad computations, decision persistence and idempotent mock delivery.
The examples provide explicit interpreter/JIT bundles and formal tests.

These scenarios use stateful Programs. Their mock persistence is in memory;
network services and durable databases remain host integrations. Energy dispatch
finds a minimum over 27 discrete plans; the guide explains when a dedicated
optimization solver would be appropriate.

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
DataBind ABI == 10
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

Requirements: CMake 3.27+, Ninja, Python 3.12+, GitHub CLI and a C17/C++20
compiler matching the selected SDK profile. `mir-jit` (MIR::MIR) comes from the
shared preinstalled vcpkg-cache tree; the payments CLI uses `Salts::CmdParser`
from the released Base SDK. Only `BUILD_TESTS` and `BUILD_BENCHMARK`
control tests and benchmarks; examples and modules assemble unconditionally.

For Windows LLVM-MinGW, in one PowerShell session:

```powershell
$env:GITHUB_TOKEN = gh auth token
./cmake/ci/restore-native-sdks.ps1 -Rid windows-x64 -Profile windows-llvm-mingw-x64 -Local
cmake --fresh --preset native-release-user
cmake --build --preset native-release-user
ctest --preset native-release-user --output-on-failure --no-tests=error
cmake --build --preset install-native-release-user
```

Run each command only after its predecessor succeeds. The wrapper loads only
non-secret build fields from `.env`, and exports qualified roots/runtime paths
after every dependency succeeds. The preset selects the complete formal test
graph. Module tests remain with their owner under `core/parser/tests`,
`core/test` and `rulesforge/tests`; verification uses those targets and CTest.

CI uses the same exact restore entry and existing presets for Windows
LLVM-MinGW x64, Linux Clang x64, macOS Clang arm64 and Android arm64. Native
profiles execute the complete CTest graph. Android cross-build/install and
16 KiB ELF alignment checks do not establish device runtime behavior.
`release-sdk.yml` delegates to the same workflow for manual qualification;
no independent installed-consumer validation project is used.

### Benchmarks

Use the existing benchmark switch with the same restored SDK environment:

```powershell
cmake --preset llvm-mingw-x64-release-user -DBUILD_TESTS=ON -DBUILD_BENCHMARK=ON
cmake --build --preset llvm-mingw-x64-release-user --parallel 3
ctest --preset llvm-mingw-x64-release-user -L '^benchmark$' -j 1 -V --no-tests=error
```

Run the seven registered benchmark suites serially. The
[measurement protocol and dated results](docs/architecture/core-ownership-and-dispatch.md#benchmark-snapshot)
explain sample counts, setup costs and the difference between MIR warm calls
and a complete program lifecycle. The current measurements establish a
baseline; they do not establish a speedup against an older revision.

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
| Develop a DLL client with RulesForge + Toad | [Development guide](docs/DEVELOPMENT_GUIDE.md) |
| Integrate the runtime | [User guide](docs/USER_GUIDE.md) |
| Bind JSON/YAML/CSV/XML/binary input | [Data ingestion](docs/DATA_INGESTION.md) |
| Continuous/event-time processing | [Continuous engine](docs/CONTINUOUS_RULE_ENGINE_DESIGN.md) |
| RFL syntax | [DSL reference](docs/dsl.md) |
| Production deployment | [Deployment guide](docs/DEPLOYMENT.md) |
| Run and interpret benchmarks | [Benchmark snapshot](docs/architecture/core-ownership-and-dispatch.md#benchmark-snapshot) |
| Example rule packs | [Examples](examples/README.md) |
| Run seven domain scenarios with mock APIs | [Scenario guide](docs/SCENARIOS.md) |
| C API example | [C API example](examples/README.md) |

---

**Salts provides the systems foundation. SaltsUtils DataBind provides typed external data. RulesForge provides deterministic rule execution.**
