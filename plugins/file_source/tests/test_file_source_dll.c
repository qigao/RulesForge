/**
 * @file test_file_source_dll.c
 * @brief BDD tests for file_source plugin via DLL loading
 */

#include "tinytest.h"
#include "turbo_fs.h"
#include "rule_forge_plugin.h"
#include <string.h>

#ifdef _WIN32
#include <windows.h>
typedef HMODULE dll_handle_t;
#define dll_open(path)    LoadLibraryA(path)
#define dll_sym(h, name)  GetProcAddress(h, name)
#define dll_close(h)      FreeLibrary(h)
#define DLL_NULL          NULL
#else
#include <dlfcn.h>
typedef void* dll_handle_t;
#define dll_open(path)    dlopen(path, RTLD_LAZY)
#define dll_sym(h, name)  dlsym(h, name)
#define dll_close(h)      dlclose(h)
#define DLL_NULL          NULL
#endif

#ifndef FILE_SOURCE_DLL
#ifdef _WIN32
#define FILE_SOURCE_DLL "file_source_plugin.dll"
#else
#define FILE_SOURCE_DLL "libfile_source_plugin.so"
#endif
#endif

#define TEST_DATA_DIR "test_data_dll"

typedef const ruleforge_datasource_vtable_t* (*get_vtable_fn)(void);

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

spec("File Source Plugin DLL") {
    describe("DLL Loading") {
        it("should load the DLL successfully") {
            dll_handle_t h = dll_open(FILE_SOURCE_DLL);
            check(h != DLL_NULL);
            if (h) dll_close(h);
        }

        it("should export ruleforge_get_source_vtable") {
            dll_handle_t h = dll_open(FILE_SOURCE_DLL);
            check(h != DLL_NULL);

            get_vtable_fn get_vtable = (get_vtable_fn)dll_sym(h, "ruleforge_get_source_vtable");
            check(get_vtable != NULL);

            dll_close(h);
        }
    }

    describe("Vtable via DLL") {
        it("should return valid vtable with all function pointers") {
            dll_handle_t h = dll_open(FILE_SOURCE_DLL);
            check(h != DLL_NULL);

            get_vtable_fn get_vtable = (get_vtable_fn)dll_sym(h, "ruleforge_get_source_vtable");
            check(get_vtable != NULL);

            const ruleforge_datasource_vtable_t* vtable = get_vtable();
            check(vtable != NULL);
            check(vtable->init != NULL);
            check(vtable->fetch != NULL);
            check(vtable->free_data != NULL);
            check(vtable->cleanup != NULL);

            dll_close(h);
        }
    }

    describe("Plugin Lifecycle via DLL") {
        it("should init and cleanup via vtable") {
            dll_handle_t h = dll_open(FILE_SOURCE_DLL);
            check(h != DLL_NULL);

            get_vtable_fn get_vtable = (get_vtable_fn)dll_sym(h, "ruleforge_get_source_vtable");
            const ruleforge_datasource_vtable_t* vtable = get_vtable();

            void* ctx = NULL;
            ruleforge_status_t status = vtable->init(NULL, &ctx);

            check_int_eq(status, RULES_FORGE_OK);
            check(ctx != NULL);

            vtable->cleanup(ctx);
            dll_close(h);
        }

        it("should fetch CSV lines via vtable") {
            const char* test_file = TEST_DATA_DIR "/dll_test.csv";
            create_test_csv(test_file, "id,name\n1,alice\n2,bob\n");

            dll_handle_t h = dll_open(FILE_SOURCE_DLL);
            check(h != DLL_NULL);

            get_vtable_fn get_vtable = (get_vtable_fn)dll_sym(h, "ruleforge_get_source_vtable");
            const ruleforge_datasource_vtable_t* vtable = get_vtable();

            void* ctx = NULL;
            vtable->init(NULL, &ctx);

            DataBindFormat data_format;
            const uint8_t* data;
            size_t len;

            // First fetch skips header, returns "1,alice"
            ruleforge_status_t s1 = vtable->fetch(ctx, test_file, "Data", &data_format, &data, &len);
            check_int_eq(s1, RULES_FORGE_OK);
            check_str_eq((char*)data, "id,name\n1,alice");

            // Second fetch returns "2,bob"
            ruleforge_status_t s2 = vtable->fetch(ctx, test_file, "Data", &data_format, &data, &len);
            check_int_eq(s2, RULES_FORGE_OK);
            check_str_eq((char*)data, "id,name\n2,bob");

            // Third fetch returns clean end-of-stream
            ruleforge_status_t s3 = vtable->fetch(ctx, test_file, "Data", &data_format, &data, &len);
            check_int_eq(s3, RULES_FORGE_STATUS_END_OF_STREAM);

            vtable->cleanup(ctx);
            dll_close(h);
            remove_test_file(test_file);
        }

        it("should return error for non-existent file via vtable") {
            dll_handle_t h = dll_open(FILE_SOURCE_DLL);
            check(h != DLL_NULL);

            get_vtable_fn get_vtable = (get_vtable_fn)dll_sym(h, "ruleforge_get_source_vtable");
            const ruleforge_datasource_vtable_t* vtable = get_vtable();

            void* ctx = NULL;
            vtable->init(NULL, &ctx);

            DataBindFormat data_format;
            const uint8_t* data;
            size_t len;

            ruleforge_status_t status = vtable->fetch(ctx, "nonexistent.csv", "Data", &data_format, &data, &len);
            check_int_eq(status, RULES_FORGE_ERROR_GENERIC);

            vtable->cleanup(ctx);
            dll_close(h);
        }
    }
}
