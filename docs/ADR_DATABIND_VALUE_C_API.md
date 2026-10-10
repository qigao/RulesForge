# Borrowed DataBindValue Fact Insertion

## Status

Accepted; retained in RulesForge 0.9.0. Dependency and ownership notes reconciled
with the [2026-10-10 architecture baseline](./architecture/overview.md).

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

The original 0.7-to-0.8 change was additive. Consumers using the new symbol need
RulesForge 0.8 or later. The current build additionally requires DataBind 3.0.0
or newer with ABI 10 and a matching Salts/SaltsUtils dependency set; the original
source-compatibility decision is not a binary-compatibility promise across SDK
ABI migrations.

The source DataBind root remains caller-owned and must survive conversion.
Accessor children and canonical type identity are borrowed; their presence does
not transfer the root or grant permission to retain its storage. RulesForge
copies into its existing fact representation rather than exposing DataBind or
CMeta native layout as an internal `ConstraintValue` layout.

TurboScript Host value views are a separate ABI and are not accepted as
`DataBindValue` pointers. Any future host adapter must perform explicit,
schema-aware conversion and preserve exact numeric and extended-type semantics.

Rollback restores a matching consumer/library/SDK set. A consumer that calls
the added symbol cannot keep running against a DLL with that symbol removed.
Object or JSON paths are explicit caller choices, not automatic failure fallback.

## Verification

Build the C API target, then run the C API tests that cover schema import,
borrowed value insertion, caller-side value destruction, fact reads, and
session cleanup.
