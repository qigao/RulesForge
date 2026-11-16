#include "input.hpp"
#include "tests/fixture.hpp"
using namespace scenario_support;
suite("Manufacturing operation routing") {
    it("chooses a qualified available machine by duration-adjusted cost") {
        auto repository = run_fixture(production_routing::input);
        check_equal(repository.get("plan-cut").action.view(), std::string_view("ASSIGN"));
        check_equal(repository.get("plan-cut").target.view(), std::string_view("cut-fast"));
        check_equal(repository.get("plan-cut").metric, 42.0);
        check_equal(repository.get("plan-coat").action.view(), std::string_view("WAIT"));
    }
    it("requires material even when qualified workers and machines are available") {
        auto input = production_routing::input;
        input[3].json = R"({"operation":"cut","ready":false})";
        auto repository = run_fixture(input);
        check_equal(repository.get("plan-cut").action.view(), std::string_view("WAIT"));
    }
    it("unlocks the next operation after a completion event and a new planning request") {
        Program program(RF_SCENARIO_BUNDLE);
        Session session(program);
        session.load(MockInputApi(production_routing::input));
        session.insert("Completion", R"({"operation":"cut","processed":false})");
        session.fire();
        session.insert("PlanRequest", R"({"id":"plan-coat-after-cut","operation":"coat","done":false})");
        session.fire();
        MockDecisionApi repository;
        repository.commit(session.decisions().rows());
        check_equal(repository.get("plan-coat").action.view(), std::string_view("WAIT"));
        check_equal(repository.get("plan-coat-after-cut").target.view(), std::string_view("coat-1"));
        check_equal(repository.get("plan-coat-after-cut").metric, 40.0);
    }
    it("rejects a zero-speed admitted machine instead of substituting a cost") {
        auto input = production_routing::input;
        input[8].json = R"({"id":"cut-fast","skill":"cutting","speed":0,"hourlyCost":18,"rank":2,"available":true})";
        check_throws_with(run_fixture(input), "invalid routing cost model");
    }
    it("executes typed cost calculations with Toad JIT") {
        auto repository = run_fixture(production_routing::input, RF_SCENARIO_JIT_BUNDLE);
        check_equal(repository.get("plan-cut").metric, 42.0);
    }
}
