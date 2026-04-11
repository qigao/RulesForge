/**
 * @file plugin_loader.h
 * @brief Cross-platform DLL/SO plugin loader for RulesForge route engine
 */

#ifndef PLUGIN_LOADER_H
#define PLUGIN_LOADER_H

#include "rule_forge_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
  #include <windows.h>
typedef HMODULE plugin_handle_t;
#else
typedef void *plugin_handle_t;
#endif

/**
 * @brief Load a plugin DLL and resolve its vtable entry point
 * @param dll_path Path to the DLL/SO
 * @param symbol   Export symbol name to resolve
 * @param out_handle Output DLL handle (caller must call plugin_unload on failure)
 * @param out_fn   Output function pointer
 * @return RULES_FORGE_OK on success
 */
ruleforge_status_t plugin_load(const char *dll_path, const char *symbol,
                               plugin_handle_t *out_handle, void **out_fn);

/**
 * @brief Unload a plugin DLL
 * @param handle Handle returned by plugin_load
 */
void plugin_unload(plugin_handle_t handle);
uint64_t get_time_us();

#ifdef __cplusplus
}
#endif

#endif // PLUGIN_LOADER_H
