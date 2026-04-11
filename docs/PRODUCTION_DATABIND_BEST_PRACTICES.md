# Production DataBind Best Practices

This note consolidates the production rules for using `data_bind` inside RulesForge.

## Core Model

Treat these objects as having different lifetimes:

- `RuleSetKey`: identifies one deployed ruleset, for example `tenant/domain/version`
- `KnowledgeBase`: compiled rules and declarations for one ruleset
- `CodecRegistry`: owned by the `KnowledgeBase`; owns shared `data_bind` codecs
- `StatefulSession`: per request, per workflow, or per connection working memory

The important rule is simple:

- build `KnowledgeBase` once per ruleset
- create `data_bind` codecs once per `KnowledgeBase`
- create `StatefulSession` per request or per unit of work

## Recommended Runtime Shape

Use this shape in production:

```text
RuleSetKey -> KnowledgeBase -> CodecRegistry -> shared binary/json codecs
Request    -> StatefulSession
Message    -> Fact
```

Do not do this:

```text
Request -> parse rules -> build KnowledgeBase -> create codec -> JIT -> parse one message -> destroy all
```

That design throws away the whole point of the compiled path.

## Binary And JSON

`data_bind` uses the schema as the common source of truth, but the execution paths differ:

- binary: schema -> MIR IR -> JIT parser -> bind to `Fact`
- json: schema -> JSON walk -> bind to `Fact`

Only binary pays the MIR/JIT cold-start cost.

## JIT Must Run Once

For binary codecs, JIT creation is an initialization cost, not a per-message cost.

Best practice:

- create the binary codec once
- keep it alive as long as the `KnowledgeBase` stays alive
- reuse it for all messages of that ruleset

In RulesForge this is handled by `CodecRegistry`, which caches:

- one shared binary codec
- one shared json codec

Do not recreate codecs per message.

## Session Lifecycle

`KnowledgeBase` is immutable and thread-safe after construction.

`StatefulSession` is not thread-safe.

Use this pattern:

1. Load or build the `KnowledgeBase` for a ruleset.
2. Create one `StatefulSession` for a request or stream context.
3. Add facts from JSON or binary.
4. Call `fire_all_rules()`.
5. Destroy the session when done.

## Thread Safety

Use this rule:

- share `KnowledgeBase`
- do not share `StatefulSession`

That means:

- one compiled `KnowledgeBase` may be used by many threads
- each thread, request, or stream context must create its own `StatefulSession`
- per-request working memory must stay inside that session

Recommended shape:

```text
many threads -> one shared KnowledgeBase
many threads -> many independent StatefulSession instances
```

Do not let multiple threads mutate the same session.

## Process Safety

Process safety is different from thread safety.

Correct rule:

- share rule text, schema, and deployment configuration across processes
- do not share in-memory `KnowledgeBase`, `CodecRegistry`, or `DataBind*` instances across processes

Each process should build and own its own runtime objects:

```text
process A -> its own KnowledgeBase -> its own codecs
process B -> its own KnowledgeBase -> its own codecs
```

Do not try to reuse object addresses or JIT-compiled codec instances across process boundaries.

If you run a multi-process server, each worker process should warm up its own ruleset cache.

## Multiple Rules And Multiple Object Types

Multiple rules do not require multiple codecs.

Multiple declared object types do not require multiple binary JIT runs.

One `KnowledgeBase` can contain:

- many rules
- many declared types
- one shared binary codec that knows all declared types
- one shared json codec that knows all declared types

The cost is paid once per ruleset, not once per type per message.

## Ruleset Cache

For a server that hosts different rulesets, cache `KnowledgeBase` instances by a stable key:

```text
(tenant, domain, version) -> shared_ptr<KnowledgeBase>
```

Typical examples:

- `tenantA/fraud/v3`
- `tenantA/pricing/v1`
- `tenantB/routing/v2`

This gives:

- clean isolation between unrelated rulesets
- one codec cache per ruleset
- one binary JIT cost per ruleset
- safe hot reload by version

## Hot Reload

Never mutate a live `KnowledgeBase` in place.

Use this flow:

1. Build a new `KnowledgeBase` from the new rule text.
2. If build fails, keep the old one.
3. If build succeeds, atomically replace the cached pointer.
4. Let old sessions finish on the old `KnowledgeBase`.

This preserves user-visible behavior and avoids partial updates.

## Schema And Enum Handling

Declarations and enums are one logical unit.

Best practice:

- always pass declarations and enums together
- never build a codec from declarations while dropping enums

If enums are lost between parsing and codec creation, enum-backed fields become incorrect or fail unexpectedly.

## Supported Type Discipline

Do not assume all schema shapes are supported just because the DSL can express them.

Current practical rule:

- use only schema types that the `DataBindValueApi` can represent
- reject unsupported shapes early during codec creation

Examples of shapes that need explicit API support:

- `Set<long>`
- `Map<String, long>`

If the API has no callback for a type family, do not paper over it at runtime.

## Error Handling

Prefer early failure:

- codec creation should fail if the API cannot support the schema
- parsing should fail if payload data does not match the schema

Do not defer obvious incompatibilities until the first live message.

## What To Measure

Measure cold path and hot path separately.

Cold path:

- schema parsing
- codec creation
- MIR generation
- JIT linking
- first parse

Hot path:

- parse throughput
- bind throughput
- session rule execution

Never mix these into one number and call it “performance”.

## Anti-Patterns

Avoid these patterns:

- building a `KnowledgeBase` per request
- creating a `data_bind` codec per message
- rebuilding binary JIT for each object type or each payload
- mixing unrelated rulesets into one giant `KnowledgeBase`
- dropping enums when creating codecs from declarations
- keeping dead `data_bind` integration code that is never used in the real parse path

## Practical Summary

If you remember only five rules, remember these:

1. One ruleset, one long-lived `KnowledgeBase`.
2. One `KnowledgeBase`, one shared binary codec and one shared json codec.
3. Binary JIT runs once per codec lifetime.
4. One request, one `StatefulSession`.
5. Facts go in; rules fire; sessions die. Rulesets do not.
