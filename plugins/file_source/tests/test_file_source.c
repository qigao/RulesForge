/**
 * @file test_file_source.c
 * @brief Unit tests for file_source plugin using tinytest
 */

#include "tinytest.h"
#include "turbo_fs.h"
#include <string.h>
#include "../file_source_plugin.c"

#define TEST_DATA_DIR "test_data"

static void create_test_csv(const char* filename, const char* content) {
    static int test_data_ready = 0;
    if (!test_data_ready) {
        turbo_fs_stat_t stat = {0};
        if (turbo_fs_stat(TEST_DATA_DIR, &stat) < 0) {
            turbo_fs_mkdir(TEST_DATA_DIR, 0755);
        }
        test_data_ready = 1;
    }
    turbo_fs_buf_t buf = turbo_fs_buf_init((char*)content, strlen(content));
    turbo_fs_write_file(filename, &buf);
}

static void remove_test_file(const char* filename) {
    turbo_fs_unlink(filename);
}

spec("File Source Plugin") {
    describe("Plugin Initialization") {
        it("should initialize with valid configuration") {
            void* ctx = NULL;
            const char* config = "{\"path\":\"data/test.csv\",\"filter\":\"value >= 100\"}";

            ruleforge_status_t status = file_source_init(config, &ctx);

            check(ctx != NULL);
            check_int_eq(status, RULES_FORGE_OK);

            file_source_ctx_t* fctx = (file_source_ctx_t*)ctx;
            check(fctx->line_buffer != NULL);
            check(fctx->read_buffer != NULL);
            check_size_eq(fctx->line_capacity, 8192);
            check_size_eq(fctx->read_capacity, 8192);
            check_int_eq(fctx->skip_header, 1);
            check_int_eq(fctx->format, DATA_BIND_FORMAT_CSV);
            check_str_eq(fctx->default_path, "data/test.csv");
            check_str_eq(fctx->filter_expr, "value >= 100");

            file_source_cleanup(ctx);
        }

        it("should initialize with NULL configuration") {
            void* ctx = NULL;

            ruleforge_status_t status = file_source_init(NULL, &ctx);

            check(ctx != NULL);
            check_int_eq(status, RULES_FORGE_OK);

            file_source_cleanup(ctx);
        }

        it("should reject malformed JSON configuration") {
            void* ctx = NULL;

            ruleforge_status_t status = file_source_init("{bad json", &ctx);

            check_int_eq(status, RULES_FORGE_ERROR_INVALID_ARGUMENT);
            check(ctx == NULL);
        }
    }

    describe("CSV File Reading") {
        it("should skip header when configured") {
            const char* test_file = TEST_DATA_DIR "/test_with_header.csv";
            create_test_csv(test_file, "id,name,value\n1,sensor1,100\n2,sensor2,200\n");

            void* ctx = NULL;
            file_source_init("{}", &ctx);

            file_source_ctx_t* fctx = (file_source_ctx_t*)ctx;
            fctx->skip_header = 1;
            fctx->format = DATA_BIND_FORMAT_CSV;

            DataBindFormat format;
            const uint8_t* data;
            size_t len;

            ruleforge_status_t status = file_source_fetch(ctx, test_file, "Sensor", &format, &data, &len);

            check_int_eq(status, RULES_FORGE_OK);
            check_int_eq(format, DATA_BIND_FORMAT_CSV);
            check(data != NULL);
            check(len > 0);
            check_str_eq((char*)data, "id,name,value\n1,sensor1,100");

            file_source_cleanup(ctx);
            remove_test_file(test_file);
        }

        it("should read first line without skipping header") {
            const char* test_file = TEST_DATA_DIR "/test_no_header.csv";
            create_test_csv(test_file, "1,sensor1,100\n2,sensor2,200\n");

            void* ctx = NULL;
            file_source_init("{}", &ctx);

            file_source_ctx_t* fctx = (file_source_ctx_t*)ctx;
            fctx->skip_header = 0;
            fctx->format = DATA_BIND_FORMAT_CSV;

            DataBindFormat format;
            const uint8_t* data;
            size_t len;

            ruleforge_status_t status = file_source_fetch(ctx, test_file, "Sensor", &format, &data, &len);

            check_int_eq(status, RULES_FORGE_OK);
            check(strncmp((char*)data, "1,sensor1,100", 13) == 0);

            file_source_cleanup(ctx);
            remove_test_file(test_file);
        }

        it("should read multiple lines sequentially") {
            const char* test_file = TEST_DATA_DIR "/test_multiline.csv";
            create_test_csv(test_file, "id,name\n1,first\n2,second\n3,third\n");

            void* ctx = NULL;
            file_source_init("{}", &ctx);

            file_source_ctx_t* fctx = (file_source_ctx_t*)ctx;
            fctx->skip_header = 1;
            fctx->format = DATA_BIND_FORMAT_CSV;

            DataBindFormat format;
            const uint8_t* data;
            size_t len;

            // First fetch
            ruleforge_status_t status1 = file_source_fetch(ctx, test_file, "Data", &format, &data, &len);
            check_int_eq(status1, RULES_FORGE_OK);
            char line1[256];
            strncpy(line1, (char*)data, len);
            line1[len] = '\0';

            // Second fetch
            ruleforge_status_t status2 = file_source_fetch(ctx, test_file, "Data", &format, &data, &len);
            check_int_eq(status2, RULES_FORGE_OK);
            char line2[256];
            strncpy(line2, (char*)data, len);
            line2[len] = '\0';

            // Third fetch
            ruleforge_status_t status3 = file_source_fetch(ctx, test_file, "Data", &format, &data, &len);
            check_int_eq(status3, RULES_FORGE_OK);
            char line3[256];
            strncpy(line3, (char*)data, len);
            line3[len] = '\0';

            check_int_eq(status1, RULES_FORGE_OK);
            check_int_eq(status2, RULES_FORGE_OK);
            check_int_eq(status3, RULES_FORGE_OK);

            check_str_eq(line1, "id,name\n1,first");
            check_str_eq(line2, "id,name\n2,second");
            check_str_eq(line3, "id,name\n3,third");

            file_source_cleanup(ctx);
            remove_test_file(test_file);
        }

        it("should use configured default path when query is NULL") {
            const char* test_file = TEST_DATA_DIR "/test_default_path.csv";
            create_test_csv(test_file, "id,name\n1,alice\n");

            void* ctx = NULL;
            file_source_init("{\"path\":\"test_data/test_default_path.csv\"}", &ctx);

            DataBindFormat format;
            const uint8_t* data;
            size_t len;

            ruleforge_status_t status = file_source_fetch(ctx, NULL, "Sensor", &format, &data, &len);

            check_int_eq(status, RULES_FORGE_OK);
            check_str_eq((char*)data, "id,name\n1,alice");

            file_source_cleanup(ctx);
            remove_test_file(test_file);
        }

        it("should reload state when query path changes") {
            const char* file_a = TEST_DATA_DIR "/test_switch_a.csv";
            const char* file_b = TEST_DATA_DIR "/test_switch_b.csv";
            create_test_csv(file_a, "id,name\n1,alpha\n");
            create_test_csv(file_b, "id,name\n2,beta\n");

            void* ctx = NULL;
            file_source_init("{}", &ctx);

            DataBindFormat format;
            const uint8_t* data;
            size_t len;

            ruleforge_status_t status_a = file_source_fetch(ctx, file_a, "Sensor", &format, &data, &len);
            check_int_eq(status_a, RULES_FORGE_OK);
            check_str_eq((char*)data, "id,name\n1,alpha");

            ruleforge_status_t status_b = file_source_fetch(ctx, file_b, "Sensor", &format, &data, &len);
            check_int_eq(status_b, RULES_FORGE_OK);
            check_str_eq((char*)data, "id,name\n2,beta");

            file_source_cleanup(ctx);
            remove_test_file(file_a);
            remove_test_file(file_b);
        }

        it("should filter CSV rows using configured DSV expression") {
            const char* test_file = TEST_DATA_DIR "/test_filter.csv";
            create_test_csv(test_file, "id_n,name_s,value_n\n1,alpha,50\n2,beta,200\n3,gamma,300\n");

            void* ctx = NULL;
            file_source_init(
                "{\"path\":\"test_data/test_filter.csv\",\"filter\":\"value_n >= 200\"}",
                &ctx);

            DataBindFormat format;
            const uint8_t* data;
            size_t len;

            ruleforge_status_t status1 = file_source_fetch(ctx, NULL, "Sensor", &format, &data, &len);
            check_int_eq(status1, RULES_FORGE_OK);
            check_str_eq((char*)data, "id_n,name_s,value_n\n2,beta,200");

            ruleforge_status_t status2 = file_source_fetch(ctx, NULL, "Sensor", &format, &data, &len);
            check_int_eq(status2, RULES_FORGE_OK);
            check_str_eq((char*)data, "id_n,name_s,value_n\n3,gamma,300");

            ruleforge_status_t status3 = file_source_fetch(ctx, NULL, "Sensor", &format, &data, &len);
            check_int_eq(status3, RULES_FORGE_STATUS_END_OF_STREAM);

            file_source_cleanup(ctx);
            remove_test_file(test_file);
        }
    }

    describe("Error Handling") {
        it("should return error for non-existent file") {
            void* ctx = NULL;
            file_source_init("{}", &ctx);

            DataBindFormat format;
            const uint8_t* data;
            size_t len;

            ruleforge_status_t status = file_source_fetch(ctx, "nonexistent.csv", "Data", &format, &data, &len);

            check_int_eq(status, RULES_FORGE_ERROR_GENERIC);

            file_source_cleanup(ctx);
        }

        it("should return error for empty file") {
            const char* test_file = TEST_DATA_DIR "/test_empty.csv";
            create_test_csv(test_file, "");

            void* ctx = NULL;
            file_source_init("{}", &ctx);

            DataBindFormat format;
            const uint8_t* data;
            size_t len;

            ruleforge_status_t status = file_source_fetch(ctx, test_file, "Data", &format, &data, &len);

            check_int_eq(status, RULES_FORGE_STATUS_END_OF_STREAM);

            file_source_cleanup(ctx);
            remove_test_file(test_file);
        }

        it("should return error for file with only header") {
            const char* test_file = TEST_DATA_DIR "/test_header_only.csv";
            create_test_csv(test_file, "id,name,value\n");

            void* ctx = NULL;
            file_source_init("{}", &ctx);

            file_source_ctx_t* fctx = (file_source_ctx_t*)ctx;
            fctx->skip_header = 1;

            DataBindFormat format;
            const uint8_t* data;
            size_t len;

            ruleforge_status_t status = file_source_fetch(ctx, test_file, "Data", &format, &data, &len);

            check_int_eq(status, RULES_FORGE_STATUS_END_OF_STREAM);

            file_source_cleanup(ctx);
            remove_test_file(test_file);
        }
    }

    describe("Memory Management") {
        it("should cleanup without crash") {
            void* ctx = NULL;
            file_source_init("{}", &ctx);

            file_source_ctx_t* fctx = (file_source_ctx_t*)ctx;
            check(fctx->line_buffer != NULL);

            file_source_cleanup(ctx);

            check(1); // If we reach here, cleanup succeeded
        }

        it("should cleanup with loaded file") {
            const char* test_file = TEST_DATA_DIR "/test_cleanup.csv";
            create_test_csv(test_file, "1,test\n");

            void* ctx = NULL;
            file_source_init("{}", &ctx);

            DataBindFormat format;
            const uint8_t* data;
            size_t len;

            file_source_fetch(ctx, test_file, "Data", &format, &data, &len);

            file_source_ctx_t* fctx = (file_source_ctx_t*)ctx;
            check_str_eq(fctx->active_path, test_file);

            file_source_cleanup(ctx);

            remove_test_file(test_file);
            check(1); // If we reach here, cleanup succeeded
        }
    }

    describe("Free Data Operation") {
        it("should be no-op for context buffer data") {
            const char* test_file = TEST_DATA_DIR "/test_free.csv";
            create_test_csv(test_file, "1,test\n");

            void* ctx = NULL;
            file_source_init("{}", &ctx);

            DataBindFormat format;
            const uint8_t* data;
            size_t len;

            file_source_fetch(ctx, test_file, "Data", &format, &data, &len);

            file_source_free_data(ctx, data);

            check(data != NULL); // Data should still be valid

            file_source_cleanup(ctx);
            remove_test_file(test_file);
        }
    }

    describe("Newline Handling") {
        it("should handle Unix line endings") {
            const char* test_file = TEST_DATA_DIR "/test_newlines.csv";
            create_test_csv(test_file, "line1\nline2\nline3\n");

            void* ctx = NULL;
            file_source_init("{}", &ctx);

            file_source_ctx_t* fctx = (file_source_ctx_t*)ctx;
            fctx->skip_header = 0;

            DataBindFormat format;
            const uint8_t* data;
            size_t len;

            file_source_fetch(ctx, test_file, "Data", &format, &data, &len);

            check_size_eq(len, 5); // "line1" without \n
            check(data[len - 1] != '\n');
            check(strncmp((char*)data, "line1", 5) == 0);

            file_source_cleanup(ctx);
            remove_test_file(test_file);
        }
    }
}
