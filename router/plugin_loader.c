/**
 * @file plugin_loader.c
 * @brief Cross-platform DLL/SO plugin loader implementation
 */

#include "plugin_loader.h"
#include <string.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <dlfcn.h>
  #include <time.h>
#endif

ruleforge_status_t plugin_load(const char *dll_path, const char *symbol,
                               plugin_handle_t *out_handle, void **out_fn) {
  if (!dll_path || !symbol || !out_handle || !out_fn)
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;

#ifdef _WIN32
  plugin_handle_t h = LoadLibraryA(dll_path);
#else
  plugin_handle_t h = dlopen(dll_path, RTLD_LAZY);
#endif

  if (!h)
    return RULES_FORGE_ERROR_GENERIC;

#ifdef _WIN32
  void *fn = (void *)GetProcAddress(h, symbol);
#else
  void *fn = dlsym(h, symbol);
#endif

  if (!fn) {
    plugin_unload(h);
    return RULES_FORGE_ERROR_GENERIC;
  }

  *out_handle = h;
  *out_fn = fn;
  return RULES_FORGE_OK;
}

void plugin_unload(plugin_handle_t handle) {
  if (!handle)
    return;
#ifdef _WIN32
  FreeLibrary(handle);
#else
  dlclose(handle);
#endif
}

// Get current time in microseconds
uint64_t get_time_us() {
#ifdef _WIN32
  LARGE_INTEGER freq, counter;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&counter);
  return (counter.QuadPart * 1000000) / freq.QuadPart;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
}
