# Continuous Rule Engine

This document describes the implemented continuous-session contract in
RulesForge 0.9.0. It is not a background service: the caller drives every event,
watermark, drain, acknowledgement, and input chunk.

## When to Use It

Use a continuous session when a rule result depends on one or more of:

- stable event IDs and duplicate rejection;
- named entry points;
- event-time ordering and watermarks;
- allowed lateness and future-time limits;
- time-window expiration without a new event;
- bounded retained events and pending outputs;
- deterministic recovery after a failed step.

For a finite request or batch, use `StatefulSession` instead.

## State and Ownership

One `ContinuousSession` owns one underlying `StatefulSession` plus coordination
state: current watermark, active-event metadata, deduplication entries,
committed replay operations, pending result batches, and metrics. The caller
must not mutate the underlying session independently.

The session is single-threaded. It owns no queue, broker client, clock thread,
or event loop.

## Configuration

For C, initialize `ruleforge_continuous_config_t` before overriding fields:

```c
ruleforge_continuous_config_t config;
if (ruleforge_continuous_config_init(&config) != RULES_FORGE_OK) {
  return 1;
}

const char *outputs[] = {"Alert"};
config.output_fact_types = outputs;
config.output_fact_type_count = 1;
config.allowed_lateness_ms = 5000;
config.event_retention_ms = 3600000;
config.dedup_retention_ms = 7200000;
```

Every growing structure is bounded. The default values are defined by
`ContinuousSessionConfig` and copied by `ruleforge_continuous_config_init`.
Applications should set limits from expected traffic and output volume rather
than treating defaults as capacity planning.

Important fields:

| Field | Purpose | Default |
|---|---|---:|
| `max_active_events` | retained event cap | `10000` |
| `max_dedup_entries` | remembered event-ID cap | `20000` |
| `max_pending_result_batches` | unacknowledged batch cap | `128` |
| `max_pending_results` | unacknowledged output-fact cap | `10000` |
| `max_input_batch_size` | maximum events in one batch | `1000` |
| `max_rules_per_step` | firing budget before drain is required | `10000` |
| `max_replay_steps` | bounded recovery history | `100000` |
| `allowed_lateness_ms` | accepted time behind the watermark | `0` |
| `event_retention_ms` | lifetime of active event facts | `3600000` |
| `dedup_retention_ms` | lifetime of event IDs | `3600000` |
| `max_event_time_lead_ms` | accepted time ahead of the watermark | `86400000` |
| `output_fact_types` | fact types copied into immutable results | empty |

## Event Commit

Each event has a non-empty ID, an imported fact type, an entry point, and an
event timestamp. `ruleforge_continuous_push_json` and
`ruleforge_continuous_push_yaml` bind one complete JSON or YAML object using the
KB's imported schema and commit one event step.

JSONPath, YPath, CSVPath, and XMLPath adapters bind multiple records. Event ID
and event time are read from caller-selected fields in each bound record; the
entry point is fixed for the batch. Complete documents and incremental streams
both commit the selected records through the same atomic batch operation.
Streams commit only when `finish` succeeds. See [Data ingestion](./DATA_INGESTION.md).

Before mutation, the runtime validates the event ID, route, timestamp, duplicate
state, lateness, future skew, and resource limits. A rejected event does not
advance accepted-event state.

## Event Time and Watermarks

The watermark is caller supplied and monotonic. Advancing it can expire active
events and trigger window changes even when no new event arrives.

An event is late when its timestamp is older than the configured watermark
boundary after `allowed_lateness_ms` is applied. An event too far ahead of the
watermark is rejected by `max_event_time_lead_ms`.

`event_retention_ms` controls active event facts. `dedup_retention_ms` controls
how long their IDs remain unavailable for reuse. These are separate because an
expired event may still need duplicate protection.

## Results, Drain, and Acknowledgement

A successful step returns an immutable result handle containing:

- `COMMITTED` or `DRAIN_REQUIRED`;
- a monotonically assigned batch ID;
- rules fired and events expired;
- the current watermark, when present;
- snapshots of configured output fact types.

If the per-step rule budget is exhausted while activations remain, the result is
`DRAIN_REQUIRED`. Call `ruleforge_continuous_drain` until a committed result is
returned before pushing more work.

Result handles and acknowledgement are different operations. Destroying a
result releases the caller's snapshot handle; acknowledging its batch ID
releases the session's pending-output accounting. Acknowledge batches in order.

```c
ruleforge_continuous_result_t result = NULL;
ruleforge_status_t status = ruleforge_continuous_push_json(
    session, "Event", "event-42", "events", 1710000000000,
    json, &result);

while (status == RULES_FORGE_OK
       && ruleforge_continuous_result_get_status(result)
              == RULES_FORGE_CONTINUOUS_DRAIN_REQUIRED) {
  uint64_t batch = ruleforge_continuous_result_get_batch_id(result);
  ruleforge_continuous_acknowledge(session, batch);
  ruleforge_continuous_result_destroy(result);
  result = NULL;
  status = ruleforge_continuous_drain(session, &result);
}

if (status == RULES_FORGE_OK) {
  ruleforge_continuous_acknowledge(
      session, ruleforge_continuous_result_get_batch_id(result));
  ruleforge_continuous_result_destroy(result);
}
```

Production code must check every status, including acknowledgement and destroy.

## Failure and Recovery

Continuous rule execution is fail-fast. If a step fails after mutation begins,
the runtime rebuilds the last committed state by deterministic replay. A
successful recovery increments `replay_recoveries`; the failed event is not
committed. If recovery cannot restore a valid state, the session becomes
inconsistent and rejects further work.

Replay is in-memory recovery, not durable checkpointing. After process failure,
the host must recreate the session and replay its authoritative event log.

## Metrics

`ruleforge_continuous_get_metrics` exposes accepted and expired events,
duplicate/late/resource-limit rejections, replay recoveries, and current sizes
for active events, dedup entries, pending batches, and pending results.

Metrics are observations, not a substitute for checking operation status.

## Current Limits

- Continuous input supports one explicit JSON/YAML event or a path-selected
  JSON/YAML/CSV/XML event batch.
- DataBind emits streamable JSON/YAML/CSV/XML records through synchronous bound-value
  callbacks during `feed`; RulesForge retains them and commits the batch at
  `finish` to preserve atomic message semantics.
- Checkpoint/restore and rule-pack hot migration are not implemented.
- Broker consumption, retries, persistence, and outbox delivery belong to the
  host application.
- Rules cannot perform external I/O. Publish result snapshots after commit and
  make downstream delivery idempotent using the result batch ID.
