#include "input.hpp"
#include "tests/fixture.hpp"
using namespace scenario_support;
suite("Production quality release") {
  it("releases a calibrated stable batch and persists a copied decision") {
    auto repository = run_fixture(quality_release::input);
    check_equal(repository.get("lot-1").action.view(), std::string_view("RELEASE"));
    check_within(repository.get("lot-1").metric, 1.0 / 600.0, 1e-10);
  }
  it("holds an otherwise qualified batch from an uncalibrated machine") {
    auto input = quality_release::input;
    input[1].json = R"({"machine":"press-1","valid":false})";
    auto repository = run_fixture(input);
    check_equal(repository.get("lot-1").reason.view(), std::string_view("calibration-expired"));
  }
  it("distinguishes dispersion from a hard specification violation") {
    auto input = quality_release::input;
    input[2].json =
        R"({"id":"lot-1","machine":"press-1","samples":{"values":[9.1,9.1,9.1,10.9,10.9,10.9]},"assessed":false})";
    auto recheck = run_fixture(input);
    check_equal(recheck.get("lot-1").action.view(), std::string_view("RECHECK"));
    input[2].json =
        R"({"id":"lot-1","machine":"press-1","samples":{"values":[10,10,10,10,10,12]},"assessed":false})";
    auto hold = run_fixture(input);
    check_equal(hold.get("lot-1").action.view(), std::string_view("HOLD"));
  }
  it("does not persist or deliver anything when the calculation fails") {
    auto input = quality_release::input;
    input[0].json = R"({"batch":"lot-1","limits":{"lower":11,"upper":9,"maxDispersion":0.01}})";
    MockDecisionApi repository;
    MockActionApi actions;
    auto execute_invalid = [&] {
      Program program(RF_SCENARIO_BUNDLE);
      evaluate(program, MockInputApi(input), repository, actions);
    };
    check_throws_with(execute_invalid(), "invalid quality specification");
    check_equal(repository.size(), std::size_t{0});
    check_equal(actions.size(), std::size_t{0});
  }
  it("executes the same typed computation with released Toad JIT") {
    auto repository = run_fixture(quality_release::input, RF_SCENARIO_JIT_BUNDLE);
    check_equal(repository.get("lot-1").action.view(), std::string_view("RELEASE"));
  }
  it("propagates persistence failure after computation without sending an action") {
    MockDecisionApi repository;
    MockActionApi actions;
    repository.fail_commit = true;
    auto execute_with_failed_storage = [&] {
      Program program(RF_SCENARIO_BUNDLE);
      evaluate(program, MockInputApi(quality_release::input), repository, actions);
    };
    check_throws_with(execute_with_failed_storage(), "Mock persistence API failed");
    check_equal(repository.size(), std::size_t{0});
    check_equal(actions.size(), std::size_t{0});
  }
  it("rejects schema-invalid API input before any decision is persisted") {
    auto input = quality_release::input;
    input[2].json = R"({"id":"lot-1"})";
    MockDecisionApi repository;
    MockActionApi actions;
    auto execute_invalid_payload = [&] {
      Program program(RF_SCENARIO_BUNDLE);
      evaluate(program, MockInputApi(input), repository, actions);
    };
    check_throws(execute_invalid_payload());
    check_equal(repository.size(), std::size_t{0});
    check_equal(actions.size(), std::size_t{0});
  }
}
