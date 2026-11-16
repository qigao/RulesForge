#pragma once

#include "rules_forge.h"
#include <array>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

namespace maintenance_example {
inline void require(ruleforge_status_t status) {
  if (status != RULES_FORGE_OK)
    throw std::runtime_error(ruleforge_get_last_error_message());
}
// All these owners are synchronous, noncopyable and destroyed on their creating
// thread. Failed cleanup is an ownership invariant failure, never a script error.
inline void release(ruleforge_status_t status) noexcept {
  if (status != RULES_FORGE_OK) {
    std::fprintf(stderr, "Example cleanup failed: %s\n", ruleforge_get_last_error_message());
    std::terminate();
  }
}
struct Program {
  ruleforge_program_t handle = nullptr;
  explicit Program(char const *bundle) { require(ruleforge_program_create(bundle, &handle)); }
  Program(Program const &) = delete;
  Program &operator=(Program const &) = delete;
  ~Program() {
    release(ruleforge_program_close(handle));
    release(ruleforge_program_destroy(handle));
  }
};
struct Query {
  ruleforge_query_result_t handle = nullptr;
  Query(ruleforge_stateful_session_t session, char const *name) {
    require(ruleforge_session_query(session, name, &handle));
  }
  Query(Query const &) = delete;
  Query &operator=(Query const &) = delete;
  ~Query() { release(ruleforge_query_result_destroy(handle)); }
  int size() const {
    int value = ruleforge_query_result_get_size(handle);
    if (value < 0) throw std::runtime_error(ruleforge_get_last_error_message());
    return value;
  }
  ruleforge_fact_t row(int index) const {
    ruleforge_fact_t value = nullptr;
    require(ruleforge_query_result_get_fact_at_index(handle, index, "row", &value));
    return value;
  }
};
inline double number(ruleforge_fact_t fact, char const *field) {
  double value = 0;
  require(ruleforge_fact_get_field_as_double(fact, field, &value));
  return value;
}
inline bool boolean(ruleforge_fact_t fact, char const *field) {
  int value = 0;
  require(ruleforge_fact_get_field_as_bool(fact, field, &value));
  return value != 0;
}
inline std::string text(ruleforge_fact_t fact, char const *field) {
  // Example identifiers and actions are bounded well below this buffer size.
  std::array<char, 128> buffer{};
  size_t size = 0;
  require(ruleforge_fact_get_field_as_string(fact, field, buffer.data(), buffer.size(), &size));
  return {buffer.data(), size};
}
struct Evaluation {
  std::string action;
  int64_t count = 0;
  double mean = 0, peak = 0, cost = 0;
};
struct Session {
  ruleforge_stateful_session_t handle = nullptr;
  ruleforge_fact_t plan = nullptr;
  explicit Session(Program &program) {
    require(ruleforge_program_create_session(program.handle, &handle));
  }
  Session(Session const &) = delete;
  Session &operator=(Session const &) = delete;
  ~Session() { release(ruleforge_session_destroy(handle)); }
  ruleforge_fact_t insert(char const *type, std::string const &json) {
    ruleforge_fact_t value = nullptr;
    require(ruleforge_session_add_fact_json(handle, type, json.c_str(), &value));
    return value;
  }
  void configure(bool permit = true, double inspection_cost = 4.0, double baseline = 1.0,
                 std::string const &cell = "press-7") {
    insert("Asset", "{\"cell\":\"" + cell + "\",\"model\":{\"baseline\":" +
        std::to_string(baseline) + ",\"criticality\":1,\"inspectionCost\":" +
        std::to_string(inspection_cost) + ",\"stopCost\":35,\"failureCost\":100}}");
    auto value = insert("MaintenancePlan", "{\"cell\":\"" + cell +
        "\",\"action\":\"IDLE\",\"meanRisk\":0,\"peakRisk\":0,\"expectedCost\":0,\"pending\":false}");
    if (cell == "press-7") plan = value;
    insert("InspectionPermit", "{\"cell\":\"" + cell + "\",\"available\":" +
        (permit ? "true}" : "false}"));
  }
  ruleforge_fact_t reading(std::string const &ticket, char const *samples,
                          std::string const &cell = "press-7") {
    return insert("Reading", "{\"cell\":\"" + cell + "\",\"ticket\":\"" + ticket +
        "\",\"waveform\":{\"values\":" + samples + "},\"processed\":false}");
  }
  int fire() {
    int fired = 0;
    require(ruleforge_session_fire_all_rules(handle, 100, &fired));
    return fired;
  }
  ruleforge_fact_t request(std::string const &ticket) {
    return insert("ReviewRequest", "{\"cell\":\"press-7\",\"ticket\":\"" + ticket +
                                    "\",\"done\":false}");
  }
  Evaluation evaluation(std::string const &ticket) {
    Query query(handle, "Evaluations");
    int matches = 0;
    Evaluation value;
    for (int index = 0; index < query.size(); ++index) {
      auto row = query.row(index);
      if (text(row, "ticket") != ticket) continue;
      ++matches;
      value.action = text(row, "action");
      require(ruleforge_fact_get_field_as_int(row, "count", &value.count));
      value.mean = number(row, "meanRisk");
      value.peak = number(row, "peakRisk");
      value.cost = number(row, "expectedCost");
    }
    if (matches != 1) throw std::runtime_error("Expected exactly one evaluation for " + ticket);
    return value;
  }
  Evaluation step(std::string const &ticket, char const *samples) {
    reading(ticket, samples);
    fire(); // Commit analysis before requesting a decision over the resulting window.
    request(ticket);
    fire();
    return evaluation(ticket);
  }
  int count(char const *query) { return Query(handle, query).size(); }
};
}
