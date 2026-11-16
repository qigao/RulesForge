#include "input.hpp"
#include "tests/fixture.hpp"
using namespace scenario_support;
suite("Software release admission") {
    it("approves an evidenced release and preserves its risk calculation") {
        auto repository = run_fixture(release_gate::input);
        check_equal(repository.get("review-7").action.view(), std::string_view("APPROVE"));
        check_equal(repository.get("review-7").metric, 12.0);
    }
    it("lets a hard vulnerability gate take precedence over missing approval") {
        auto input = release_gate::input;
        input[2].json = R"({"build":"build-7","critical":1,"high":1})";
        input[4].json = R"({"build":"build-7","environment":"production","approved":false})";
        auto repository = run_fixture(input);
        check_equal(repository.get("review-7").reason.view(), std::string_view("critical-vulnerability"));
    }
    it("distinguishes a deployment freeze from a pending approval") {
        auto input = release_gate::input;
        input[0].json = R"({"id":"production","minCoverage":90,"maxRisk":20,"maxHigh":2,"frozen":true})";
        auto deferred = run_fixture(input);
        check_equal(deferred.get("review-7").action.view(), std::string_view("DEFER"));
        input = release_gate::input;
        input[4].json = R"({"build":"build-7","environment":"production","approved":false})";
        auto waiting = run_fixture(input);
        check_equal(waiting.get("review-7").action.view(), std::string_view("WAIT"));
    }
    it("rejects malformed evidence without issuing a deployment request") {
        auto input = release_gate::input;
        input[3].json = R"({"build":"build-7","size":-1,"rollbackMinutes":4})";
        check_throws_with(run_fixture(input), "invalid release evidence");
    }
    it("runs the admitted risk computation in released JIT") {
        auto repository = run_fixture(release_gate::input, RF_SCENARIO_JIT_BUNDLE);
        check_equal(repository.get("review-7").metric, 12.0);
    }
}
