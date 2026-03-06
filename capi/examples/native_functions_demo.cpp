// Example demonstrating native C function registration in RulesForge
// This shows how to register custom C functions that can be called from rules

#include "rule_forge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Example 1: Simple logging function
ruleforge_status_t log_function(void *ctx, int argc, const char **argv, char **out_result) {
  printf("[LOG] ");
  for (int i = 0; i < argc; i++) {
    printf("%s ", argv[i]);
  }
  printf("\n");

  // Return undefined (no result)
  *out_result = nullptr;
  return RULES_FORGE_OK;
}

// Example 2: Webhook simulation (would normally make HTTP request)
ruleforge_status_t webhook_function(void *ctx, int argc, const char **argv, char **out_result) {
  if (argc < 2) {
    *out_result = strdup("{\"error\": \"webhook requires url and payload\"}");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  const char *url = argv[0];
  const char *payload = argv[1];

  printf("[WEBHOOK] POST to %s with payload: %s\n", url, payload);

  // Simulate successful response
  *out_result = strdup("{\"status\": \"success\", \"code\": 200}");
  return RULES_FORGE_OK;
}

// Example 3: MQTT republish simulation
ruleforge_status_t republish_function(void *ctx, int argc, const char **argv, char **out_result) {
  if (argc < 2) {
    *out_result = strdup("{\"error\": \"republish requires topic and message\"}");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  const char *topic = argv[0];
  const char *message = argv[1];

  printf("[MQTT] Publishing to topic '%s': %s\n", topic, message);

  // Return success
  *out_result = strdup("{\"published\": true}");
  return RULES_FORGE_OK;
}

// Example 4: Custom calculation with context
struct CalculatorContext {
  double multiplier;
};

ruleforge_status_t calculate_function(void *ctx, int argc, const char **argv, char **out_result) {
  if (argc < 1) {
    *out_result = strdup("{\"error\": \"calculate requires a number\"}");
    return RULES_FORGE_ERROR_INVALID_ARGUMENT;
  }

  CalculatorContext *calc_ctx = (CalculatorContext *)ctx;
  double value = atof(argv[0]);
  double result = value * calc_ctx->multiplier;

  char buffer[256];
  snprintf(buffer, sizeof(buffer), "{\"result\": %.2f}", result);
  *out_result = strdup(buffer);

  return RULES_FORGE_OK;
}

int main() {
#ifdef _WIN32
  system("chcp 65001 >nul"); // UTF-8
#endif
  printf("=== RulesForge Native Functions Demo ===\n\n");

  // Initialize RulesForge
  if (ruleforge_init() != RULES_FORGE_OK) {
    fprintf(stderr, "Failed to initialize RulesForge\n");
    return 1;
  }

  // Create knowledge base
  ruleforge_knowledge_base_t kb;
  if (ruleforge_kb_create(&kb) != RULES_FORGE_OK) {
    fprintf(stderr, "Failed to create knowledge base\n");
    return 1;
  }

  // Register native functions
  printf("Registering native functions...\n");

  if (ruleforge_kb_register_native_function(kb, "log", log_function, nullptr) != RULES_FORGE_OK) {
    fprintf(stderr, "Failed to register log function: %s\n", ruleforge_get_last_error_message());
    return 1;
  }
  printf("  ✓ Registered 'log' function\n");

  if (ruleforge_kb_register_native_function(kb, "webhook", webhook_function, nullptr) !=
      RULES_FORGE_OK) {
    fprintf(stderr, "Failed to register webhook function: %s\n",
            ruleforge_get_last_error_message());
    return 1;
  }
  printf("  ✓ Registered 'webhook' function\n");

  if (ruleforge_kb_register_native_function(kb, "republish", republish_function, nullptr) !=
      RULES_FORGE_OK) {
    fprintf(stderr, "Failed to register republish function: %s\n",
            ruleforge_get_last_error_message());
    return 1;
  }
  printf("  ✓ Registered 'republish' function\n");

  // Register function with context
  CalculatorContext calc_ctx = {2.5};
  if (ruleforge_kb_register_native_function(kb, "calculate", calculate_function, &calc_ctx) !=
      RULES_FORGE_OK) {
    fprintf(stderr, "Failed to register calculate function: %s\n",
            ruleforge_get_last_error_message());
    return 1;
  }
  printf("  ✓ Registered 'calculate' function with context\n\n");

  // Load rules that use native functions
  const char *rules = R"(
declare Sensor
    id : String
    temperature : double
    location : String
end

declare Alert
    sensorId : String
    temperature : double
    calculatedValue : double
end

rule "Test Native Functions"
when
    $sensor : Sensor(temperature > 25)
    not Alert(sensorId == $sensor.id)
then
    insert Alert {
        sensorId = $sensor.id,
        temperature = $sensor.temperature,
        calculatedValue = $sensor.temperature * 2.5
    }
end
    )";

  printf("Loading rules...\n");
  if (ruleforge_kb_load_drl(kb, rules) != RULES_FORGE_OK) {
    fprintf(stderr, "Failed to load rules: %s\n", ruleforge_get_last_error_message());
    ruleforge_kb_destroy(kb);
    return 1;
  }
  printf("  ✓ Rules loaded successfully\n\n");

  // Create session
  ruleforge_stateful_session_t session;
  if (ruleforge_session_create(kb, &session) != RULES_FORGE_OK) {
    fprintf(stderr, "Failed to create session: %s\n", ruleforge_get_last_error_message());
    ruleforge_kb_destroy(kb);
    return 1;
  }
  printf("Session created successfully\n\n");

  // Add a sensor fact that will trigger the rule
  printf("Adding sensor fact...\n");
  const char *sensor_json =
      "{\"id\": \"sensor-001\", \"temperature\": 28.5, \"location\": \"room-A\"}";
  if (ruleforge_session_add_fact_json(session, "Sensor", sensor_json) != RULES_FORGE_OK) {
    fprintf(stderr, "Failed to add fact: %s\n", ruleforge_get_last_error_message());
    ruleforge_session_destroy(session);
    ruleforge_kb_destroy(kb);
    return 1;
  }
  printf("  ✓ Sensor fact added\n\n");

  // Fire rules
  printf("Firing rules...\n");
  int fired_count = 0;
  if (ruleforge_session_fire_all_rules(session, -1, &fired_count) != RULES_FORGE_OK) {
    fprintf(stderr, "Failed to fire rules: %s\n", ruleforge_get_last_error_message());
    ruleforge_session_destroy(session);
    ruleforge_kb_destroy(kb);
    return 1;
  }
  printf("  ✓ Fired %d rule(s)\n\n", fired_count);

  // Cleanup
  ruleforge_session_destroy(session);
  ruleforge_kb_destroy(kb);
  ruleforge_cleanup();

  printf("=== Demo completed successfully ===\n");
  return 0;
}
