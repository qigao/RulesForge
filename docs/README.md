# RulesForge Documentation

RulesForge 0.9.0 is an embedded RETE rule engine. Rules consume typed facts and
produce changes inside a session; they do not perform external I/O or invoke
arbitrary host functions.

## Start Here

| Need | Document |
|---|---|
| Understand architecture, ownership, and current Salts/TurboScript boundaries | [Architecture overview](./architecture/overview.md) |
| Build and run the bundled example | [C API example](../capi/examples/readme.md) |
| Integrate the runtime | [User guide](./USER_GUIDE.md) |
| Bind JSON, YAML, CSV, XML, or binary input | [Data ingestion](./DATA_INGESTION.md) |
| Understand borrowed DataBind value insertion | [DataBind C API decision](./ADR_DATABIND_VALUE_C_API.md) |
| Process event-time streams | [Continuous engine](./CONTINUOUS_RULE_ENGINE_DESIGN.md) |
| Review proposed execution limits and Salts/TurboScript integration boundaries | [Bounded execution design](./architecture/bounded-execution.md) |
| Look up RFL syntax | [DSL reference](./dsl.md) |
| Prepare a production deployment | [Deployment guide](./DEPLOYMENT.md) |
| Browse sample rule packs | [Examples](./examples/README.md) |

## Public Contract

The installed C contract is [`include/rules_forge.h`](../include/rules_forge.h).
It defines ownership, thread affinity, status codes, DataBind input functions,
and continuous-session functions. C and C++ applications both integrate through
this C ABI and the installed `RulesForge` shared library. The C++ headers under
`rulesforge/include` are internal implementation interfaces and are not
installed.

User-facing guides describe the current implementation, not a compatibility
promise beyond the public headers and exported library ABI. Architecture designs
explicitly marked as proposed distinguish upstream capabilities from work that
RulesForge has not implemented.
