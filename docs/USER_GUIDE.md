# RulesForge User Guide

RulesForge embeds compiled RFL rules in a C or C++ application. The host owns
input, scheduling, persistence, and external side effects. The engine owns rule
matching, session facts, agenda execution, queries, and event-time state.

## Runtime Objects

`KnowledgeBase` contains compiled rules and imported type declarations. Load
and configure it before creating sessions. A configured knowledge base may be
shared by multiple sessions without further mutation.

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

## Insert External Data

External JSON, CSV, XML, and binary values must be described by a DataBind
`.schema` file imported by the RFL rule pack. There are two equally supported
input styles:

- complete-document functions parse an already available payload;
- incremental stream functions accept chunks and commit only on `finish`.

JSONPath, CSVPath, and XMLPath select records before insertion in both input
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
[`rule_forge.h`](../include/rule_forge.h).

## Execution Modes

Select the mode on the knowledge base before creating sessions:

- `RULES_FORGE_EXECUTION_MODE_V1_STANDARD` preserves exact salience ordering;
- `RULES_FORGE_EXECUTION_MODE_V2_HIGH_PERFORMANCE` uses coarser priority buckets.

Do not depend on ordering between close salience values in V2. Measure both
modes with the actual rule pack before choosing one; throughput claims are not
a substitute for a local benchmark.

## Error Handling

Every status-returning C function must be checked. On failure,
`ruleforge_get_last_error_message()` returns thread-local diagnostic text until
the next RulesForge call on that thread.

Malformed payloads, unknown schema types, invalid entry points, decreasing
watermarks, resource-limit violations, and invalid handle state fail explicitly.
A failed DataBind stream cannot be reused; destroy it.

## Threading and Ownership

- Configure a knowledge base before sharing it.
- Use each stateful or continuous session from one thread at a time.
- Use a DataBind stream on the same thread as its session.
- Destroy all active streams before destroying their session.
- Destroy result handles and arrays with the matching RulesForge function.
- Never dereference an opaque C handle.

## Next Steps

- [C API example](../capi/examples/readme.md)
- [Data ingestion](./DATA_INGESTION.md)
- [Continuous engine](./CONTINUOUS_RULE_ENGINE_DESIGN.md)
- [DSL reference](./dsl.md)
- [Deployment](./DEPLOYMENT.md)
