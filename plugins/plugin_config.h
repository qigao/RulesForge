/**
 * @file plugin_config.h
 * @brief Small shared helpers for plugin JSON configuration parsing
 */

#ifndef RULEFORGE_PLUGIN_CONFIG_H
#define RULEFORGE_PLUGIN_CONFIG_H

#include "ruleforge_types.h"
#include <string.h>
#include <turbo_parser.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    json_value_t* root;
} ruleforge_plugin_json_config_t;

static inline ruleforge_status_t ruleforge_plugin_json_config_open(
    const char* config_json,
    ruleforge_plugin_json_config_t* out_config) {

    if (!out_config) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    out_config->root = NULL;
    if (!config_json || !config_json[0]) {
        return RULES_FORGE_OK;
    }

    if (turbo_parse_json((const uint8_t*)config_json, strlen(config_json), &out_config->root) != 0 ||
        !out_config->root) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    return RULES_FORGE_OK;
}

static inline void ruleforge_plugin_json_config_close(
    ruleforge_plugin_json_config_t* config) {
    if (!config || !config->root) {
        return;
    }

    turbo_free_json(&config->root);
    config->root = NULL;
}

static inline const char* ruleforge_plugin_json_config_get_string(
    const ruleforge_plugin_json_config_t* config,
    const char* key,
    const char* default_value) {
    const char* value;

    if (!config || !config->root || !key) {
        return default_value;
    }

    value = turbo_json_get_string(config->root, key);
    return value ? value : default_value;
}

static inline int ruleforge_plugin_json_config_get_int(
    const ruleforge_plugin_json_config_t* config,
    const char* key,
    int default_value) {
    if (!config || !config->root || !key) {
        return default_value;
    }

    return turbo_json_get_int(config->root, key, default_value);
}

static inline int ruleforge_plugin_json_config_get_bool(
    const ruleforge_plugin_json_config_t* config,
    const char* key,
    int default_value) {
    if (!config || !config->root || !key) {
        return default_value;
    }

    return turbo_json_get_bool(config->root, key, default_value != 0) ? 1 : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* RULEFORGE_PLUGIN_CONFIG_H */
