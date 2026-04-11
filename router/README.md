# RulesForge Router

`router/` is a source-to-sink routing runtime:

```text
DataSource -> route_engine -> RulesForge session -> RouteDecision query -> DataSink
```

The core path is simple:

1. a source plugin fetches one payload
2. the router parses it into one or more facts
3. RulesForge fires rules
4. the router queries `get_route_decisions`
5. sinks receive a `ruleforge_route_envelope_t`

There is no broker ingress path here anymore. The router is about sources and sinks.

## Main API

The main execution API is:

```c
ruleforge_status_t route_engine_run(
    route_engine_t* engine,
    const char* source_name,
    const char* query,
    const char* fact_type,
    int max_messages);
```

`route_engine_run()` blocks, fetches from the named source, routes each payload, and stops when:

- `max_messages` is reached
- the source returns `RULES_FORGE_STATUS_END_OF_STREAM`
- the source returns an error
- the caller invokes `route_engine_stop()`

This is deliberate. A broken source should fail loudly instead of being hidden behind retries or queueing tricks.

## Supported Input Formats

Today the router accepts:

- `DATA_BIND_FORMAT_JSON`
- `DATA_BIND_FORMAT_CSV`
- `DATA_BIND_FORMAT_BINARY`

## Envelope Contract

Sinks receive one `ruleforge_route_envelope_t` per route decision.

The envelope carries:

- `source_name`
- `source_query`
- `fact_type`
- `target_name`
- `payload`
- `metadata`
- `effective_metadata`
- `original_fact`
- `original_facts`
- `original_fact_count`
- `decision_fact`
- `message_index`
- `decision_index`
- `decision_count`

Rules:

- `decision_fact` is the selected `RouteDecision`
- `original_fact` is set only when one fetched payload maps to exactly one inserted fact
- multi-row CSV keeps `original_fact == NULL` and exposes the full batch through `original_facts`
- `effective_metadata` prefers `payload`, then falls back to `metadata`

That last rule is intentional. Index-based guessing is brittle garbage.

## Plugin Loading

Startup plugin entries:

```c
typedef struct {
    const char* name;
    const char* dll_path;
    const char* type;
    const char* config_json;
} route_engine_plugin_entry_t;
```

Supported plugin types:

- `source`
- `sink`
- `native`
- `tscript`

Additive loading API:

```c
ruleforge_status_t route_engine_load_plugin_ex(
    route_engine_t* engine,
    const char* plugin_name,
    const char* dll_path,
    const char* plugin_type,
    const char* config_json);
```

Rules:

- `plugin_name` is optional
- if omitted, the router derives it from the DLL filename
- duplicate source or sink names are rejected
- incomplete source or sink vtables are rejected
- startup plugin load failures abort `route_engine_create()`

## Runtime Semantics

`route_engine_run()` is strict:

- source fetch failure is returned to the caller
- rule firing failure is returned to the caller
- parse failure increments parse statistics and aborts that run
- end of stream returns `RULES_FORGE_OK`

If no explicit `RouteDecision` is produced and `default_target` is configured, the router emits one sink envelope with:

- `decision_count == 0`
- `decision_fact == NULL`

This keeps default delivery explicit instead of magical.

## Statistics

`route_engine_stats_t` reports:

- `total_messages`
- `total_routed`
- `no_route`
- `route_errors`
- `parse_errors`
- `avg_decisions_per_msg`
- `avg_route_time_us`

Pool usage is exposed through `route_engine_get_stats()`.

## Example

```c
#include "route_engine.h"

int main(void) {
    route_engine_plugin_entry_t plugins[] = {
        {
            .name = "orders_source",
            .dll_path = "file_source_plugin.dll",
            .type = "source",
            .config_json = "{\"path\":\"orders.csv\"}"
        },
        {
            .name = "audit_sink",
            .dll_path = "http_sink_plugin.dll",
            .type = "sink",
            .config_json = "{\"url\":\"http://localhost:8080/ingest\"}"
        }
    };

    route_engine_config_t config = {
        .rules_file = "router/tests/routing_rules.rfl",
        .schema_file = NULL,
        .session_pool_size = 8,
        .max_rule_fires = 100,
        .drop_no_route = 1,
        .default_target = NULL,
        .plugins = plugins,
        .plugin_count = 2,
    };

    route_engine_t* engine = route_engine_create(&config);
    if (!engine) {
        return 1;
    }

    ruleforge_status_t status = route_engine_run(
        engine,
        "orders_source",
        NULL,
        "com.example.pricing.Order",
        -1);

    route_engine_destroy(engine);
    return status == RULES_FORGE_OK ? 0 : 2;
}
```

## Open Gaps

Still missing:

- richer batch-to-decision correlation than “all facts from one fetched payload”
- sink-side retry and DLQ stages

These are architectural gaps, not cosmetic ones.

## Tests

Current coverage includes:

- core route-engine behavior with mock source and sink plugins
- DLL plugin loading for source and sink plugins
- startup plugin failure semantics
- duplicate logical plugin names
- metadata selection behavior
- CSV single-row and batch routing
- binary single-payload routing with original-fact preservation
