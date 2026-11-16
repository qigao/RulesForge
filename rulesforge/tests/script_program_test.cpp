#include "model/value_types.hpp"
#include "rules_forge.h"
#include <array>
#include <barrier>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <tinytest.hpp>
#include <vector>

namespace {
std::filesystem::path directory = RF_PROGRAM_TEST_DIR;
std::string read(char const *name) {
  std::ifstream f(directory / name, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
void write(char const *name, std::string const &source) {
  std::ofstream f(directory / name, std::ios::binary);
  f << source;
  if (!f)
    throw std::runtime_error("fixture write failed");
}
void ok(ruleforge_status_t s) {
  if (s != RULES_FORGE_OK)
    throw std::runtime_error(ruleforge_get_last_error_message());
}
struct Fixture {
  ruleforge_program_t program = nullptr;
  std::vector<ruleforge_stateful_session_t> sessions;
  std::string pricing = read("pricing.toad"), tax = read("tax.toad"), rules = read("pricing.rfl"),
              bundle = read("bundle.json"), schema = read("order.schema");
  ~Fixture() {
    for (auto s : sessions)
      if (s)
        ok(ruleforge_session_destroy(s));
    if (program) {
      ok(ruleforge_program_close(program));
      ok(ruleforge_program_destroy(program));
    }
    write("pricing.toad", pricing);
    write("tax.toad", tax);
    write("pricing.rfl", rules);
    write("bundle.json", bundle);
    write("order.schema", schema);
  }
  void create() {
    ok(ruleforge_program_create((directory / "bundle.json").string().c_str(), &program));
  }
  ruleforge_stateful_session_t session() {
    ruleforge_stateful_session_t s = nullptr;
    ok(ruleforge_program_create_session(program, &s));
    sessions.push_back(s);
    return s;
  }
  void reload() {
    ok(ruleforge_program_reload(program, (directory / "bundle.json").string().c_str()));
  }
  double run(ruleforge_stateful_session_t s) {
    ruleforge_fact_t fact = nullptr;
    ok(ruleforge_session_add_fact_json(s, "Order", "{\"amount\":15,\"total\":0,\"done\":false}",
                                       &fact));
    int fired = 0;
    ok(ruleforge_session_fire_all_rules(s, 10, &fired));
    if (fired != 1)
      throw std::runtime_error("unexpected fired count");
    double result = 0;
    ok(ruleforge_fact_get_field_as_double(fact, "total", &result));
    return result;
  }
};
} // namespace

suite("Toad script Program C API") {
  it("executes released Toad JIT while pinning script generations") {
    Fixture f;
    auto bundle = f.bundle;
    auto position = bundle.find("\"interpreter\"");
    check_not_equal(position, std::string::npos);
    bundle.replace(position, std::strlen("\"interpreter\""), "\"jit\"");
    write("bundle.json", bundle);
    f.create();
    auto first = f.session();
    check_equal(f.run(first), 40.0);
    write("pricing.toad",
          "func calculate(amount){return map {total:amount+10};};export(\"calculate\");");
    f.reload();
    auto second = f.session();
    check_equal(f.run(second), 50.0);
    check_equal(f.run(first), 40.0);
  }
  it("computes with two files and writes the result through RHS") {
    Fixture f;
    f.create();
    check_equal(f.run(f.session()), 40.0);
  }
  it("keeps old sessions on their original script generation") {
    Fixture f;
    f.create();
    auto first = f.session();
    write("pricing.toad",
          "func calculate(amount){return map {total:amount+10};};export(\"calculate\");");
    f.reload();
    auto second = f.session();
    check_equal(f.run(first), 40.0);
    check_equal(f.run(second), 50.0);
    check_equal(ruleforge_program_reload(f.program, (directory / "bundle.json").string().c_str()),
                RULES_FORGE_ERROR_RESOURCE_LIMIT);
    ok(ruleforge_session_destroy(first));
    f.sessions[0] = nullptr;
    f.reload();
    check_equal(f.run(f.session()), 50.0);
  }
  it("rejects a malformed candidate while the published generation still works") {
    Fixture f;
    f.create();
    write("pricing.toad", "invalid script {");
    check_equal(ruleforge_program_reload(f.program, (directory / "bundle.json").string().c_str()),
                RULES_FORGE_ERROR_COMPILATION_FAILED);
    check_equal(f.run(f.session()), 40.0);
  }
  it("rolls back an earlier fact update when a script fails") {
    Fixture f;
    write("pricing.rfl",
          "import schema \"order.schema\"\nrule \"Fail\" when $order: Order(done == false) then "
          "update $order { total = 99 } call \"pricing.calculate\"($order.amount) into $price "
          "update $order {done=true} end\n");
    write("pricing.toad", "func calculate(amount){throw \"failed\";};export(\"calculate\");");
    f.create();
    auto s = f.session();
    ruleforge_fact_t fact = nullptr;
    ok(ruleforge_session_add_fact_json(s, "Order", "{\"amount\":15,\"total\":0,\"done\":false}",
                                       &fact));
    int fired = 123;
    check_not_equal(ruleforge_session_fire_all_rules(s, 10, &fired), RULES_FORGE_OK);
    check_contains(ruleforge_get_last_error_message(), "pricing.calculate");
    double total = -1;
    ok(ruleforge_fact_get_field_as_double(fact, "total", &total));
    check_equal(total, 0.0);
  }
  it("does not retain script globals between computations") {
    Fixture f;
    write("pricing.toad", "count=0;func calculate(amount){count=count+1;return map "
                         "{total:count};};export(\"calculate\");");
    f.create();
    auto s = f.session();
    check_equal(f.run(s), 2.0);
    check_equal(f.run(s), 2.0);
  }
  it("requires the owner thread and retains handles after rejected destroy") {
    Fixture f;
    f.create();
    auto s = f.session();
    ruleforge_status_t reset = RULES_FORGE_OK, destroy = RULES_FORGE_OK;
    std::thread other([&] {
      reset = ruleforge_session_reset(s);
      destroy = ruleforge_session_destroy(s);
    });
    other.join();
    check_equal(reset, RULES_FORGE_ERROR_INVALID_ARGUMENT);
    check_equal(destroy, RULES_FORGE_ERROR_INVALID_ARGUMENT);
    ok(ruleforge_program_close(f.program));
    check_equal(ruleforge_program_destroy(f.program), RULES_FORGE_ERROR_RESOURCE_LIMIT);
    ruleforge_stateful_session_t rejected = nullptr;
    check_equal(ruleforge_program_create_session(f.program, &rejected),
                RULES_FORGE_ERROR_INVALID_ARGUMENT);
    check_null(rejected);
    check_equal(f.run(s), 40.0);
  }
  it("rejects CALL through an ordinary KB without a Program") {
    ruleforge_knowledge_base_t kb = nullptr;
    ok(ruleforge_kb_create(&kb));
    auto result = ruleforge_kb_load_drl(
        kb, "declare Item\namount: double\nend\nrule \"call\"\nwhen\n$item: Item()\nthen\n"
            "call \"pricing.calculate\"($item.amount) into $value\nend\n");
    check_equal(result, RULES_FORGE_ERROR_COMPILATION_FAILED);
    check_contains(ruleforge_get_last_error_message(), "CALL requires a Program");
    ok(ruleforge_kb_destroy(kb));
  }
  it("runs separate owner contexts concurrently across publication") {
    for (int round = 0; round < 16; ++round) {
      Fixture f;
      f.create();
      std::barrier ready(3), published(3);
      std::array<double, 2> answers{};
      std::array<std::string, 2> errors{};
      std::vector<std::thread> workers;
      for (size_t i = 0; i < 2; ++i)
        workers.emplace_back([&, i] {
          ruleforge_stateful_session_t s = nullptr;
          auto created = ruleforge_program_create_session(f.program, &s);
          if (created != RULES_FORGE_OK)
            errors[i] = ruleforge_get_last_error_message();
          ready.arrive_and_wait();
          published.arrive_and_wait();
          if (s) {
            try {
              answers[i] = f.run(s);
            } catch (std::exception const &e) {
              errors[i] = e.what();
            }
            auto closed = ruleforge_session_destroy(s);
            if (closed != RULES_FORGE_OK)
              errors[i] = ruleforge_get_last_error_message();
          }
        });
      ready.arrive_and_wait();
      write("pricing.toad",
            "func calculate(amount){return map {total:amount+10};};export(\"calculate\");");
      auto reloaded =
          ruleforge_program_reload(f.program, (directory / "bundle.json").string().c_str());
      published.arrive_and_wait();
      for (auto &worker : workers)
        worker.join();
      ok(reloaded);
      check(errors[0].empty());
      check(errors[1].empty());
      check_equal(answers[0], 40.0);
      check_equal(answers[1], 40.0);
      check_equal(f.run(f.session()), 50.0);
    }
  }
  it("rejects unknown configuration fields before publication") {
    Fixture f;
    auto text = f.bundle;
    text.insert(text.find('{') + 1, "\"unknown_setting\":true,");
    write("bundle.json", text);
    check_equal(ruleforge_program_create((directory / "bundle.json").string().c_str(), &f.program),
                RULES_FORGE_ERROR_COMPILATION_FAILED);
    check_null(f.program);
  }
  it("rejects exponentially expanded signature schemas within a bounded budget") {
    Fixture f;
    std::string schema =
        "schema Shapes [id(1), version(1), byte_order(little)];\nmessage Type0 {double value;}\n";
    for (int n = 1; n <= 17; ++n)
      schema += "message Type" + std::to_string(n) + " { Type" + std::to_string(n - 1) +
                " left; Type" + std::to_string(n - 1) + " right;}\n";
    write("order.schema", schema);
    auto bundle = f.bundle;
    auto position = bundle.find("\"result\":\"Price\"");
    bundle.replace(position, std::string("\"result\":\"Price\"").size(), "\"result\":\"Type17\"");
    write("bundle.json", bundle);
    check_equal(ruleforge_program_create((directory / "bundle.json").string().c_str(), &f.program),
                RULES_FORGE_ERROR_RESOURCE_LIMIT);
    check_null(f.program);
  }
  it("enforces session capacity without losing rejected ownership") {
    Fixture f;
    auto text = f.bundle;
    auto position = text.find("\"max_sessions\":8");
    text.replace(position, std::string("\"max_sessions\":8").size(), "\"max_sessions\":1");
    write("bundle.json", text);
    f.create();
    auto first = f.session();
    ruleforge_stateful_session_t rejected = nullptr;
    check_equal(ruleforge_program_create_session(f.program, &rejected),
                RULES_FORGE_ERROR_RESOURCE_LIMIT);
    check_null(rejected);
    ok(ruleforge_session_destroy(first));
    f.sessions[0] = nullptr;
    check_equal(f.run(f.session()), 40.0);
  }
  it("propagates a call step limit through the C API") {
    Fixture f;
    auto text = f.bundle;
    auto position = text.find("\"steps\":100000");
    text.replace(position, std::string("\"steps\":100000").size(), "\"steps\":1");
    write("bundle.json", text);
    f.create();
    auto s = f.session();
    ruleforge_fact_t fact = nullptr;
    ok(ruleforge_session_add_fact_json(s, "Order", "{\"amount\":15,\"total\":0,\"done\":false}",
                                       &fact));
    int fired = 0;
    check_equal(ruleforge_session_fire_all_rules(s, 10, &fired), RULES_FORGE_ERROR_RESOURCE_LIMIT);
    double total = -1;
    ok(ruleforge_fact_get_field_as_double(fact, "total", &total));
    check_equal(total, 0.0);
  }
  it("bounds all copied results within one RHS rather than resetting per CALL") {
    Fixture f;
    auto text = f.bundle;
    auto position = text.find("\"value_bytes\":4194304");
    // Price has two owned value nodes plus the five-byte "total" key. Leave
    // one byte less than another scalar result, using this build's ABI sizes.
    text.replace(position, std::string("\"value_bytes\":4194304").size(),
                 "\"value_bytes\":" + std::to_string(3 * sizeof(ConstraintValue) + 4));
    write("bundle.json", text);
    write("pricing.rfl", "import schema \"order.schema\"\nrule \"One\"\nwhen\n$order: Order(done "
                         "== false)\nthen\ncall \"pricing.calculate\"($order.amount) into "
                         "$price\nupdate $order {total=$price.total,done=true}\nend\n");
    f.create();
    check_equal(f.run(f.session()), 20.0);
    write("pricing.rfl", f.rules);
    f.reload();
    auto s = f.session();
    ruleforge_fact_t fact = nullptr;
    ok(ruleforge_session_add_fact_json(s, "Order", "{\"amount\":15,\"total\":0,\"done\":false}",
                                       &fact));
    int fired = 0;
    check_equal(ruleforge_session_fire_all_rules(s, 10, &fired), RULES_FORGE_ERROR_RESOURCE_LIMIT);
    double total = -1;
    ok(ruleforge_fact_get_field_as_double(fact, "total", &total));
    check_equal(total, 0.0);
  }
  it("rejects a wrong result type and rolls back previous commands") {
    Fixture f;
    write("pricing.toad",
          "func calculate(amount){return map {total:\"bad\"};};export(\"calculate\");");
    f.create();
    auto s = f.session();
    ruleforge_fact_t fact = nullptr;
    ok(ruleforge_session_add_fact_json(s, "Order", "{\"amount\":15,\"total\":0,\"done\":false}",
                                       &fact));
    int fired = 0;
    check_not_equal(ruleforge_session_fire_all_rules(s, 10, &fired), RULES_FORGE_OK);
    double total = -1;
    ok(ruleforge_fact_get_field_as_double(fact, "total", &total));
    check_equal(total, 0.0);
  }
}
