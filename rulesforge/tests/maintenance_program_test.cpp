#include "../../examples/maintenance/session.hpp"
#include <tinytest.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace maintenance_example;

namespace {
struct CandidateBundle {
  std::filesystem::path path;
  std::string source;
  CandidateBundle() {
    static std::atomic<unsigned> sequence{0};
    auto directory = std::filesystem::path(RF_MAINTENANCE_BUNDLE).parent_path();
    path = directory / ("candidate-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                         "-" + std::to_string(sequence.fetch_add(1)) + ".json");
    std::ifstream input(RF_MAINTENANCE_BUNDLE, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read maintenance bundle");
    source.assign(std::istreambuf_iterator<char>(input), {});
  }
  CandidateBundle(CandidateBundle const &) = delete;
  ~CandidateBundle() {
    std::error_code error;
    std::filesystem::remove(path, error);
    if (error) std::terminate();
  }
  std::string native_entries() const {
    auto field = source.find("\"native_modules\"");
    auto begin = source.find('[', field), end = source.find(']', begin);
    if (field == std::string::npos || begin == std::string::npos || end == std::string::npos)
      throw std::runtime_error("Missing fixture native module list");
    return source.substr(begin + 1, end - begin - 1);
  }
  void native(std::string const &entries) {
    auto field = source.find("\"native_modules\"");
    auto begin = source.find('[', field), end = source.find(']', begin);
    if (field == std::string::npos || begin == std::string::npos || end == std::string::npos)
      throw std::runtime_error("Missing fixture native module list");
    source.replace(begin, end - begin + 1, "[" + entries + "]");
  }
  void jit() {
    auto offset = source.find("\"mode\":\"interpreter\"");
    if (offset == std::string::npos) throw std::runtime_error("Missing fixture execution mode");
    source.replace(offset, std::string("\"mode\":\"interpreter\"").size(), "\"mode\":\"jit\"");
  }
  std::string save() const {
    std::ofstream output(path, std::ios::binary);
    output << source;
    output.close();
    if (!output) throw std::runtime_error("Cannot write candidate bundle");
    return path.string();
  }
};
}

suite("Windowed predictive maintenance with Toad") {
  it("runs the script and native calculation chain in JIT mode") {
    CandidateBundle bundle;
    bundle.jit();
    Program program(bundle.save().c_str());
    Session session(program);
    session.configure();
    session.step("one", "[1,1,1,1]");
    session.step("two", "[1,2,1,2]");
    auto value = session.step("three", "[1,2,1,2]");
    check_equal(value.action, std::string("INSPECT"));
    check_within(value.mean, 25.0 / 3.0, 1e-9);
    check_within(value.cost, 4.0 + 25.0 / 12.0, 1e-9);
  }
  it("rejects native module candidates atomically and keeps the published generation usable") {
    Program program(RF_MAINTENANCE_BUNDLE);
    Session old(program);
    old.configure(false);
    old.step("one", "[1,1,1,1]");
    old.step("two", "[1,2,1,2]");
    old.step("three", "[1,2,1,2]");
    for (unsigned kind = 0; kind < 8; ++kind) {
      CandidateBundle bundle;
      auto original = bundle.native_entries();
      if (kind == 0) bundle.native("{\"path\":\"missing-native-module.dll\"}");
      if (kind == 1) bundle.native(original + "," + original);
      if (kind == 2) bundle.native(original + ",{\"path\":\"" RF_MAINTENANCE_PROVIDER_NAME "\"}");
      if (kind == 3) bundle.native("{\"unknown_path\":\"bad\"}");
      if (kind == 4) bundle.native("42");
      if (kind == 5) bundle.native("");
      if (kind == 6) {
        auto absolute = std::filesystem::path(RF_MAINTENANCE_PROVIDER_PATH).generic_string();
        bundle.native("{\"path\":\"" + absolute + "\"}");
      }
      if (kind == 7) {
        std::string excessive = original;
        for (unsigned i = 1; i < 17; ++i) excessive += "," + original;
        bundle.native(excessive);
      }
      auto path = bundle.save();
      check_not_equal(ruleforge_program_reload(program.handle, path.c_str()), RULES_FORGE_OK);
      old.request("after-failure-" + std::to_string(kind));
      old.fire();
      auto value = old.evaluation("after-failure-" + std::to_string(kind));
      check_equal(value.action, std::string("MONITOR"));
      check_equal(value.count, int64_t{3});
      check_within(value.mean, 25.0 / 3.0, 1e-9);
    }
    require(ruleforge_program_reload(program.handle, RF_MAINTENANCE_BUNDLE));
    Session current(program);
    current.configure();
    check_equal(current.step("fresh", "[1,1,1,1]").action, std::string("WARMUP"));
  }
  it("rolls back the current RHS when the native calculation rejects arithmetic underflow") {
    Program program(RF_MAINTENANCE_BUNDLE);
    Session session(program);
    session.insert("Asset", "{\"cell\":\"press-7\",\"model\":{\"baseline\":1e-200,\"criticality\":1,\"inspectionCost\":4,\"stopCost\":35,\"failureCost\":100}}");
    session.plan = session.insert("MaintenancePlan", "{\"cell\":\"press-7\",\"action\":\"IDLE\",\"meanRisk\":0,\"peakRisk\":0,\"expectedCost\":0,\"pending\":false}");
    session.insert("InspectionPermit", "{\"cell\":\"press-7\",\"available\":true}");
    auto reading = session.reading("native-failure", "[1e-200,1e-200,1e-200,1e-200]");
    int fired = 0;
    check_not_equal(ruleforge_session_fire_all_rules(session.handle, 100, &fired), RULES_FORGE_OK);
    check_contains(ruleforge_get_last_error_message(), "non-finite");
    check_false(boolean(session.plan, "pending"));
    check_false(boolean(reading, "processed"));
    check_equal(session.count("Observations"), 0);
    check_equal(text(session.plan, "action"), std::string("IDLE"));
  }
  it("checks every decision and removes an old peak from all window aggregates") {
    Program program(RF_MAINTENANCE_BUNDLE);
    Session session(program);
    session.configure();
    std::array<char const *, 8> samples = {
        "[1,1,1,1]", "[1,2,1,2]", "[1,2,1,2]", "[1,5,1,5]",
        "[1,1,1,1]", "[1,1,1,1]", "[1,1,1,1]", "[1,1,1,1]"};
    std::array<char const *, 8> actions = {
        "WARMUP", "WARMUP", "INSPECT", "STOP", "STOP", "STOP", "STOP", "MONITOR"};
    std::array<double, 8> means = {0, 0, 25.0 / 3.0, 31.25, 31.25, 28.125, 25, 0};
    for (size_t index = 0; index < samples.size(); ++index) {
      auto value = session.step(std::to_string(index), samples[index]);
      check_equal(value.action, std::string(actions[index]));
      check_equal(value.count, static_cast<int64_t>(index < 4 ? index + 1 : 4));
      check_within(value.mean, means[index], 1e-9);
      double peak = index < 2 || index == 7 ? 0 : index == 2 ? 12.5 : 100;
      check_within(value.peak, peak, 1e-9);
      double cost = index < 2 || index == 7 ? 0 : index == 2 ? 4 + 25.0 / 12.0 : 35;
      check_within(value.cost, cost, 1e-9);
      check_false(boolean(session.plan, "pending"));
    }
    check_equal(session.count("Evaluations"), 8);
    check_equal(session.count("Observations"), 8); // Window eviction does not delete session facts.
    check_equal(session.count("ActionRequests"), 5); // Real forward chaining after CALL.
    Query observations(session.handle, "Observations");
    int inspected = 0;
    for (int index = 0; index < observations.size(); ++index) {
      auto row = observations.row(index);
      if (text(row, "ticket") == "1") {
        ++inspected;
        check_within(number(row, "variance"), 0.25, 1e-9);
        check_within(number(row, "roughness"), 1.0, 1e-9);
        check_within(number(row, "kurtosis"), 1.0, 1e-9);
        check_within(number(row, "risk"), 12.5, 1e-9);
      }
    }
    check_equal(inspected, 1);
  }
  it("filters another cell before that event can consume window capacity") {
    Program program(RF_MAINTENANCE_BUNDLE);
    Session session(program);
    session.configure();
    session.configure(true, 4, 1, "press-8");
    session.step("one", "[1,1,1,1]");
    session.step("two", "[1,2,1,2]");
    auto before = session.step("three", "[1,2,1,2]");
    for (int index = 0; index < 6; ++index) {
      session.reading("noise-" + std::to_string(index), "[1,9,1,9]", "press-8");
      session.fire();
    }
    session.request("after-noise");
    session.fire();
    auto after = session.evaluation("after-noise");
    check_equal(after.count, int64_t{3});
    check_equal(after.action, before.action);
    check_within(after.mean, before.mean, 1e-9);
    check_within(after.peak, before.peak, 1e-9);
    check_equal(session.count("Observations"), 9);
  }
  it("uses a joined permit to exclude an otherwise cheaper inspection") {
    Program program(RF_MAINTENANCE_BUNDLE);
    Session session(program);
    session.configure(false);
    session.step("one", "[1,1,1,1]");
    session.step("two", "[1,2,1,2]");
    auto value = session.step("three", "[1,2,1,2]");
    check_equal(value.action, std::string("MONITOR"));
    check_within(value.cost, 25.0 / 3.0, 1e-9);
    check_equal(session.count("ActionRequests"), 0);
  }
  it("rolls back the pending marker and input state when calibration is invalid") {
    Program program(RF_MAINTENANCE_BUNDLE);
    Session session(program);
    session.configure(true, 4, 0);
    auto reading = session.reading("bad-baseline", "[1,2,1,2]");
    int fired = 0;
    check_not_equal(ruleforge_session_fire_all_rules(session.handle, 100, &fired), RULES_FORGE_OK);
    check_contains(ruleforge_get_last_error_message(), "signal.analyze");
    check_false(boolean(session.plan, "pending"));
    check_false(boolean(reading, "processed"));
    check_equal(text(session.plan, "action"), std::string("IDLE"));
    check_equal(session.count("Observations"), 0);
    check_equal(session.count("ActionRequests"), 0);
  }
  it("preserves committed analysis while rolling back a failed decision RHS") {
    Program program(RF_MAINTENANCE_BUNDLE);
    Session session(program);
    session.configure(true, -1); // Valid schema data, invalid business cost model.
    session.step("one", "[1,1,1,1]");
    session.step("two", "[1,2,1,2]");
    auto reading = session.reading("three", "[1,2,1,2]");
    session.fire();
    auto request = session.request("three");
    int fired = 0;
    check_not_equal(ruleforge_session_fire_all_rules(session.handle, 100, &fired), RULES_FORGE_OK);
    check_contains(ruleforge_get_last_error_message(), "decision.decide");
    check_true(boolean(reading, "processed"));
    check_false(boolean(request, "done"));
    check_false(boolean(session.plan, "pending"));
    check_equal(text(session.plan, "action"), std::string("WARMUP"));
    check_equal(session.count("Observations"), 3);
    check_equal(session.count("Evaluations"), 2);
    check_equal(session.count("ActionRequests"), 0);
  }
  it("rejects malformed nested schema input before inserting a candidate fact") {
    Program program(RF_MAINTENANCE_BUNDLE);
    Session session(program);
    session.configure();
    int before = ruleforge_session_get_fact_count(session.handle);
    ruleforge_fact_t value = nullptr;
    check_not_equal(ruleforge_session_add_fact_json(session.handle, "Reading",
        "{\"cell\":\"press-7\",\"ticket\":\"bad\",\"waveform\":{\"values\":[\"bad\",1,1,1]},\"processed\":false}",
        &value), RULES_FORGE_OK);
    check_null(value);
    check_equal(ruleforge_session_get_fact_count(session.handle), before);
  }
  it("keeps window and script results independent in two concurrent owner sessions") {
    Program program(RF_MAINTENANCE_BUNDLE);
    std::array<std::string, 2> actions, errors;
    std::array<std::jthread, 2> owners;
    for (size_t index = 0; index < owners.size(); ++index) {
      owners[index] = std::jthread([&, index] {
        try {
          Session session(program);
          session.configure();
          auto samples = index == 0 ? "[1,1,1,1]" : "[1,5,1,5]";
          session.step("one", samples);
          session.step("two", samples);
          actions[index] = session.step("three", samples).action;
        } catch (std::exception const &error) { errors[index] = error.what(); }
      });
    }
    for (auto &owner : owners) owner.join();
    check_equal(errors[0], std::string{});
    check_equal(errors[1], std::string{});
    check_equal(actions[0], std::string("MONITOR"));
    check_equal(actions[1], std::string("STOP"));
  }
  it("pins the old window and strategy while new sessions use the published conservative strategy") {
    Program program(RF_MAINTENANCE_BUNDLE);
    Session old_session(program);
    old_session.configure(false);
    old_session.step("one", "[1,1,1,1]");
    old_session.step("two", "[1,2,1,2]");
    old_session.step("three", "[1,2,1,2]");
    require(ruleforge_program_reload(program.handle, RF_MAINTENANCE_CONSERVATIVE_BUNDLE));
    Session new_session(program);
    new_session.configure(false);
    new_session.step("one", "[1,1,1,1]");
    new_session.step("two", "[1,2,1,2]");
    auto conservative = new_session.step("three", "[1,2,1,2]");
    old_session.request("after-reload");
    old_session.fire();
    auto retained = old_session.evaluation("after-reload");
    check_equal(retained.count, int64_t{3});
    check_equal(conservative.count, retained.count);
    check_within(conservative.mean, retained.mean, 1e-9);
    check_equal(retained.action, std::string("MONITOR"));
    check_within(retained.cost, 25.0 / 3.0, 1e-9);
    check_equal(conservative.action, std::string("STOP"));
    check_within(conservative.cost, 35.0, 1e-9);
    check_equal(ruleforge_program_reload(program.handle, RF_MAINTENANCE_CONSERVATIVE_BUNDLE),
                RULES_FORGE_ERROR_RESOURCE_LIMIT);
  }
}
