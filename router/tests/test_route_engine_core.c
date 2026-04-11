/**
 * @file test_route_engine_core.c
 * @brief BDD tests for route_engine source payload routing and execute_routes via mock plugins
 */

#include "tinytest.h"
#include "route_engine.h"
#include "rule_forge_plugin.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fmt.h>

// ============================================================
// Mock DataSource
// ============================================================

typedef struct {
    const char* json;       /* text payload to return */
    const uint8_t* binary;
    size_t binary_len;
    DataBindFormat format;
    int fetch_count;
    int fail_after;         /* return error after N fetches (-1 = never) */
    int end_after;          /* return END_OF_STREAM after N successful fetches (-1 = never) */
} mock_source_ctx_t;

/* test-controlled handle — set before registering source */
static mock_source_ctx_t* g_source_ctx = NULL;

static ruleforge_status_t mock_source_init(const char* cfg, void** out_ctx) {
    (void)cfg;
    mock_source_ctx_t* ctx = calloc(1, sizeof(mock_source_ctx_t));
    ctx->json       = "{\"quantity\": 6, \"unitPrice\": 20.0, \"finalPrice\": 0.01}";
    ctx->binary     = NULL;
    ctx->binary_len = 0;
    ctx->format     = DATA_BIND_FORMAT_JSON;
    ctx->fail_after = -1;
    ctx->end_after  = -1;
    *out_ctx = ctx;
    g_source_ctx = ctx;
    return RULES_FORGE_OK;
}

static ruleforge_status_t mock_source_fetch(
    void* ctx, const char* query, const char* fact_type,
    DataBindFormat* out_format, const uint8_t** out_data, size_t* out_len)
{
    (void)query; (void)fact_type;
    mock_source_ctx_t* c = (mock_source_ctx_t*)ctx;
    if (c->fail_after >= 0 && c->fetch_count >= c->fail_after)
        return RULES_FORGE_ERROR_GENERIC;
    if (c->end_after >= 0 && c->fetch_count >= c->end_after)
        return RULES_FORGE_STATUS_END_OF_STREAM;
    *out_format = c->format;
    if (c->format == DATA_BIND_FORMAT_BINARY) {
        *out_data = c->binary;
        *out_len = c->binary_len;
    } else {
        *out_data = (const uint8_t*)c->json;
        *out_len = strlen(c->json);
    }
    c->fetch_count++;
    return RULES_FORGE_OK;
}

static void mock_source_free_data(void* ctx, const uint8_t* data) {
    (void)ctx; (void)data;
}

static void mock_source_cleanup(void* ctx) { free(ctx); g_source_ctx = NULL; }

static const ruleforge_datasource_vtable_t mock_source_vtable = {
    .init      = mock_source_init,
    .fetch     = mock_source_fetch,
    .free_data = mock_source_free_data,
    .cleanup   = mock_source_cleanup,
};

// ============================================================
// Mock DataSink
// ============================================================

typedef struct {
    int push_count;
    char last_metadata[256];
    char last_source_name[64];
    char last_source_query[128];
    char last_fact_type[128];
    char last_target_name[64];
    char last_payload[256];
    int saw_original_fact;
    int saw_original_batch;
    int saw_decision_fact;
    uint64_t last_message_index;
    int last_decision_index;
    int last_decision_count;
    int last_original_fact_count;
} mock_sink_ctx_t;

static mock_sink_ctx_t* g_high_value_ctx = NULL;
static mock_sink_ctx_t* g_default_ctx    = NULL;

static ruleforge_status_t mock_sink_init(const char* cfg, void** out_ctx) {
    mock_sink_ctx_t* ctx = calloc(1, sizeof(mock_sink_ctx_t));
    /* cfg carries the sink name so tests can distinguish */
    if (cfg && strcmp(cfg, "high_value_sink") == 0)
        g_high_value_ctx = ctx;
    else
        g_default_ctx = ctx;
    *out_ctx = ctx;
    return RULES_FORGE_OK;
}

static ruleforge_status_t mock_sink_push_route(void* ctx, const ruleforge_route_envelope_t* route) {
    mock_sink_ctx_t* c = (mock_sink_ctx_t*)ctx;
    c->push_count++;

    if (route->effective_metadata) {
        strncpy(c->last_metadata, route->effective_metadata, sizeof(c->last_metadata) - 1);
    }
    if (route->source_name) {
        strncpy(c->last_source_name, route->source_name, sizeof(c->last_source_name) - 1);
    }
    if (route->source_query) {
        strncpy(c->last_source_query, route->source_query, sizeof(c->last_source_query) - 1);
    }
    if (route->fact_type) {
        strncpy(c->last_fact_type, route->fact_type, sizeof(c->last_fact_type) - 1);
    }
    if (route->target_name) {
        strncpy(c->last_target_name, route->target_name, sizeof(c->last_target_name) - 1);
    }
    if (route->payload) {
        strncpy(c->last_payload, route->payload, sizeof(c->last_payload) - 1);
    }

    c->saw_original_fact = route->original_fact != NULL;
    c->saw_original_batch = route->original_facts != NULL;
    c->saw_decision_fact = route->decision_fact != NULL;
    c->last_message_index = route->message_index;
    c->last_decision_index = route->decision_index;
    c->last_decision_count = route->decision_count;
    c->last_original_fact_count = route->original_fact_count;
    return RULES_FORGE_OK;
}

static void mock_sink_cleanup(void* ctx) { free(ctx); }

static const ruleforge_datasink_vtable_t mock_sink_vtable = {
    .init       = mock_sink_init,
    .push_route = mock_sink_push_route,
    .cleanup    = mock_sink_cleanup,
};

// ============================================================
// Helpers
// ============================================================

static route_engine_t* make_engine_with_plugins(void) {
    route_engine_t* engine = route_engine_create_empty();
    if (!engine) return NULL;
    route_engine_register_source(engine, "mock_source", &mock_source_vtable, NULL);
    route_engine_register_sink(engine, "mock_sink", &mock_sink_vtable, NULL);
    return engine;
}

#ifndef ROUTING_RULES_FILE
#define ROUTING_RULES_FILE "tests/routing_rules.rfl"
#endif

#ifndef PAYMENTS_RULES_FILE
#define PAYMENTS_RULES_FILE "../../capi/examples/payments.rfl"
#endif

#define FACT_TYPE "com.example.pricing.Order"

static route_engine_t* make_full_engine(void) {
    g_high_value_ctx = NULL;
    g_default_ctx    = NULL;

    route_engine_config_t cfg = {
        .rules_file        = ROUTING_RULES_FILE,
        .schema_file       = NULL,
        .session_pool_size = 4,
        .max_rule_fires    = 100,
        .drop_no_route     = 0,
        .default_target    = NULL,
        .plugins           = NULL,
        .plugin_count      = 0,
    };

    route_engine_t* engine = route_engine_create(&cfg);
    if (!engine) return NULL;

    route_engine_register_source(engine, "mock_source", &mock_source_vtable, NULL);
    route_engine_register_sink(engine, "high_value_sink", &mock_sink_vtable, "high_value_sink");
    route_engine_register_sink(engine, "default_sink",    &mock_sink_vtable, "default_sink");
    return engine;
}

static route_engine_t* make_default_target_engine(void) {
    g_high_value_ctx = NULL;
    g_default_ctx = NULL;

    route_engine_config_t cfg = {
        .rules_file        = ROUTING_RULES_FILE,
        .schema_file       = NULL,
        .session_pool_size = 4,
        .max_rule_fires    = 100,
        .drop_no_route     = 0,
        .default_target    = "default_sink",
        .plugins           = NULL,
        .plugin_count      = 0,
    };

    route_engine_t* engine = route_engine_create(&cfg);
    if (!engine) return NULL;

    route_engine_register_source(engine, "mock_source", &mock_source_vtable, NULL);
    route_engine_register_sink(engine, "default_sink", &mock_sink_vtable, "default_sink");
    return engine;
}

static route_engine_t* make_full_engine_all_targets(void) {
    g_high_value_ctx = NULL;
    g_default_ctx = NULL;

    route_engine_config_t cfg = {
        .rules_file        = ROUTING_RULES_FILE,
        .schema_file       = NULL,
        .session_pool_size = 4,
        .max_rule_fires    = 100,
        .drop_no_route     = 0,
        .default_target    = NULL,
        .plugins           = NULL,
        .plugin_count      = 0,
    };

    route_engine_t* engine = route_engine_create(&cfg);
    if (!engine) return NULL;

    route_engine_register_source(engine, "mock_source", &mock_source_vtable, NULL);
    route_engine_register_sink(engine, "high_value_sink", &mock_sink_vtable, "high_value_sink");
    route_engine_register_sink(engine, "default_sink", &mock_sink_vtable, "default_sink");
    return engine;
}

// ============================================================
// Tests
// ============================================================

spec("Route Engine Core") {

    describe("register_source / register_sink") {
        it("should register source and increment count") {
            route_engine_t* engine = route_engine_create_empty();
            check(engine != NULL);

            ruleforge_status_t s = route_engine_register_source(
                engine, "src", &mock_source_vtable, NULL);
            check_int_eq(s, RULES_FORGE_OK);
            check_int_eq(route_engine_source_count(engine), 1);

            route_engine_destroy(engine);
        }

        it("should register sink and increment count") {
            route_engine_t* engine = route_engine_create_empty();
            check(engine != NULL);

            ruleforge_status_t s = route_engine_register_sink(
                engine, "sink", &mock_sink_vtable, NULL);
            check_int_eq(s, RULES_FORGE_OK);
            check_int_eq(route_engine_sink_count(engine), 1);

            route_engine_destroy(engine);
        }

        it("should register another sink and increment count") {
            route_engine_t* engine = route_engine_create_empty();
            check(engine != NULL);

            ruleforge_status_t s = route_engine_register_sink(
                engine, "another_sink", &mock_sink_vtable, NULL);
            check_int_eq(s, RULES_FORGE_OK);
            check_int_eq(route_engine_sink_count(engine), 1);

            route_engine_destroy(engine);
        }

        it("should reject NULL arguments") {
            route_engine_t* engine = route_engine_create_empty();

            check_int_eq(
                route_engine_register_source(NULL, "src", &mock_source_vtable, NULL),
                RULES_FORGE_ERROR_INVALID_ARGUMENT);
            check_int_eq(
                route_engine_register_source(engine, NULL, &mock_source_vtable, NULL),
                RULES_FORGE_ERROR_INVALID_ARGUMENT);
            check_int_eq(
                route_engine_register_source(engine, "src", NULL, NULL),
                RULES_FORGE_ERROR_INVALID_ARGUMENT);

            route_engine_destroy(engine);
        }

        it("should reject overflow beyond MAX_SOURCES") {
            route_engine_t* engine = route_engine_create_empty();
            ruleforge_status_t last = RULES_FORGE_OK;
            char name[16];
            for (int i = 0; i < 20; i++) {
                fmt(name, sizeof(name), "src_{}", i);
                last = route_engine_register_source(engine, name, &mock_source_vtable, NULL);
            }
            check_int_eq(last, RULES_FORGE_ERROR_GENERIC);
            route_engine_destroy(engine);
        }
    }

    describe("get_stats on empty engine") {
        it("should not crash") {
            route_engine_t* engine = route_engine_create_empty();
            check(engine != NULL);

            route_engine_stats_t stats;
            route_engine_get_stats(engine, &stats);
            check_int_eq((int)stats.total_messages, 0);

            route_engine_destroy(engine);
        }

        it("should handle NULL engine and NULL stats gracefully") {
            route_engine_stats_t stats;
            route_engine_get_stats(NULL, &stats);
            route_engine_t* e = route_engine_create_empty();
            route_engine_get_stats(e, NULL);
            route_engine_destroy(e);
        }
    }

    describe("route_engine_run argument validation") {
        it("should return INVALID_ARGUMENT for unknown source") {
            route_engine_t* engine = make_engine_with_plugins();
            check(engine != NULL);

            ruleforge_status_t s = route_engine_run(
                engine, "nonexistent", NULL, "Fact", 1);
            check_int_eq(s, RULES_FORGE_ERROR_INVALID_ARGUMENT);

            route_engine_destroy(engine);
        }

        it("should return INVALID_ARGUMENT for NULL engine") {
            check_int_eq(
                route_engine_run(NULL, "src", NULL, "Fact", 1),
                RULES_FORGE_ERROR_INVALID_ARGUMENT);
        }

        it("should return INVALID_ARGUMENT for NULL fact_type") {
            route_engine_t* engine = make_engine_with_plugins();
            check_int_eq(
                route_engine_run(engine, "mock_source", NULL, NULL, 1),
                RULES_FORGE_ERROR_INVALID_ARGUMENT);
            route_engine_destroy(engine);
        }
    }

    describe("route_engine_stop") {
        it("should return OK") {
            route_engine_t* engine = route_engine_create_empty();
            check_int_eq(route_engine_stop(engine), RULES_FORGE_OK);
            route_engine_destroy(engine);
        }

        it("should return INVALID_ARGUMENT for NULL") {
            check_int_eq(
                route_engine_stop(NULL),
                RULES_FORGE_ERROR_INVALID_ARGUMENT);
        }
    }

    describe("plugin startup loading via config") {
        it("should load zero plugins when plugin_count is 0") {
            route_engine_plugin_entry_t plugins[] = {
                { .name = "unused_source", .dll_path = "nonexistent.dll", .type = "source", .config_json = NULL }
            };
            route_engine_config_t cfg = {
                .rules_file        = "nonexistent.rfl",
                .schema_file       = NULL,
                .session_pool_size = 4,
                .max_rule_fires    = 10,
                .plugins           = plugins,
                .plugin_count      = 0,
            };
            check_int_eq(cfg.plugin_count, 0);
        }

        it("should fail engine creation when startup plugin loading fails") {
            route_engine_plugin_entry_t plugins[] = {
                { .name = "missing_source", .dll_path = "nonexistent.dll", .type = "source", .config_json = NULL }
            };
            route_engine_config_t cfg = {
                .rules_file        = ROUTING_RULES_FILE,
                .schema_file       = NULL,
                .session_pool_size = 4,
                .max_rule_fires    = 10,
                .plugins           = plugins,
                .plugin_count      = 1,
            };

            route_engine_t* engine = route_engine_create(&cfg);
            check(engine == NULL);
        }
    }

    describe("source payload integration (real KB + mock source/sink)") {
        it("should route high-value order to high_value_sink") {
            route_engine_t* engine = make_full_engine();
            if (!engine) { check(0 && "engine create failed — check routing_rules.rfl path"); }

            /* quantity=6, unitPrice=20 → High Discount → finalPrice=108 → high_value_sink */
            g_source_ctx->json = "{\"quantity\": 6, \"unitPrice\": 20.0, \"finalPrice\": 0.01}";

            ruleforge_status_t s = route_engine_run(
                engine, "mock_source", "orders.high", FACT_TYPE, 1);
            check_int_eq(s, RULES_FORGE_OK);

            route_engine_stats_t stats;
            route_engine_get_stats(engine, &stats);
            check_int_eq((int)stats.total_messages, 1);
            check_int_eq((int)stats.total_routed, 1);
            check_int_eq((int)stats.route_errors, 0);

            check(g_high_value_ctx != NULL);
            check_int_eq(g_high_value_ctx->push_count, 1);
            check_str_eq(g_high_value_ctx->last_metadata, "dispatch:high_value");
            check_str_eq(g_high_value_ctx->last_source_name, "mock_source");
            check_str_eq(g_high_value_ctx->last_source_query, "orders.high");
            check_str_eq(g_high_value_ctx->last_fact_type, FACT_TYPE);
            check_str_eq(g_high_value_ctx->last_target_name, "high_value_sink");
            check_str_eq(g_high_value_ctx->last_payload, "dispatch:high_value");
            check(g_high_value_ctx->saw_original_fact);
            check(g_high_value_ctx->saw_decision_fact);
            check_int_eq((int)g_high_value_ctx->last_message_index, 1);
            check_int_eq(g_high_value_ctx->last_decision_index, 0);
            check_int_eq(g_high_value_ctx->last_decision_count, 1);

            route_engine_destroy(engine);
        }

        it("should route low-value order to default_sink") {
            route_engine_t* engine = make_full_engine();
            if (!engine) { check(0 && "engine create failed"); }

            /* quantity=1, unitPrice=10 → No Discount → finalPrice=10 → default_sink */
            g_source_ctx->json = "{\"quantity\": 1, \"unitPrice\": 10.0, \"finalPrice\": 0.01}";

            route_engine_run(engine, "mock_source", "orders.low", FACT_TYPE, 1);

            check(g_default_ctx != NULL);
            check_int_eq(g_default_ctx->push_count, 1);
            check_str_eq(g_default_ctx->last_metadata, "default");

            route_engine_destroy(engine);
        }

        it("should route single-row CSV order and preserve original fact") {
            route_engine_t* engine = make_full_engine();
            if (!engine) { check(0 && "engine create failed"); }

            g_source_ctx->format = DATA_BIND_FORMAT_CSV;
            g_source_ctx->json =
                "quantity,unitPrice,finalPrice\n"
                "6,20,0.01\n";

            ruleforge_status_t status = route_engine_run(
                engine, "mock_source", "orders.csv", FACT_TYPE, 1);
            check_int_eq(status, RULES_FORGE_OK);

            check(g_high_value_ctx != NULL);
            check_int_eq(g_high_value_ctx->push_count, 1);
            check(g_high_value_ctx->saw_original_fact);
            check(g_high_value_ctx->saw_decision_fact);
            check_str_eq(g_high_value_ctx->last_source_query, "orders.csv");

            route_engine_destroy(engine);
        }

        it("should route binary order payload and preserve original fact") {
            route_engine_t* engine = make_full_engine();
            uint8_t order_buf[20] = {0};
            int32_t quantity = 6;
            double unit_price = 20.0;
            double final_price = 0.01;
            if (!engine) { check(0 && "engine create failed"); }

            memcpy(order_buf + 0, &quantity, sizeof(quantity));
            memcpy(order_buf + 4, &unit_price, sizeof(unit_price));
            memcpy(order_buf + 12, &final_price, sizeof(final_price));

            g_source_ctx->format = DATA_BIND_FORMAT_BINARY;
            g_source_ctx->binary = order_buf;
            g_source_ctx->binary_len = sizeof(order_buf);

            ruleforge_status_t status = route_engine_run(
                engine, "mock_source", "orders.binary", FACT_TYPE, 1);
            check_int_eq(status, RULES_FORGE_OK);

            check(g_high_value_ctx != NULL);
            check_int_eq(g_high_value_ctx->push_count, 1);
            check(g_high_value_ctx->saw_original_fact);
            check(g_high_value_ctx->saw_decision_fact);
            check_str_eq(g_high_value_ctx->last_source_query, "orders.binary");

            route_engine_destroy(engine);
        }

        it("should expose batch original facts for multi-row CSV routes") {
            route_engine_t* engine = make_full_engine_all_targets();
            if (!engine) { check(0 && "engine create failed"); }

            g_source_ctx->format = DATA_BIND_FORMAT_CSV;
            g_source_ctx->json =
                "quantity,unitPrice,finalPrice\n"
                "6,20,0.01\n"
                "1,10,0.01\n";

            ruleforge_status_t status = route_engine_run(
                engine, "mock_source", "orders.batch.csv", FACT_TYPE, 1);
            check_int_eq(status, RULES_FORGE_OK);

            check(g_high_value_ctx != NULL);
            check(g_default_ctx != NULL);
            check_int_eq(g_high_value_ctx->push_count, 1);
            check_int_eq(g_default_ctx->push_count, 1);

            check(!g_high_value_ctx->saw_original_fact);
            check(!g_default_ctx->saw_original_fact);
            check(g_high_value_ctx->saw_original_batch);
            check(g_default_ctx->saw_original_batch);
            check_int_eq(g_high_value_ctx->last_original_fact_count, 2);
            check_int_eq(g_default_ctx->last_original_fact_count, 2);
            check_int_eq(g_high_value_ctx->last_decision_count, 2);
            check_int_eq(g_default_ctx->last_decision_count, 2);
            check_str_eq(g_high_value_ctx->last_source_query, "orders.batch.csv");
            check_str_eq(g_default_ctx->last_source_query, "orders.batch.csv");

            route_engine_destroy(engine);
        }

        it("should increment total_messages per fetch") {
            route_engine_t* engine = make_full_engine();
            if (!engine) { check(0 && "engine create failed"); }

            g_source_ctx->json = "{\"quantity\": 1, \"unitPrice\": 5.0, \"finalPrice\": 0.01}";

            route_engine_run(engine, "mock_source", NULL, FACT_TYPE, 3);

            route_engine_stats_t stats;
            route_engine_get_stats(engine, &stats);
            check_int_eq((int)stats.total_messages, 3);

            route_engine_destroy(engine);
        }

        it("should count no_route when rules produce no decision") {
            route_engine_t* engine = make_full_engine();
            if (!engine) { check(0 && "engine create failed"); }

            /* quantity=0 means no discount rules fire. finalPrice remains 0.01,
               which fails both > 0.01 and >= 100.0 routing rules — producing no route */
            g_source_ctx->json = "{\"quantity\": 0, \"unitPrice\": 5.0, \"finalPrice\": 0.01}";

            route_engine_run(engine, "mock_source", NULL, FACT_TYPE, 1);

            route_engine_stats_t stats;
            route_engine_get_stats(engine, &stats);
            check_int_eq((int)stats.no_route, 1);

            route_engine_destroy(engine);
        }

        it("should route no-decision payloads to the default target") {
            route_engine_t* engine = make_default_target_engine();
            if (!engine) { check(0 && "engine create failed"); }

            g_source_ctx->json = "{\"quantity\": 0, \"unitPrice\": 5.0, \"finalPrice\": 0.01}";

            ruleforge_status_t status = route_engine_run(
                engine, "mock_source", "orders.unrouted", FACT_TYPE, 1);
            check_int_eq(status, RULES_FORGE_OK);

            check(g_default_ctx != NULL);
            check_int_eq(g_default_ctx->push_count, 1);
            check_str_eq(g_default_ctx->last_source_name, "mock_source");
            check_str_eq(g_default_ctx->last_source_query, "orders.unrouted");
            check_str_eq(g_default_ctx->last_target_name, "default_sink");
            check_int_eq((int)g_default_ctx->last_message_index, 1);
            check_int_eq(g_default_ctx->last_decision_count, 0);
            check(g_default_ctx->saw_original_fact);
            check(!g_default_ctx->saw_decision_fact);

            route_engine_destroy(engine);
        }

        it("should stop after max_messages") {
            route_engine_t* engine = make_full_engine();
            if (!engine) { check(0 && "engine create failed"); }

            g_source_ctx->json = "{\"quantity\": 1, \"unitPrice\": 5.0, \"finalPrice\": 0.01}";

            route_engine_run(engine, "mock_source", NULL, FACT_TYPE, 5);

            check_int_eq(g_source_ctx->fetch_count, 5);

            route_engine_destroy(engine);
        }

        it("should propagate source fetch failures instead of spinning forever") {
            route_engine_t* engine = make_full_engine();
            if (!engine) { check(0 && "engine create failed"); }

            g_source_ctx->json = "{\"quantity\": 1, \"unitPrice\": 5.0, \"finalPrice\": 0.01}";
            g_source_ctx->fail_after = 1;

            ruleforge_status_t status = route_engine_run(engine, "mock_source", NULL, FACT_TYPE, 5);
            check_int_eq(status, RULES_FORGE_ERROR_GENERIC);
            check_int_eq(g_source_ctx->fetch_count, 1);

            route_engine_stats_t stats;
            route_engine_get_stats(engine, &stats);
            check_int_eq((int)stats.total_messages, 1);
            check_int_eq((int)stats.avg_decisions_per_msg, 1);

            route_engine_destroy(engine);
        }

        it("should stop cleanly when source reaches end of stream") {
            route_engine_t* engine = make_full_engine();
            if (!engine) { check(0 && "engine create failed"); }

            g_source_ctx->json = "{\"quantity\": 1, \"unitPrice\": 5.0, \"finalPrice\": 0.01}";
            g_source_ctx->end_after = 1;

            ruleforge_status_t status = route_engine_run(engine, "mock_source", NULL, FACT_TYPE, 5);
            check_int_eq(status, RULES_FORGE_OK);
            check_int_eq(g_source_ctx->fetch_count, 1);

            route_engine_stats_t stats;
            route_engine_get_stats(engine, &stats);
            check_int_eq((int)stats.total_messages, 1);

            route_engine_destroy(engine);
        }
    }
}
