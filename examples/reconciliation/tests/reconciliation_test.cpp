#include "input.hpp"
#include "tests/fixture.hpp"
using namespace scenario_support;
suite("Cross-system reconciliation") {
    it("separates normalized matches mismatches missing rows and conflicting values") {
        auto repository = run_fixture(reconciliation::input);
        check_equal(repository.get("check-1").action.view(), std::string_view("MATCH"));
        check_equal(repository.get("check-2").action.view(), std::string_view("MISMATCH"));
        check_within(repository.get("check-2").metric, 0.2, 1e-12);
        check_equal(repository.get("check-3").action.view(), std::string_view("MISSING"));
        check_equal(repository.get("check-4").action.view(), std::string_view("CONFLICT"));
    }
    it("uses explicit tolerance rather than requiring binary floating point equality") {
        auto input = reconciliation::input;
        input[4].json = R"({"key":"read-2","value":4.805})";
        auto repository = run_fixture(input);
        check_equal(repository.get("check-2").action.view(), std::string_view("MATCH"));
        check_within(repository.get("check-2").metric, 0.005, 1e-12);
    }
    it("does not silently pick one of two conflicting external values") {
        Program program(RF_SCENARIO_BUNDLE);
        Session session(program);
        session.insert("ExternalRecord", R"({"key":"read-1","value":9})");
        session.load(MockInputApi(reconciliation::input));
        MockDecisionApi repository;
        repository.commit(session.decisions().rows());
        check_equal(repository.get("check-1").reason.view(), std::string_view("conflicting-external-values"));
    }
    it("fails on an invalid conversion instead of assuming an identity scale") {
        auto input = reconciliation::input;
        input[0].json = R"({"device":"meter","scale":0,"offset":0,"tolerance":0.01})";
        check_throws_with(run_fixture(input), "invalid reconciliation unit model");
    }
    it("runs unit conversion through released Toad JIT") {
        auto repository = run_fixture(reconciliation::input, RF_SCENARIO_JIT_BUNDLE);
        check_equal(repository.get("check-1").metric, 0.0);
        check_equal(repository.get("check-4").action.view(), std::string_view("CONFLICT"));
    }
}
