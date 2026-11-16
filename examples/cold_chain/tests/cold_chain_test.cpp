#include "input.hpp"
#include "tests/fixture.hpp"
using namespace scenario_support;
suite("Cold chain exposure decisions") {
    it("changes decisions as old exposure leaves the three-observation window") {
        auto repository = run_fixture(cold_chain::input);
        check_equal(repository.get("review-1").action.view(), std::string_view("HOLD"));
        check_equal(repository.get("review-1").metric, 30.0);
        check_equal(repository.get("review-2").action.view(), std::string_view("CONTINUE"));
        check_equal(repository.get("review-2").metric, 10.0);
    }
    it("does not let unrelated cargo consume the explicit cargo window") {
        Program program(RF_SCENARIO_BUNDLE);
        Session session(program);
        MockInputApi source(std::span(cold_chain::input).first(4));
        session.load(source);
        session.insert("Shipment", R"({"id":"other","upper":6,"budget":25})");
        session.insert("Reading", R"({"id":"other-r1","shipment":"other","temperature":100,"minutes":10,"processed":false})");
        session.fire();
        session.insert("Review", R"({"id":"isolated","shipment":"cargo-1","done":false})");
        session.fire();
        MockDecisionApi repository;
        repository.commit(session.decisions().rows());
        check_equal(repository.get("isolated").metric, 30.0);
    }
    it("reports warmup explicitly instead of producing an empty successful result") {
        std::array input{cold_chain::input[0], cold_chain::input[1],
            FactInput{"Review", R"({"id":"early","shipment":"cargo-1","done":false})"}};
        auto repository = run_fixture(input);
        check_equal(repository.get("early").action.view(), std::string_view("WAIT"));
    }
    it("rejects invalid interval duration before persistence") {
        auto input = cold_chain::input;
        input[1].json = R"({"id":"r1","shipment":"cargo-1","temperature":5,"minutes":0,"processed":false})";
        check_throws_with(run_fixture(input), "interval duration must be positive");
    }
    it("runs window calculations in Toad JIT mode") {
        auto repository = run_fixture(cold_chain::input, RF_SCENARIO_JIT_BUNDLE);
        check_equal(repository.get("review-2").metric, 10.0);
    }
}
