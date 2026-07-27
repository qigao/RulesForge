# Data Ingestion

RulesForge uses TurboUtils DataBind at the C API boundary. DataBind validates
external values against `.schema` declarations and RulesForge converts the
bound values into session-owned facts. The rule engine itself remains fact-only.
Imported DataBind codecs are retained by the immutable Knowledge Base and reused
by every session created from it; inserting a fact does not reload its schema.

Input processing has two distinct filtering stages:

```text
complete document or chunked stream
  -> JSONPath / CSVPath / XMLPath structural selection
  -> schema binding and candidate fact insertion
  -> RFL constraints, rules, and queries
```

Path selection reduces which records enter the session. RFL then performs
business filtering over those candidate facts. A fact that does not match a
rule is still present in working memory until the host or a rule retracts it.

## Type Contract

An external fact type must be imported by the loaded RFL:

```rfl
import schema "orders.schema"

rule "Large order"
when
    $order: Order(total > 1000)
then
    // mutate or insert session facts
end
```

Use RFL `declare` for facts created inside the session. A `declare` or `enum`
cannot duplicate the short name of an imported schema type.

## Complete Documents

Use these functions when the complete payload is already in memory:

| Format | C API | Result |
|---|---|---|
| JSON object | `ruleforge_session_add_fact_json` | one fact |
| first JSONPath match | `ruleforge_session_add_fact_json_path` | one fact |
| all JSONPath matches | `ruleforge_session_add_facts_json_path` | zero or more facts |
| YAML object | `ruleforge_session_add_fact_yaml` | one fact |
| first YPath match | `ruleforge_session_add_fact_yaml_path` | one fact |
| all YPath matches | `ruleforge_session_add_facts_yaml_path` | zero or more facts |
| DataBind binary | `ruleforge_session_add_fact_binary` | one fact |
| CSV document | `ruleforge_session_add_facts_csv` | zero or more facts |
| CSVPath matches | `ruleforge_session_add_facts_csv_path` | zero or more facts |
| XML document/path | `ruleforge_session_add_facts_xml` | zero or more facts |

Multi-fact functions return an array only when requested. Release that array
with `ruleforge_fact_array_free`; the inserted facts remain session-owned.

For example, use `ruleforge_session_add_fact_json(session, "Order", json,
&fact)`. The type must have been imported by the RFL rule pack. The KB is the
unique schema source for session ingestion; callers do not pass a schema path.

At startup, `ruleforge_kb_has_schema_type` can validate that an external type is
available. For enum fields, `ruleforge_fact_get_field_as_enum` returns the
numeric value and DataBind schema name together; `ruleforge_fact_get_enum_name`
returns only the name.

Owned DataBind objects support JSON, YAML, XML, CSV, and schema binary
serialization. Release text with `ruleforge_data_bind_serialized_free` and
binary output with `ruleforge_data_bind_binary_free`.

For example, a JSONPath such as `$.customers[*]` can select customer records
from an envelope. RFL may then match only `Customer(age >= 18)`. Both selected
customers are inserted; only adults satisfy that rule constraint.

## Incremental Streams

Use streams when bytes arrive in chunks or the caller does not want to buffer
the complete document itself:

| Input shape | Constructor |
|---|---|
| one JSON value | `ruleforge_data_bind_stream_json_create` |
| all JSON values | `ruleforge_data_bind_stream_json_all_create` |
| one JSON path value | `ruleforge_data_bind_stream_json_path_create` |
| all JSON path values | `ruleforge_data_bind_stream_json_path_all_create` |
| all CSV rows | `ruleforge_data_bind_stream_csv_all_create` |
| CSV path rows | `ruleforge_data_bind_stream_csv_path_create` |
| one XML value | `ruleforge_data_bind_stream_xml_create` |
| all XML path values | `ruleforge_data_bind_stream_xml_path_all_create` |

All constructors use the same lifecycle:

```text
create -> feed memory and/or files -> finish -> destroy
```

`finish` validates the completed value and inserts the resulting facts as one
operation. No fact is inserted merely because a chunk was accepted.

Complete and incremental APIs call the corresponding DataBind path operation.
Given the same schema, payload, fact type, and path, they produce the same
candidate facts; only payload delivery and commit timing differ.

```c
ruleforge_data_bind_stream_t stream = NULL;
ruleforge_fact_t *facts = NULL;
int fact_count = 0;

ruleforge_status_t status = ruleforge_data_bind_stream_json_create(
    session, "orders.schema", "Order", &stream);
if (status == RULES_FORGE_OK) {
  status = ruleforge_data_bind_stream_feed(stream, first_chunk, first_len);
}
if (status == RULES_FORGE_OK) {
  status = ruleforge_data_bind_stream_feed(stream, second_chunk, second_len);
}
if (status == RULES_FORGE_OK) {
  status = ruleforge_data_bind_stream_finish(stream, &facts, &fact_count);
}

ruleforge_fact_array_free(facts);
ruleforge_data_bind_stream_destroy(stream);
```

## Async I/O Integration

DataBind streams are synchronous incremental parsers. An async socket, file,
or broker callback may call `feed` for each received chunk and call `finish`
after end-of-message. RulesForge does not schedule callbacks, retain the
caller's chunk buffer, or create worker threads.

Keep the stream and its session on the same execution strand or thread. If an
async framework may resume callbacks on different workers, pin the operation or
dispatch all stream calls to one serialized executor.

## Continuous Events

Continuous sessions accept one JSON object with explicit event metadata, or a
path-selected JSON, CSV, or XML batch whose records contain metadata fields.

The single-event JSON lifecycle is:

```text
create(session, type, event_id, entry_point, event_time)
  -> feed
  -> finish and commit exactly one event step
  -> inspect/acknowledge result
  -> destroy stream
```

The event is not accepted until `ruleforge_continuous_data_bind_stream_finish`
succeeds.

Path-selected batches use these complete-document APIs:

- `ruleforge_continuous_push_json_path`
- `ruleforge_continuous_push_csv_path`
- `ruleforge_continuous_push_xml_path`

Their incremental equivalents are the three
`ruleforge_continuous_data_bind_stream_*_path_create` constructors. The caller
names the bound string field containing each event ID and the bound integer
field containing each event timestamp. All selected records share one entry
point and commit atomically through the continuous batch operation.

DataBind 2.0 invokes a synchronous callback whenever a streamable JSON array
item, CSV row, or XML path element has passed path selection and schema binding.
RulesForge immediately copies that borrowed value into an owned pending event.
No event enters working memory during `feed`: `finish` atomically submits the
pending records through the continuous batch operation. This preserves message
transactionality while avoiding a second parse or finish-time list traversal.

File, socket, HTTP, and broker adapters remain byte sources. They call `feed`
only when the callback consumer is ready, which provides source-side
backpressure without a hidden DataBind thread or event loop.

## Failure and Ownership

- A failed `feed` or `finish` makes the stream unusable except for `destroy`.
- A finished stream cannot be fed or finished again.
- Destroying a session with an active stream is rejected.
- Input memory only needs to remain valid for the duration of the call.
- Returned fact handles are borrowed from the session or result owner.
- Use `ruleforge_get_last_error_message()` immediately after a failed call.
