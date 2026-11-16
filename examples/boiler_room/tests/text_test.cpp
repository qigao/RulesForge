#define TINYTEST_NO_MAIN
#include "dispatch_fixture.hpp"
#include "runtime.hpp"
#include "text.hpp"
#include <array>
#include <limits>
#include <stdexcept>
#include <tinytest.hpp>

using namespace boiler_room;
using namespace boiler_room_test;
suite("Boiler-room text and binding boundaries") {
  it("recognizes whole scenario names from non-terminated slices") {
    const std::array<char, 6> input{'w', 'i', 'n', 'd', 'o', 'w'};
    auto spec = parse_scenario({input.data(), input.size()});
    check_equal(static_cast<int>(spec.scenario), static_cast<int>(Scenario::window));
    check_equal(spec.seconds, 240);
    check_equal(static_cast<int>(parse_scenario("dispatch").scenario),
                static_cast<int>(Scenario::dispatch));
    check_throws_as(parse_scenario("window-extra"), std::invalid_argument);
    check_throws_as(parse_scenario(""), std::invalid_argument);
    check_throws_as(parse_scenario("WINDOW"), std::invalid_argument);
  }
  it("returns static labels after input destruction and rejects prefixes and embedded NUL") {
    std::string_view result;
    {
      std::array<char, 4> input{'K', 'E', 'E', 'P'};
      result = canonical_label({input.data(), input.size()});
      input.fill('x');
    }
    check_equal(result, std::string_view("KEEP"));
    check_throws_as(canonical_label("KEEP_EXTRA"), std::invalid_argument);
    check_throws_as(canonical_label(std::string_view("KEEP\0EXTRA", 10)), std::invalid_argument);
    check_throws_as(canonical_label(""), std::invalid_argument);
    std::array<char, 96> oversized{};
    check_throws_as(canonical_label({oversized.data(), oversized.size()}), std::invalid_argument);
  }
  it("compares unaligned payloads including every vector tail and the last wire byte") {
    std::array<char, 514> a{}, b{};
    for (size_t i = 0; i < a.size(); ++i)
      a[i] = b[i] = static_cast<char>(i);
    for (size_t length = 0; length <= 512; ++length) {
      auto left = vstr_from_buf(a.data() + 1, length);
      auto right = vstr_from_buf(b.data() + 1, length);
      check_true(payload_equal(left, right));
      if (length) {
        b[length] ^= 1;
        check_false(payload_equal(left, right));
        b[length] ^= 1;
      }
    }
    check_false(payload_equal(vstr_from_buf(a.data(), 1), vstr_from_buf(b.data(), 2)));
    check_true(payload_equal(vstr_from_buf(nullptr, 0), vstr_from_buf(nullptr, 0)));
    check_throws_as(payload_equal(vstr_from_buf(nullptr, 1), vstr_from_buf(b.data(), 1)),
                    std::invalid_argument);
  }
  it("rejects a bad final snapshot before admitting any dispatch facts") {
    Runtime runtime(BOILER_BUNDLE);
    auto input = ready_dispatch_input();
    input.pumps[2].window.mean = std::numeric_limits<double>::quiet_NaN();
    check_throws_as(runtime.dispatch(input), std::runtime_error);
    check_equal(runtime.pending_dispatch_facts(), 0u);
    check_equal(runtime.dispatch(ready_dispatch_input()).action, std::string_view("KEEP"));
  }
  it("rejects a bad pump value before advancing the session clock") {
    Runtime runtime(BOILER_BUNDLE);
    PumpInput input;
    input.rms = std::numeric_limits<double>::infinity();
    check_throws_as(runtime.pump(0, input), std::runtime_error);
    input.rms = 2;
    check_equal(runtime.pump(0, input).window.tick, 0);
  }
  it("keeps alarm labels alive independently of the caller buffer") {
    Runtime runtime(BOILER_BUNDLE);
    std::array<char, 11> code{'D', 'A', 'T', 'A', '_', 'S', 'T', 'A', 'L', 'E', '\0'};
    auto alarm = runtime.alarm(0, code.data());
    code.fill('x');
    check_equal(alarm.code, std::string_view("DATA_STALE"));
  }
  it("propagates a DataBind JSON parse failure from the simulated MQTT source") {
    Config config;
    config.scenario = Scenario::normal;
    config.seconds = 60;
    config.payload_failure = Config::PayloadFailure::malformed_json;
    check_throws_as(simulate(BOILER_BUNDLE, config), std::runtime_error);
  }
}
