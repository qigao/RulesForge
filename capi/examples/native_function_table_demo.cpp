#include "rule_forge.h"

#include <cstdio>
#include <string>

namespace {
std::string default_plugin_name() {
#if defined(_WIN32)
  return "ruleforge_native_plugin.dll";
#elif defined(__APPLE__)
  return "libruleforge_native_plugin.dylib";
#else
  return "libruleforge_native_plugin.so";
#endif
}
}  // namespace

int main(int argc, char** argv) {
  std::string plugin_path = argc > 1 ? argv[1] : default_plugin_name();

  if (ruleforge_init() != RULES_FORGE_OK) {
    std::fprintf(stderr, "init failed\n");
    return 1;
  }

  ruleforge_knowledge_base_t kb = nullptr;
  if (ruleforge_kb_create(&kb) != RULES_FORGE_OK) {
    std::fprintf(stderr, "kb create failed: %s\n", ruleforge_get_last_error_message());
    return 1;
  }

  if (ruleforge_kb_load_native_function_table(kb, plugin_path.c_str(), nullptr) != RULES_FORGE_OK) {
    std::fprintf(stderr, "plugin load failed: %s\n", ruleforge_get_last_error_message());
    ruleforge_kb_destroy(kb);
    return 1;
  }
  std::printf("loaded plugin table from: %s\n", plugin_path.c_str());

  char const* rules = R"(
declare Sensor
    id : String
    temperature : double
end

rule "Plugin Invoke"
when
    $s : Sensor(temperature > 30)
then
    invoke pluginLog($s.id, $s.temperature)
    invoke pluginMetric($s.temperature, 2, 3)
end
)";

  if (ruleforge_kb_load_drl(kb, rules) != RULES_FORGE_OK) {
    std::fprintf(stderr, "rule load failed: %s\n", ruleforge_get_last_error_message());
    ruleforge_kb_destroy(kb);
    return 1;
  }

  ruleforge_stateful_session_t session = nullptr;
  if (ruleforge_session_create(kb, &session) != RULES_FORGE_OK) {
    std::fprintf(stderr, "session create failed: %s\n", ruleforge_get_last_error_message());
    ruleforge_kb_destroy(kb);
    return 1;
  }

  char const* fact_json = "{\"id\":\"S-9\",\"temperature\":35.5}";
  if (ruleforge_session_add_fact_json(session, "Sensor", fact_json) != RULES_FORGE_OK) {
    std::fprintf(stderr, "add fact failed: %s\n", ruleforge_get_last_error_message());
    ruleforge_session_destroy(session);
    ruleforge_kb_destroy(kb);
    return 1;
  }

  int fired = 0;
  if (ruleforge_session_fire_all_rules(session, -1, &fired) != RULES_FORGE_OK) {
    std::fprintf(stderr, "fire failed: %s\n", ruleforge_get_last_error_message());
    ruleforge_session_destroy(session);
    ruleforge_kb_destroy(kb);
    return 1;
  }

  std::printf("fired rules: %d\n", fired);

  ruleforge_session_destroy(session);
  ruleforge_kb_destroy(kb);
  ruleforge_cleanup();
  return 0;
}
