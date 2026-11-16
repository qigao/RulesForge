# RulesForge User Guide

RulesForge embeds compiled RFL rules in a C or C++ application. The host owns
input, scheduling, persistence, and external side effects. The engine owns rule
matching, session facts, agenda execution, queries, and event-time state.

For an application walkthrough using the RulesForge DLL, a complete client,
business schema, RFL, Toad modules and native extensions, start with the
[development guide](DEVELOPMENT_GUIDE.md). This document describes runtime usage
and ownership at the public C boundary.

## Runtime Objects

`Program` binds trusted Toad scripts to RHS CALL commands and publishes complete
ACE generations. Program sessions stay on their creating thread and retain
their original generation across reload. See [Script Programs](SCRIPT_PROGRAM.md)
for deployment, typed values, error handling and close/drain ownership.

`KnowledgeBase` contains compiled rules and imported type declarations. Load
and configure it before creating sessions. A configured knowledge base may be
shared by multiple sessions without further mutation.

Core scalar expressions compile to MIR native programs while rules are loaded.
RETE matching, aggregate expressions and RHS calculations use these programs;
they do not reparse scalar expression strings per fact. Typed Toad CALL exports
remain a separate computation boundary. A Program bundle's `mode` selects Toad
interpreter or JIT execution and does not select the core scalar backend.

`StatefulSession` contains mutable facts and agenda state. Use it for request,
batch, or explicitly managed stateful evaluation. It is not thread-safe.

`ContinuousSession` adds event IDs, entry points, event time, watermarks,
retention, output batches, and bounded replay recovery. It is explicitly driven
by the caller and does not create a thread or event loop.

## Supported Integration Boundary

RulesForge is a filtering and inference library:

- the host loads RFL and external data;
- rules match facts and mutate session-owned facts;
- queries and configured continuous outputs return results to the host;
- the host decides whether to call services, write files, or publish messages.

RFL cannot load dynamic function tables or invoke arbitrary external calls.
Rule conditions use the built-in expression and typed predicate set only.
External service calls and other side effects remain the host's responsibility.

## Load Rules

The C API accepts in-memory RFL, one file, multiple files, and CSV decision
tables:

```c
ruleforge_knowledge_base_t kb = NULL;
if (ruleforge_kb_create(&kb) != RULES_FORGE_OK) {
  return 1;
}

const char *search_dirs[] = {"rules"};
if (ruleforge_kb_load_drl_file(kb, "rules/main.rfl", search_dirs, 1)
    != RULES_FORGE_OK) {
  fprintf(stderr, "%s\n", ruleforge_get_last_error_message());
  ruleforge_kb_destroy(kb);
  return 1;
}
```

Schema imports are resolved relative to the importing RFL file and then through
the supplied base directories.

## Choose a Session

Use `StatefulSession` when input is a request or finite batch and the caller
controls when to fire rules:

```text
create session -> insert facts -> fire -> query -> destroy session
```

Use `ContinuousSession` when correctness depends on event identity, event time,
watermarks, bounded retention, or acknowledgement:

```text
create -> push event -> inspect result -> acknowledge -> advance watermark
```

Do not emulate continuous processing by keeping an unbounded stateful session.
That omits duplicate detection, lateness checks, retention limits, result
backpressure, and recovery semantics.

For concrete application patterns, see the [seven runnable scenarios](SCENARIOS.md).
They use stateful Programs with typed Toad CALL results. Their explicit
interpreter/JIT bundle mode selects script execution. Their mock APIs demonstrate
input and output ownership; production transports and durable storage belong to
the embedding application. The cold-chain scenario is an arrival-ordered length
window, not a continuous event-time or lateness example.

## Insert External Data

External JSON, YAML, CSV, XML, and binary values must be described by a DataBind
`.schema` file imported by the RFL rule pack. There are two equally supported
input styles:

- complete-document functions parse an already available payload;
- incremental stream functions accept chunks and commit only on `finish`.

The Knowledge Base owns and reuses imported DataBind codecs. Use
`ruleforge_session_add_fact_json` (or its YAML, CSV, XML, binary, and path
variants) when inserting by imported type; callers do not pass a schema path to
session insertion APIs. If the host already owns a parsed DataBind object or
value, `ruleforge_session_add_data_bind_object` and
`ruleforge_session_add_data_bind_value` copy it into the session.

JSONPath, YPath, CSVPath, and XMLPath select records before insertion in both input
styles. RFL rules and queries then apply business constraints to the selected
facts. Path filtering and rule filtering are consecutive stages, not competing
APIs.

The stream API is synchronous. It is designed to be called from async I/O
callbacks; it does not own an async runtime. See
[Data ingestion](./DATA_INGESTION.md) for the API matrix and ownership rules.

## Fire and Query

After inserting facts into a stateful session, fire rules with a finite budget
unless the rule pack is known to terminate:

```c
int fired = 0;
if (ruleforge_session_fire_all_rules(session, 10000, &fired)
    != RULES_FORGE_OK) {
  fprintf(stderr, "%s\n", ruleforge_get_last_error_message());
}
```

Named RFL queries return a result handle. Facts borrowed from that result remain
valid only while their owner remains alive; follow the ownership comments in
[`rules_forge.h`](../rulesforge/include/rules_forge.h).

## Error Handling

Every status-returning C function must be checked. On failure,
`ruleforge_get_last_error_message()` returns thread-local diagnostic text until
the next RulesForge call on that thread.

Malformed payloads, unknown schema types, invalid entry points, decreasing
watermarks, resource-limit violations, and invalid handle state fail explicitly.
A failed DataBind stream cannot be reused; destroy it.

Unsupported scalar syntax/functions and expression limits fail loading. During
execution, signed integer overflow, division by zero, non-finite arithmetic and
present nonnumeric arithmetic operands fail explicitly; a missing LHS field
retains its non-match semantics.

For stateful sessions, RHS failure rolls back the current rule transaction;
previously committed rules remain committed. A successful rollback leaves the
session usable. Failed rollback or an error after nontransactional network
mutation begins marks the session inconsistent. Further firing, querying or fact
mutation is rejected.
Check `ruleforge_session_reset` before using an explicitly reset session, or
destroy it and rebuild from authoritative input. Reset clears facts and derived
state; it does not recover the prior application state. Capture the original
diagnostic before reset or cleanup changes the thread-local last error.

## Threading and Ownership

- Configure a knowledge base before sharing it.
- Use each stateful or continuous session from one thread at a time.
- Use a DataBind stream on the same thread as its session.
- Destroy all active streams before destroying their session.
- Destroy result handles and arrays with the matching RulesForge function.
- Never dereference an opaque C handle.

## Next Steps

- [RulesForge + Toad DLL client development](DEVELOPMENT_GUIDE.md)
- [Runnable scenarios, mock APIs and optimization](SCENARIOS.md)
- [C API example](../examples/README.md)
- [Data ingestion](./DATA_INGESTION.md)
- [Continuous engine](./CONTINUOUS_RULE_ENGINE_DESIGN.md)
- [DSL reference](./dsl.md)
- [Deployment](./DEPLOYMENT.md)
- [Benchmark protocol and snapshot](./architecture/core-ownership-and-dispatch.md#benchmark-snapshot)
