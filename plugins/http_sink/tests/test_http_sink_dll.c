/**
 * @file test_http_sink_dll.c
 * @brief BDD tests for http_sink plugin via DLL loading
 */

#include "tinytest.h"
#include "rule_forge_plugin.h"
#include <string.h>

#ifdef _WIN32
#include <windows.h>
typedef HMODULE dll_handle_t;
#define dll_open(path)        LoadLibraryA(path)
#define dll_sym(h, name)      GetProcAddress(h, name)
#define dll_close(h)          FreeLibrary(h)
#define DLL_NULL              NULL
#else
#include <dlfcn.h>
typedef void* dll_handle_t;
#define dll_open(path)        dlopen(path, RTLD_LAZY)
#define dll_sym(h, name)      dlsym(h, name)
#define dll_close(h)          dlclose(h)
#define DLL_NULL              NULL
#endif

#ifndef HTTP_SINK_DLL
#ifdef _WIN32
#define HTTP_SINK_DLL "http_sink_plugin.dll"
#else
#define HTTP_SINK_DLL "libhttp_sink_plugin.so"
#endif
#endif

typedef const ruleforge_datasink_vtable_t* (*get_vtable_fn)(void);

spec("HTTP Sink Plugin DLL") {
    describe("DLL Loading") {
        it("should load the DLL successfully") {
            dll_handle_t h = dll_open(HTTP_SINK_DLL);
            check(h != DLL_NULL);
            if (h) dll_close(h);
        }

        it("should export ruleforge_get_sink_vtable") {
            dll_handle_t h = dll_open(HTTP_SINK_DLL);
            check(h != DLL_NULL);

            get_vtable_fn get_vtable = (get_vtable_fn)dll_sym(h, "ruleforge_get_sink_vtable");
            check(get_vtable != NULL);

            dll_close(h);
        }

    }

    describe("Vtable via DLL") {
        it("should return valid vtable with all function pointers") {
            dll_handle_t h = dll_open(HTTP_SINK_DLL);
            check(h != DLL_NULL);

            get_vtable_fn get_vtable = (get_vtable_fn)dll_sym(h, "ruleforge_get_sink_vtable");
            check(get_vtable != NULL);

            const ruleforge_datasink_vtable_t* vtable = get_vtable();
            check(vtable != NULL);
            check(vtable->init != NULL);
            check(vtable->push_route != NULL);
            check(vtable->cleanup != NULL);

            dll_close(h);
        }
    }

    describe("Plugin Lifecycle via DLL") {
        it("should init and cleanup via vtable") {
            dll_handle_t h = dll_open(HTTP_SINK_DLL);
            check(h != DLL_NULL);

            get_vtable_fn get_vtable = (get_vtable_fn)dll_sym(h, "ruleforge_get_sink_vtable");
            const ruleforge_datasink_vtable_t* vtable = get_vtable();

            void* ctx = NULL;
            ruleforge_status_t status = vtable->init(NULL, &ctx);

            check_int_eq(status, RULES_FORGE_OK);
            check(ctx != NULL);

            vtable->cleanup(ctx);
            dll_close(h);
        }

        it("should return error for NULL route envelope via vtable") {
            dll_handle_t h = dll_open(HTTP_SINK_DLL);
            check(h != DLL_NULL);

            get_vtable_fn get_vtable = (get_vtable_fn)dll_sym(h, "ruleforge_get_sink_vtable");
            const ruleforge_datasink_vtable_t* vtable = get_vtable();

            void* ctx = NULL;
            vtable->init(NULL, &ctx);

            ruleforge_status_t status = vtable->push_route(ctx, NULL);
            check_int_eq(status, RULES_FORGE_ERROR_INVALID_ARGUMENT);

            vtable->cleanup(ctx);
            dll_close(h);
        }
    }
}
