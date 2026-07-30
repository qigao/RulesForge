# Borrowed DataBindValue Fact Insertion

## Status

Accepted.

## Background

DataBind-based callers can already own a parsed, schema-bound
`DataBindValue`. Converting that value to JSON and asking RulesForge to parse it
again adds allocation, serialization, and parsing while weakening the ownership
contract between the two libraries.

## Decision

RulesForge 0.8 adds
`ruleforge_session_add_data_bind_value(session, fact_type, value, out_fact)`.

- `value` is borrowed and must be an object.
- `fact_type` must already be imported by the knowledge base.
- The session copies all fields before returning.
- `out_fact` is optional and, when returned, remains session-owned.
- Validation, session-consistency, and insertion failures use existing status
  codes and `ruleforge_get_last_error_message()`.

The existing `DataBindObject` and JSON insertion APIs remain unchanged.

## Consequences

- DataBind codecs can hand parsed values to RulesForge without a format
  round-trip.
- Fact allocation remains owned by the stateful session.
- The public header now exposes the existing public DataBind dependency.
- The additive API changes the package minor version from 0.7 to 0.8.

## Compatibility And Rollback

Existing source and binary callers do not change. Consumers using the new
symbol require the RulesForge 0.8 library. Rollback consists of removing the
new symbol and keeping the existing object or JSON insertion paths.

## Verification

Build the C API target, then run the C API tests that cover schema import,
borrowed value insertion, caller-side value destruction, fact reads, and
session cleanup.
