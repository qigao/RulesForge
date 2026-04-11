/**
 * @file test_http_sink.c
 * @brief Unit tests for http_sink plugin using tinytest
 */

#include "tinytest.h"
#include "http_client.h"
#include "http_common.h"
#include <string.h>
#include "../http_sink_plugin.c"

spec("HTTP Sink Plugin") {
    describe("Plugin Initialization") {
        it("should initialize with valid configuration") {
            void* ctx = NULL;
            const char* config = "{\"base_url\":\"http://localhost:8080\",\"endpoint\":\"/api/facts\"}";

            ruleforge_status_t status = http_sink_init(config, &ctx);

            check(ctx != NULL);
            check_int_eq(status, RULES_FORGE_OK);

            http_sink_ctx_t* sctx = (http_sink_ctx_t*)ctx;
            check(sctx->client != NULL);
            check_str_eq(http_client_get_base_url(sctx->client), "http://localhost:8080");
            check(strncmp(sctx->endpoint, "/api/facts", 10) == 0);

            http_sink_cleanup(ctx);
        }

        it("should initialize with NULL configuration") {
            void* ctx = NULL;

            ruleforge_status_t status = http_sink_init(NULL, &ctx);

            check(ctx != NULL);
            check_int_eq(status, RULES_FORGE_OK);

            http_sink_cleanup(ctx);
        }

        it("should honor JSON configuration fields") {
            void* ctx = NULL;
            const char* config =
                "{"
                "\"base_url\":\"http://example.com:9000\","
                "\"endpoint\":\"/ingest\","
                "\"timeout_ms\":1500,"
                "\"connect_timeout_ms\":250,"
                "\"read_timeout_ms\":500,"
                "\"follow_redirects\":false,"
                "\"compression\":true"
                "}";

            ruleforge_status_t status = http_sink_init(config, &ctx);
            check_int_eq(status, RULES_FORGE_OK);
            check(ctx != NULL);

            http_sink_ctx_t* sctx = (http_sink_ctx_t*)ctx;
            check_str_eq(http_client_get_base_url(sctx->client), "http://example.com:9000");
            check_str_eq(sctx->endpoint, "/ingest");

            http_sink_cleanup(ctx);
        }

        it("should reject malformed JSON configuration") {
            void* ctx = NULL;
            ruleforge_status_t status = http_sink_init("{bad json", &ctx);

            check_int_eq(status, RULES_FORGE_ERROR_INVALID_ARGUMENT);
            check(ctx == NULL);
        }
    }

    describe("Push Validation") {
        it("should return error for NULL route envelope") {
            void* ctx = NULL;
            http_sink_init(NULL, &ctx);

            ruleforge_status_t status = http_sink_push_route(ctx, NULL);

            check_int_eq(status, RULES_FORGE_ERROR_INVALID_ARGUMENT);

            http_sink_cleanup(ctx);
        }

        it("should attempt delivery with empty effective metadata") {
            void* ctx = NULL;
            http_sink_init(NULL, &ctx);

            ruleforge_route_envelope_t route = {
                .effective_metadata = "",
            };
            ruleforge_status_t status = http_sink_push_route(ctx, &route);

            check(status == RULES_FORGE_OK || status == RULES_FORGE_ERROR_GENERIC);

            http_sink_cleanup(ctx);
        }
    }

    describe("Memory Management") {
        it("should cleanup without crash") {
            void* ctx = NULL;
            http_sink_init(NULL, &ctx);

            http_sink_ctx_t* sctx = (http_sink_ctx_t*)ctx;
            check(sctx->client != NULL);

            http_sink_cleanup(ctx);
            check(1); // If we reach here, cleanup succeeded
        }

        it("should cleanup NULL client gracefully") {
            void* ctx = NULL;
            http_sink_init(NULL, &ctx);

            http_sink_ctx_t* sctx = (http_sink_ctx_t*)ctx;
            http_client_destroy(sctx->client);
            sctx->client = NULL;

            // cleanup should handle NULL client
            http_sink_cleanup(ctx);
            check(1);
        }
    }

    describe("Vtable") {
        it("should expose valid vtable") {
            const ruleforge_datasink_vtable_t* vtable = ruleforge_get_sink_vtable();

            check(vtable != NULL);
            check(vtable->init != NULL);
            check(vtable->push_route != NULL);
            check(vtable->cleanup != NULL);
        }
    }
}
