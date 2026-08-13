# RulesForge Documentation

RulesForge 0.9.0 is an embedded RETE rule engine. Rules consume typed facts and
produce changes inside a session; they do not perform external I/O or invoke
arbitrary host functions.

## Start Here

| Need | Document |
|---|---|
| Build and run the bundled example | [C API example](../capi/examples/readme.md) |
| Integrate the runtime | [User guide](./USER_GUIDE.md) |
| Bind JSON, YAML, CSV, XML, or binary input | [Data ingestion](./DATA_INGESTION.md) |
| Process event-time streams | [Continuous engine](./CONTINUOUS_RULE_ENGINE_DESIGN.md) |
| Look up RFL syntax | [DSL reference](./dsl.md) |
| Prepare a production deployment | [Deployment guide](./DEPLOYMENT.md) |
| Browse sample rule packs | [Examples](./examples/README.md) |

## Public Contract

The installed C contract is [`include/rules_forge.h`](../include/rules_forge.h).
It defines ownership, thread affinity, status codes, DataBind input functions,
and continuous-session functions. The C++ headers under `rulesforge/include`
are intended for applications that need direct engine integration.

Documents describe the current implementation, not a compatibility promise
beyond the public headers and exported library ABI.
