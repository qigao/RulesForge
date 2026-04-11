/**
 * @file http_sink_plugin.c
 * @brief HTTP DataSink plugin - POSTs fact JSON to a REST endpoint
 */

#include "rule_forge_plugin.h"
#include "../plugin_config.h"
#include "http_client.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    http_client_t* client;
    char* endpoint;
} http_sink_ctx_t;



static ruleforge_status_t http_sink_post_body(http_sink_ctx_t* ctx, const char* body) {
    if (!ctx || !body) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    http_response_t* resp = http_post_json(ctx->client, ctx->endpoint, body);
    if (!resp) return RULES_FORGE_ERROR_GENERIC;

    int ok = (resp->status_code >= 200 && resp->status_code < 300);
    http_response_free(resp);

    return ok ? RULES_FORGE_OK : RULES_FORGE_ERROR_GENERIC;
}

static ruleforge_status_t http_sink_init(const char* config_json, void** out_ctx) {
    http_sink_ctx_t* ctx = calloc(1, sizeof(http_sink_ctx_t));
    if (!ctx) return RULES_FORGE_ERROR_MEMORY_ALLOCATION;

    ruleforge_plugin_json_config_t config = {0};
    ruleforge_status_t status = ruleforge_plugin_json_config_open(config_json, &config);
    if (status != RULES_FORGE_OK) {
        free(ctx);
        return status;
    }

    const char* base_url = ruleforge_plugin_json_config_get_string(&config, "base_url", "http://localhost:8080");
    const char* endpoint = ruleforge_plugin_json_config_get_string(&config, "endpoint", "/api/facts");
    const char* user_agent = ruleforge_plugin_json_config_get_string(&config, "user_agent", NULL);
    const char* bearer_token = ruleforge_plugin_json_config_get_string(&config, "bearer_token", NULL);
    int timeout_ms = ruleforge_plugin_json_config_get_int(&config, "timeout_ms", 0);
    int connect_timeout_ms = ruleforge_plugin_json_config_get_int(&config, "connect_timeout_ms", 0);
    int read_timeout_ms = ruleforge_plugin_json_config_get_int(&config, "read_timeout_ms", 0);
    int follow_redirects = ruleforge_plugin_json_config_get_bool(&config, "follow_redirects", 1);
    int compression = ruleforge_plugin_json_config_get_bool(&config, "compression", 0);

    ctx->endpoint = strdup(endpoint);
    if (!ctx->endpoint) {
        status = RULES_FORGE_ERROR_MEMORY_ALLOCATION;
        goto error;
    }

    ctx->client = http_client_create(base_url);
    if (!ctx->client) {
        status = RULES_FORGE_ERROR_GENERIC;
        goto error;
    }

    if (timeout_ms > 0) http_client_set_timeout(ctx->client, timeout_ms);
    if (connect_timeout_ms > 0) http_client_set_connect_timeout(ctx->client, connect_timeout_ms);
    if (read_timeout_ms > 0) http_client_set_read_timeout(ctx->client, read_timeout_ms);
    if (user_agent && user_agent[0]) http_client_set_user_agent(ctx->client, user_agent);
    if (bearer_token && bearer_token[0]) http_client_set_bearer_token(ctx->client, bearer_token);
    
    http_client_follow_redirects(ctx->client, follow_redirects);
    http_client_enable_compression(ctx->client, compression);
    http_client_set_default_header(ctx->client, "Content-Type", "application/json");

    ruleforge_plugin_json_config_close(&config);
    *out_ctx = ctx;
    return RULES_FORGE_OK;

error:
    ruleforge_plugin_json_config_close(&config);
    if (ctx->endpoint) free(ctx->endpoint);
    if (ctx->client) http_client_destroy(ctx->client);
    free(ctx);
    return status;
}

static ruleforge_status_t http_sink_push_route(
    void* ctx_ptr,
    const ruleforge_route_envelope_t* route) {

    http_sink_ctx_t* ctx = (http_sink_ctx_t*)ctx_ptr;
    if (!route) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    return http_sink_post_body(ctx, route->effective_metadata);
}

static void http_sink_cleanup(void* ctx_ptr) {
    http_sink_ctx_t* ctx = (http_sink_ctx_t*)ctx_ptr;
    if (!ctx) return;
    
    if (ctx->client) http_client_destroy(ctx->client);
    if (ctx->endpoint) free(ctx->endpoint);
    free(ctx);
}

static const ruleforge_datasink_vtable_t http_sink_vtable = {
    .init       = http_sink_init,
    .push_route = http_sink_push_route,
    .cleanup    = http_sink_cleanup
};

CXX_C_API const ruleforge_datasink_vtable_t* ruleforge_get_sink_vtable(void) {
    return &http_sink_vtable;
}
