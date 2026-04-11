/**
 * @file test_route_engine_plugins.c
 * @brief BDD tests for route_engine plugin loading via DLL
 */

#include "tinytest.h"
#include "route_engine.h"
#include "rule_forge_plugin.h"
#include "turbo_fs.h"
#include <string.h>
#include <stdlib.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#endif

#ifndef FILE_SOURCE_DLL
#ifdef _WIN32
#define FILE_SOURCE_DLL "file_source_plugin.dll"
#else
#define FILE_SOURCE_DLL "libfile_source_plugin.so"
#endif
#endif

#ifndef HTTP_SINK_DLL
#ifdef _WIN32
#define HTTP_SINK_DLL "http_sink_plugin.dll"
#else
#define HTTP_SINK_DLL "libhttp_sink_plugin.so"
#endif
#endif

#ifndef ROUTING_RULES_FILE
#define ROUTING_RULES_FILE "tests/routing_rules.rfl"
#endif

typedef struct {
    int push_count;
    char last_target[64];
    char last_query[128];
} plugin_test_sink_ctx_t;

static plugin_test_sink_ctx_t* g_plugin_sink_ctx = NULL;

static ruleforge_status_t plugin_test_sink_init(const char* cfg, void** out_ctx) {
    (void)cfg;
    plugin_test_sink_ctx_t* ctx = (plugin_test_sink_ctx_t*)calloc(1, sizeof(*ctx));
    *out_ctx = ctx;
    g_plugin_sink_ctx = ctx;
    return ctx ? RULES_FORGE_OK : RULES_FORGE_ERROR_MEMORY_ALLOCATION;
}

static ruleforge_status_t plugin_test_sink_push_route(void* ctx,
                                                      const ruleforge_route_envelope_t* route) {
    plugin_test_sink_ctx_t* sink_ctx = (plugin_test_sink_ctx_t*)ctx;
    if (!sink_ctx || !route) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    sink_ctx->push_count++;
    if (route->target_name) {
        strncpy(sink_ctx->last_target, route->target_name, sizeof(sink_ctx->last_target) - 1);
    }
    if (route->source_query) {
        strncpy(sink_ctx->last_query, route->source_query, sizeof(sink_ctx->last_query) - 1);
    }
    return RULES_FORGE_OK;
}

static void plugin_test_sink_cleanup(void* ctx) {
    free(ctx);
    g_plugin_sink_ctx = NULL;
}

static const ruleforge_datasink_vtable_t plugin_test_sink_vtable = {
    .init = plugin_test_sink_init,
    .push_route = plugin_test_sink_push_route,
    .cleanup = plugin_test_sink_cleanup,
};

static void create_test_csv(const char* filename, const char* content) {
    static int test_data_ready = 0;
    if (!test_data_ready) {
#ifdef _WIN32
        _mkdir("test_data_router");
#else
        mkdir("test_data_router", 0755);
#endif
        test_data_ready = 1;
    }
    turbo_fs_buf_t buf = turbo_fs_buf_init((char*)content, strlen(content));
    turbo_fs_write_file(filename, &buf);
}

static void remove_test_file(const char* filename) {
    turbo_fs_unlink(filename);
}

spec("Route Engine Plugin Loading") {
    describe("Load Source Plugin") {
        it("should load file_source_plugin.dll as source") {
            route_engine_t* engine = route_engine_create_empty();
            check(engine != NULL);

            ruleforge_status_t status = route_engine_load_plugin(
                engine, FILE_SOURCE_DLL, "source");

            check_int_eq(status, RULES_FORGE_OK);
            check_int_eq(route_engine_source_count(engine), 1);

            route_engine_destroy(engine);
        }

        it("should load source plugin with explicit logical name") {
            route_engine_t* engine = route_engine_create_empty();
            check(engine != NULL);

            ruleforge_status_t status = route_engine_load_plugin_ex(
                engine, "orders_source", FILE_SOURCE_DLL, "source", "{\"path\":\"orders.csv\"}");

            check_int_eq(status, RULES_FORGE_OK);
            check_int_eq(route_engine_source_count(engine), 1);

            route_engine_destroy(engine);
        }

        it("should reject unknown plugin type") {
            route_engine_t* engine = route_engine_create_empty();
            check(engine != NULL);

            ruleforge_status_t status = route_engine_load_plugin(
                engine, FILE_SOURCE_DLL, "unknown");

            check_int_eq(status, RULES_FORGE_ERROR_INVALID_ARGUMENT);

            route_engine_destroy(engine);
        }

        it("should fail for non-existent DLL") {
            route_engine_t* engine = route_engine_create_empty();
            check(engine != NULL);

            ruleforge_status_t status = route_engine_load_plugin(
                engine, "nonexistent_plugin.dll", "source");

            check_int_eq(status, RULES_FORGE_ERROR_GENERIC);

            route_engine_destroy(engine);
        }
    }

    describe("Load Sink Plugin") {
        it("should load http_sink_plugin.dll as sink") {
            route_engine_t* engine = route_engine_create_empty();
            check(engine != NULL);

            ruleforge_status_t status = route_engine_load_plugin(
                engine, HTTP_SINK_DLL, "sink");

            check_int_eq(status, RULES_FORGE_OK);
            check_int_eq(route_engine_sink_count(engine), 1);

            route_engine_destroy(engine);
        }
    }

    describe("Load Multiple Plugins") {
        it("should load source and sink together") {
            route_engine_t* engine = route_engine_create_empty();
            check(engine != NULL);

            ruleforge_status_t s1 = route_engine_load_plugin(
                engine, FILE_SOURCE_DLL, "source");
            ruleforge_status_t s2 = route_engine_load_plugin(
                engine, HTTP_SINK_DLL, "sink");

            check_int_eq(s1, RULES_FORGE_OK);
            check_int_eq(s2, RULES_FORGE_OK);
            check_int_eq(route_engine_source_count(engine), 1);
            check_int_eq(route_engine_sink_count(engine), 1);

            route_engine_destroy(engine);
        }

        it("should reject NULL arguments") {
            route_engine_t* engine = route_engine_create_empty();

            check_int_eq(route_engine_load_plugin(NULL, FILE_SOURCE_DLL, "source"),
                         RULES_FORGE_ERROR_INVALID_ARGUMENT);
            check_int_eq(route_engine_load_plugin(engine, NULL, "source"),
                         RULES_FORGE_ERROR_INVALID_ARGUMENT);
            check_int_eq(route_engine_load_plugin(engine, FILE_SOURCE_DLL, NULL),
                         RULES_FORGE_ERROR_INVALID_ARGUMENT);

            route_engine_destroy(engine);
        }

        it("should reject duplicate logical source names") {
            route_engine_t* engine = route_engine_create_empty();
            check(engine != NULL);

            check_int_eq(
                route_engine_load_plugin_ex(engine, "shared_name", FILE_SOURCE_DLL, "source", NULL),
                RULES_FORGE_OK);
            check_int_eq(
                route_engine_load_plugin_ex(engine, "shared_name", FILE_SOURCE_DLL, "source", NULL),
                RULES_FORGE_ERROR_INVALID_ARGUMENT);

            route_engine_destroy(engine);
        }

        it("should route CSV records from file_source plugin through the router") {
            const char* csv_path = "test_data_router/orders.csv";
            create_test_csv(csv_path, "quantity,unitPrice,finalPrice\n6,20,0.01\n");

            route_engine_config_t cfg = {
                .rules_file = ROUTING_RULES_FILE,
                .schema_file = NULL,
                .session_pool_size = 4,
                .max_rule_fires = 100,
                .drop_no_route = 0,
                .default_target = NULL,
                .plugins = NULL,
                .plugin_count = 0,
            };

            route_engine_t* engine = route_engine_create(&cfg);
            check(engine != NULL);
            g_plugin_sink_ctx = NULL;

            check_int_eq(
                route_engine_load_plugin_ex(
                    engine,
                    "orders_source",
                    FILE_SOURCE_DLL,
                    "source",
                    "{\"path\":\"test_data_router/orders.csv\",\"format\":\"csv\",\"skip_header\":true}"),
                RULES_FORGE_OK);

            check_int_eq(
                route_engine_register_sink(
                    engine,
                    "high_value_sink",
                    &plugin_test_sink_vtable,
                    NULL),
                RULES_FORGE_OK);

            ruleforge_status_t status = route_engine_run(
                engine, "orders_source", NULL, "com.example.pricing.Order", 5);
            check_int_eq(status, RULES_FORGE_OK);

            check(g_plugin_sink_ctx != NULL);
            check_int_eq(g_plugin_sink_ctx->push_count, 1);
            check_str_eq(g_plugin_sink_ctx->last_target, "high_value_sink");
            check_str_eq(g_plugin_sink_ctx->last_query, "");

            route_engine_destroy(engine);
            remove_test_file(csv_path);
        }
    }
}
