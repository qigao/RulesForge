# RulesForge Documentation

RulesForge 0.1.0 is an embedded RETE rule engine. Rules consume typed facts and
produce changes inside a session. Programs additionally bind Toad RHS
computations; external I/O remains owned by the host.

## Start Here

| Need | Document |
|---|---|
| Develop a client application against the RulesForge DLL | [RulesForge + Toad development guide](DEVELOPMENT_GUIDE.md) |
| Build and run the bundled example | [C API example](../examples/README.md) |
| Integrate the runtime | [User guide](./USER_GUIDE.md) |
| Bind JSON, YAML, CSV, XML, or binary input | [Data ingestion](./DATA_INGESTION.md) |
| Process event-time streams | [Continuous engine](./CONTINUOUS_RULE_ENGINE_DESIGN.md) |
| Look up RFL syntax | [DSL reference](./dsl.md) |
| Prepare a production deployment | [Deployment guide](./DEPLOYMENT.md) |
| Browse sample rule packs | [Examples](../examples/README.md) |
| Choose and run seven domain scenarios | [Scenario guide](SCENARIOS.md) |
| Understand mock persistence, replay and delivery | [Scenario host contract](SCENARIOS.md#ownership-and-mock-apis) |
| Understand candidate optimization and solver boundaries | [Optimization and solvers](SCENARIOS.md#optimization-and-solvers) |
| Bind Toad computations and reload complete generations | [Script Programs](./SCRIPT_PROGRAM.md) |
| Run a complex RulesForge + Toad scenario | [Windowed maintenance](../examples/maintenance/README.md) |
| Review Toad RHS and ACE ownership decisions | [Toad RHS design](./architecture/toad-rhs-integration.md) |
| Review core ownership, parsing and dispatch decisions | [Core ownership and dispatch](./architecture/core-ownership-and-dispatch.md) |
| Reproduce benchmarks and interpret the measured baseline | [Benchmark snapshot](./architecture/core-ownership-and-dispatch.md#benchmark-snapshot) |

## Public Contract

The installed C contract is [`rulesforge/include/rules_forge.h`](../rulesforge/include/rules_forge.h).
It defines ownership, thread affinity, status codes, DataBind input functions,
and continuous-session functions. C and C++ applications both integrate through
this C ABI and the installed `RulesForge` shared library. The C++ headers under
`core/include` are internal implementation interfaces and are not
installed.

Documents describe the current implementation, not a compatibility promise
beyond the public headers and exported library ABI.
