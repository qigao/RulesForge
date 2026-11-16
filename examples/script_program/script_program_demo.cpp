#include "rules_forge.h"
#include <cstdio>
#include <string>

int main(int argc, char **argv) {
  if (argc != 4) {
    std::fprintf(stderr, "Usage: script_program_demo <bundle.json> <fact-type> <fact-json>\n");
    return 2;
  }
  ruleforge_program_t program = nullptr;
  ruleforge_stateful_session_t session = nullptr;
  int fired = 0;
  auto status = ruleforge_program_create(argv[1], &program);
  if (status == RULES_FORGE_OK)
    status = ruleforge_program_create_session(program, &session);
  if (status == RULES_FORGE_OK)
    status = ruleforge_session_add_fact_json(session, argv[2], argv[3], nullptr);
  if (status == RULES_FORGE_OK)
    status = ruleforge_session_fire_all_rules(session, 1000, &fired);
  if (status != RULES_FORGE_OK)
    std::fprintf(stderr, "%s\n", ruleforge_get_last_error_message());
  if (session) {
    auto closed = ruleforge_session_destroy(session);
    if (closed != RULES_FORGE_OK) {
      std::fprintf(stderr, "%s\n", ruleforge_get_last_error_message());
      return 1; // Program must remain alive while session cleanup still owns it.
    }
  }
  if (program) {
    auto closed = ruleforge_program_close(program);
    if (closed == RULES_FORGE_OK)
      closed = ruleforge_program_destroy(program);
    if (closed != RULES_FORGE_OK) {
      std::fprintf(stderr, "%s\n", ruleforge_get_last_error_message());
      return 1;
    }
  }
  if (status != RULES_FORGE_OK)
    return 1;
  std::printf("fired=%d\n", fired);
  return 0;
}
