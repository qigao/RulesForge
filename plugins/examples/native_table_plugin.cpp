#include "rule_forge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fmt.h>
 
 namespace {
char *dup_cstr(char const *s) {
  size_t n = std::strlen(s);
  auto *p = static_cast<char *>(std::malloc(n + 1));
  if (!p)
    return nullptr;
  std::memcpy(p, s, n + 1);
  return p;
}

ruleforge_status_t plugin_log(void *ctx, int argc, const char **argv, char **out_result) {
  (void)ctx;
  std::printf("[plugin_log] argc=%d", argc);
  for (int i = 0; i < argc; ++i) {
    std::printf(" arg%d=%s", i, argv && argv[i] ? argv[i] : "null");
  }
  std::printf("\n");
  if (out_result)
    *out_result = dup_cstr("{\"ok\":true}");
  return RULES_FORGE_OK;
}

ruleforge_status_t plugin_metric(void *ctx, int argc, const char **argv, char **out_result) {
  (void)ctx;
  double sum = 0.0;
  for (int i = 0; i < argc; ++i) {
    if (!argv || !argv[i])
      continue;
    sum += std::atof(argv[i]);
  }
  char buffer[128];
  fmt(buffer, sizeof(buffer), "{{\"sum\":{:.3f}}}", sum);
  if (out_result)
    *out_result = dup_cstr(buffer);
  return RULES_FORGE_OK;
}
} // namespace

#if defined(_WIN32)
  #define RF_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
  #define RF_PLUGIN_EXPORT extern "C"
#endif

RF_PLUGIN_EXPORT ruleforge_status_t
ruleforge_get_function_table(ruleforge_plugin_function_table_t *out_table) {
  if (!out_table)
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;

  static ruleforge_plugin_function_entry_t entries[] = {
      {"pluginLog", plugin_log, nullptr},
      {"pluginMetric", plugin_metric, nullptr},
  };

  out_table->abi_version = RULEFORGE_PLUGIN_ABI_V1;
  out_table->function_count = static_cast<uint32_t>(sizeof(entries) / sizeof(entries[0]));
  out_table->functions = entries;
  return RULES_FORGE_OK;
}
