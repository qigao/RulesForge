/**
 * @file file_source_plugin.c
 * @brief File DataSource plugin using turbo_fs and turbo_parser
 */

#include "rule_forge_plugin.h"
#include "../plugin_config.h"
#include "platform.h"
#include "turbo_fs.h"
#include <stdlib.h>
#include <string.h>
#include <turbo_parser.h>

typedef struct {
    char* file_data;
    size_t file_size;
    DataBindFormat format;
    int skip_header;
    char* line_buffer;
    size_t line_capacity;
    char* emit_buffer;
    size_t emit_capacity;
    char* read_buffer;
    size_t read_capacity;
    size_t buffered_length;
    char default_path[512];
    char active_path[512];
    char filter_expr[512];
    char* csv_header;
    size_t csv_header_length;
    turbo_csv_stream_processor_t* csv_stream;
    turbo_file_t file_handle;
    int header_consumed;
    int end_of_file;
    int json_emitted;
} file_source_ctx_t;

static int copy_string_or_truncate(char* dst, size_t dst_size, const char* src) {
    size_t len;

    if (!dst || dst_size == 0 || !src) {
        return 0;
    }

    len = strlen(src);
    if (len >= dst_size) {
        return 0;
    }

    memcpy(dst, src, len + 1);
    return 1;
}

static ruleforge_status_t ensure_buffer_capacity(char** buffer,
                                                 size_t* capacity,
                                                 size_t needed) {
    char* new_buffer;
    size_t new_capacity = *capacity;

    if (needed + 1 <= *capacity) {
        return RULES_FORGE_OK;
    }

    while (new_capacity <= needed) {
        new_capacity *= 2;
    }

    new_buffer = realloc(*buffer, new_capacity);
    if (!new_buffer) {
        return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
    }

    *buffer = new_buffer;
    *capacity = new_capacity;
    return RULES_FORGE_OK;
}

static ruleforge_status_t ensure_line_capacity(file_source_ctx_t* ctx, size_t needed) {
    return ensure_buffer_capacity(&ctx->line_buffer, &ctx->line_capacity, needed);
}

static ruleforge_status_t ensure_emit_capacity(file_source_ctx_t* ctx, size_t needed) {
    return ensure_buffer_capacity(&ctx->emit_buffer, &ctx->emit_capacity, needed);
}

static ruleforge_status_t ensure_read_capacity(file_source_ctx_t* ctx, size_t needed) {
    return ensure_buffer_capacity(&ctx->read_buffer, &ctx->read_capacity, needed);
}

static int csv_uses_stream_processor(const file_source_ctx_t* ctx) {
    return ctx && (ctx->skip_header || ctx->filter_expr[0]);
}

static void reset_active_input(file_source_ctx_t* ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->csv_stream) {
        turbo_csv_stream_processor_destroy(ctx->csv_stream);
        ctx->csv_stream = NULL;
    }
    if (ctx->file_handle != TURBO_INVALID_FILE) {
        turbo_fs_close(ctx->file_handle);
        ctx->file_handle = TURBO_INVALID_FILE;
    }
    if (ctx->file_data) {
        turbo_fs_buf_t buf = turbo_fs_buf_init(ctx->file_data, ctx->file_size);
        turbo_fs_buf_free(&buf);
        ctx->file_data = NULL;
        ctx->file_size = 0;
    }

    ctx->buffered_length = 0;
    ctx->header_consumed = 0;
    ctx->end_of_file = 0;
    ctx->json_emitted = 0;
    ctx->csv_header_length = 0;
    if (ctx->csv_header) {
        ctx->csv_header[0] = '\0';
    }
    ctx->active_path[0] = '\0';
}

static ruleforge_status_t copy_current_line(file_source_ctx_t* ctx,
                                            const char* line,
                                            size_t len) {
    if (ensure_line_capacity(ctx, len) != RULES_FORGE_OK) {
        return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
    }

    if (len > 0) {
        memcpy(ctx->line_buffer, line, len);
    }
    ctx->line_buffer[len] = '\0';
    return RULES_FORGE_OK;
}

static ruleforge_status_t set_csv_header(file_source_ctx_t* ctx,
                                         const char* header,
                                         size_t len) {
    char* new_header = NULL;

    if (!ctx) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    new_header = (char*)realloc(ctx->csv_header, len + 1);
    if (!new_header) {
        return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
    }

    ctx->csv_header = new_header;
    if (len > 0) {
        memcpy(ctx->csv_header, header, len);
    }
    ctx->csv_header[len] = '\0';
    ctx->csv_header_length = len;
    return RULES_FORGE_OK;
}

static ruleforge_status_t build_csv_document(file_source_ctx_t* ctx,
                                             const char* row,
                                             size_t row_len,
                                             const uint8_t** out_data,
                                             size_t* out_len) {
    size_t total_len;

    if (!ctx || !out_data || !out_len) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    if (!ctx->csv_header || ctx->csv_header_length == 0) {
        *out_data = (const uint8_t*)row;
        *out_len = row_len;
        return RULES_FORGE_OK;
    }

    total_len = ctx->csv_header_length + 1 + row_len;
    if (ensure_emit_capacity(ctx, total_len) != RULES_FORGE_OK) {
        return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
    }

    memcpy(ctx->emit_buffer, ctx->csv_header, ctx->csv_header_length);
    ctx->emit_buffer[ctx->csv_header_length] = '\n';
    if (row_len > 0) {
        memcpy(ctx->emit_buffer + ctx->csv_header_length + 1, row, row_len);
    }
    ctx->emit_buffer[total_len] = '\0';

    *out_data = (const uint8_t*)ctx->emit_buffer;
    *out_len = total_len;
    return RULES_FORGE_OK;
}

static ruleforge_status_t resolve_path(file_source_ctx_t* ctx,
                                       const char* query,
                                       char* out_path,
                                       size_t out_path_size) {
    const char* path = query;

    if ((!path || !path[0]) && ctx->default_path[0]) {
        path = ctx->default_path;
    }
    if (!path || !path[0]) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    if (!copy_string_or_truncate(out_path, out_path_size, path)) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    return RULES_FORGE_OK;
}

static ruleforge_status_t ensure_json_loaded(file_source_ctx_t* ctx, const char* path) {
    turbo_fs_buf_t buf;
    int ret;

    if (ctx->file_data && strcmp(ctx->active_path, path) == 0) {
        return RULES_FORGE_OK;
    }

    reset_active_input(ctx);

    ret = turbo_fs_read_file(path, &buf);
    if (ret < 0) {
        return RULES_FORGE_ERROR_GENERIC;
    }

    ctx->file_data = buf.base;
    ctx->file_size = buf.len;
    if (!copy_string_or_truncate(ctx->active_path, sizeof(ctx->active_path), path)) {
        reset_active_input(ctx);
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }
    return RULES_FORGE_OK;
}

static ruleforge_status_t ensure_csv_open(file_source_ctx_t* ctx, const char* path) {
    if (ctx->file_handle != TURBO_INVALID_FILE && strcmp(ctx->active_path, path) == 0) {
        return RULES_FORGE_OK;
    }

    reset_active_input(ctx);

    ctx->file_handle = turbo_fs_open(path, TURBO_FS_O_RDONLY, TURBO_FS_DEFAULT_MODE);
    if (ctx->file_handle == TURBO_INVALID_FILE) {
        return RULES_FORGE_ERROR_GENERIC;
    }

    if (!copy_string_or_truncate(ctx->active_path, sizeof(ctx->active_path), path)) {
        reset_active_input(ctx);
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    if (csv_uses_stream_processor(ctx)) {
        ctx->csv_stream = turbo_csv_stream_processor_create(NULL);
        if (!ctx->csv_stream) {
            reset_active_input(ctx);
            return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
        }
        if (ctx->filter_expr[0] &&
            !turbo_csv_stream_processor_set_filter(ctx->csv_stream, ctx->filter_expr)) {
            reset_active_input(ctx);
            return RULES_FORGE_ERROR_INVALID_ARGUMENT;
        }
    }

    return RULES_FORGE_OK;
}

static ruleforge_status_t read_next_csv_line(file_source_ctx_t* ctx, size_t* out_len) {
    for (;;) {
        char* newline = NULL;
        size_t raw_len = 0;
        size_t consumed = 0;
        size_t effective_len = 0;

        if (ctx->buffered_length > 0) {
            newline = (char*)memchr(ctx->read_buffer, '\n', ctx->buffered_length);
        }

        if (newline) {
            raw_len = (size_t)(newline - ctx->read_buffer);
            consumed = raw_len + 1;
        } else if (ctx->end_of_file && ctx->buffered_length > 0) {
            raw_len = ctx->buffered_length;
            consumed = ctx->buffered_length;
        } else if (ctx->end_of_file) {
            return RULES_FORGE_STATUS_END_OF_STREAM;
        } else {
            int read_len;
            ruleforge_status_t status = ensure_read_capacity(ctx, ctx->buffered_length + ctx->read_capacity);
            if (status != RULES_FORGE_OK) {
                return status;
            }

            read_len = turbo_fs_read(
                ctx->file_handle,
                ctx->read_buffer + ctx->buffered_length,
                ctx->read_capacity - ctx->buffered_length);
            if (read_len < 0) {
                return RULES_FORGE_ERROR_GENERIC;
            }
            if (read_len == 0) {
                ctx->end_of_file = 1;
            } else {
                ctx->buffered_length += (size_t)read_len;
            }
            continue;
        }

        effective_len = raw_len;
        if (effective_len > 0 && ctx->read_buffer[effective_len - 1] == '\r') {
            effective_len--;
        }

        if (copy_current_line(ctx, ctx->read_buffer, effective_len) != RULES_FORGE_OK) {
            return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
        }

        ctx->buffered_length -= consumed;
        if (ctx->buffered_length > 0) {
            memmove(ctx->read_buffer, ctx->read_buffer + consumed, ctx->buffered_length);
        }

        *out_len = effective_len;
        return RULES_FORGE_OK;
    }
}

static ruleforge_status_t feed_csv_stream_line(file_source_ctx_t* ctx, size_t len) {
    const char* err;

    turbo_csv_stream_processor_feed(ctx->line_buffer, len, ctx->csv_stream);
    turbo_csv_stream_processor_feed("\n", 1, ctx->csv_stream);

    err = turbo_csv_stream_processor_error(ctx->csv_stream);
    if (err && err[0]) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    return RULES_FORGE_OK;
}

static ruleforge_status_t parse_config_json(file_source_ctx_t* ctx, const char* config_json) {
    ruleforge_plugin_json_config_t config = {0};
    const char* text;
    size_t requested_capacity;
    ruleforge_status_t status;

    status = ruleforge_plugin_json_config_open(config_json, &config);
    if (status != RULES_FORGE_OK) {
        return status;
    }

    text = ruleforge_plugin_json_config_get_string(&config, "path", NULL);
    if (text) {
        (void)copy_string_or_truncate(ctx->default_path, sizeof(ctx->default_path), text);
    }

    text = ruleforge_plugin_json_config_get_string(&config, "format", NULL);
    if (text && strcmp(text, "json") == 0) {
        ctx->format = DATA_BIND_FORMAT_JSON;
    } else if (text && strcmp(text, "csv") == 0) {
        ctx->format = DATA_BIND_FORMAT_CSV;
    }

    ctx->skip_header = ruleforge_plugin_json_config_get_bool(&config, "skip_header", ctx->skip_header);

    requested_capacity = (size_t)ruleforge_plugin_json_config_get_int(
        &config, "buffer_size", (int)ctx->line_capacity);
    if (requested_capacity > ctx->line_capacity) {
        char* new_buffer = realloc(ctx->line_buffer, requested_capacity);
        if (new_buffer) {
            ctx->line_buffer = new_buffer;
            ctx->line_capacity = requested_capacity;
        }
    }
    if (requested_capacity > ctx->emit_capacity) {
        char* new_emit_buffer = realloc(ctx->emit_buffer, requested_capacity);
        if (new_emit_buffer) {
            ctx->emit_buffer = new_emit_buffer;
            ctx->emit_capacity = requested_capacity;
        }
    }
    if (requested_capacity > ctx->read_capacity) {
        char* new_read_buffer = realloc(ctx->read_buffer, requested_capacity);
        if (new_read_buffer) {
            ctx->read_buffer = new_read_buffer;
            ctx->read_capacity = requested_capacity;
        }
    }

    text = ruleforge_plugin_json_config_get_string(&config, "filter", NULL);
    if (text) {
        (void)copy_string_or_truncate(ctx->filter_expr, sizeof(ctx->filter_expr), text);
    }

    ruleforge_plugin_json_config_close(&config);
    return RULES_FORGE_OK;
}

static ruleforge_status_t file_source_init(const char* config_json, void** out_ctx) {
    file_source_ctx_t* ctx = calloc(1, sizeof(file_source_ctx_t));
    if (!ctx) return RULES_FORGE_ERROR_MEMORY_ALLOCATION;

    ctx->format = DATA_BIND_FORMAT_CSV;
    ctx->skip_header = 1;
    ctx->line_capacity = 8192;
    ctx->emit_capacity = 8192;
    ctx->read_capacity = 8192;
    ctx->file_handle = TURBO_INVALID_FILE;
    ctx->line_buffer = malloc(ctx->line_capacity);
    ctx->emit_buffer = malloc(ctx->emit_capacity);
    ctx->read_buffer = malloc(ctx->read_capacity);

    if (!ctx->line_buffer || !ctx->emit_buffer || !ctx->read_buffer) {
        free(ctx->emit_buffer);
        free(ctx->read_buffer);
        free(ctx->line_buffer);
        free(ctx);
        return RULES_FORGE_ERROR_MEMORY_ALLOCATION;
    }

    if (parse_config_json(ctx, config_json) != RULES_FORGE_OK) {
        free(ctx->emit_buffer);
        free(ctx->read_buffer);
        free(ctx->line_buffer);
        free(ctx);
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    *out_ctx = ctx;
    return RULES_FORGE_OK;
}

static ruleforge_status_t file_source_fetch(
    void* ctx_ptr,
    const char* query,
    const char* fact_type,
    DataBindFormat* out_format,
    const uint8_t** out_data,
    size_t* out_len) {

    file_source_ctx_t* ctx = (file_source_ctx_t*)ctx_ptr;
    ruleforge_status_t status;

    (void)fact_type;

    if (!ctx || !out_format || !out_data || !out_len) {
        return RULES_FORGE_ERROR_INVALID_ARGUMENT;
    }

    if (ctx->format == DATA_BIND_FORMAT_JSON) {
        char path[512];
        status = resolve_path(ctx, query, path, sizeof(path));
        if (status != RULES_FORGE_OK) {
            return status;
        }

        status = ensure_json_loaded(ctx, path);
        if (status != RULES_FORGE_OK) {
            return status;
        }

        if (ctx->json_emitted || !ctx->file_data || ctx->file_size == 0) {
            return RULES_FORGE_STATUS_END_OF_STREAM;
        }

        ctx->json_emitted = 1;
        *out_format = ctx->format;
        *out_data = (const uint8_t*)ctx->file_data;
        *out_len = ctx->file_size;
        return RULES_FORGE_OK;
    }

    {
        char path[512];
        status = resolve_path(ctx, query, path, sizeof(path));
        if (status != RULES_FORGE_OK) {
            return status;
        }

        status = ensure_csv_open(ctx, path);
        if (status != RULES_FORGE_OK) {
            return status;
        }
    }

    for (;;) {
        size_t line_len = 0;

        status = read_next_csv_line(ctx, &line_len);
        if (status != RULES_FORGE_OK) {
            return status;
        }

        if (line_len == 0) {
            continue;
        }

        if (csv_uses_stream_processor(ctx)) {
            if (!ctx->header_consumed) {
                status = set_csv_header(ctx, ctx->line_buffer, line_len);
                if (status != RULES_FORGE_OK) {
                    return status;
                }
                ctx->header_consumed = 1;
                status = feed_csv_stream_line(ctx, line_len);
                if (status != RULES_FORGE_OK) {
                    return status;
                }
                continue;
            }

            {
                size_t before = turbo_csv_stream_processor_row_count(ctx->csv_stream);
                status = feed_csv_stream_line(ctx, line_len);
                if (status != RULES_FORGE_OK) {
                    return status;
                }
                if (turbo_csv_stream_processor_row_count(ctx->csv_stream) == before) {
                    continue;
                }
            }
        }

        *out_format = ctx->format;
        if (csv_uses_stream_processor(ctx)) {
            return build_csv_document(ctx, ctx->line_buffer, line_len, out_data, out_len);
        }
        *out_data = (const uint8_t*)ctx->line_buffer;
        *out_len = line_len;
        return RULES_FORGE_OK;
    }

}

static void file_source_free_data(void* ctx, const uint8_t* data) {
    (void)ctx;
    (void)data;
}

static void file_source_cleanup(void* ctx_ptr) {
    file_source_ctx_t* ctx = (file_source_ctx_t*)ctx_ptr;

    if (!ctx) {
        return;
    }

    reset_active_input(ctx);
    free(ctx->line_buffer);
    free(ctx->emit_buffer);
    free(ctx->read_buffer);
    free(ctx->csv_header);
    free(ctx);
}

static const ruleforge_datasource_vtable_t file_source_vtable = {
    .init = file_source_init,
    .fetch = file_source_fetch,
    .free_data = file_source_free_data,
    .cleanup = file_source_cleanup
};

CXX_C_API const ruleforge_datasource_vtable_t* ruleforge_get_source_vtable(void) {
    return &file_source_vtable;
}
