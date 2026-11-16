#include "input.hpp"
#include "tests/fixture.hpp"
using namespace scenario_support;
suite("Finite energy dispatch optimization") {
    it("selects the exact minimum over the independently enumerated feasible set") {
        double reference = 1e100;
        for (int a : {-2, 0, 2}) for (int b : {-2, 0, 2}) for (int c : {-2, 0, 2}) {
            std::array dispatch{a, b, c};
            std::array demand{4, 6, 2};
            std::array price{0.1, 0.5, 0.2};
            int soc = 4;
            bool feasible = true;
            double cost = 0;
            for (int i = 0; i < 3; ++i) {
                soc -= dispatch[i];
                int grid = demand[i] - dispatch[i];
                feasible = feasible && soc >= 2 && soc <= 6 && grid >= 0 && grid <= 6;
                cost += grid * price[i] + std::abs(dispatch[i]) * 0.02;
            }
            if (feasible && soc >= 2 && cost < reference) reference = cost;
        }
        auto repository = run_fixture(energy_dispatch::input);
        check_within(repository.get("dispatch-1").metric, reference, 1e-10);
        check_within(reference, 2.72, 1e-10);
        check_equal(repository.get("dispatch-1").target.view(), std::string_view("P08"));
    }
    it("changes the optimum when the grid constraint is tightened") {
        auto input = energy_dispatch::input;
        input[0].json = R"({"id":"site-1","model":{"demand":[6,8,4],"renewable":[2,2,2],"price":[0.1,0.5,0.2],"initialKwh":4,"capacityKwh":6,"reserveKwh":2,"finalKwh":2,"maxGridKw":4,"wearPerKwh":0.02}})";
        auto repository = run_fixture(input);
        check_equal(repository.get("dispatch-1").target.view(), std::string_view("P16"));
        check_within(repository.get("dispatch-1").metric, 2.84, 1e-10);
    }
    it("reports infeasibility instead of selecting the cheapest invalid candidate") {
        auto input = energy_dispatch::input;
        input[0].json = R"({"id":"site-1","model":{"demand":[6,8,4],"renewable":[2,2,2],"price":[0.1,0.5,0.2],"initialKwh":4,"capacityKwh":6,"reserveKwh":2,"finalKwh":2,"maxGridKw":1,"wearPerKwh":0.02}})";
        auto repository = run_fixture(input);
        check_equal(repository.get("dispatch-1").action.view(), std::string_view("NO_PLAN"));
    }
    it("rejects a broken physical model at the calculation boundary") {
        auto input = energy_dispatch::input;
        input[0].json = R"({"id":"site-1","model":{"demand":[6,8,4],"renewable":[2,2,2],"price":[0.1,0.5,0.2],"initialKwh":4,"capacityKwh":1,"reserveKwh":2,"finalKwh":2,"maxGridKw":6,"wearPerKwh":0.02}})";
        check_throws_with(run_fixture(input), "invalid energy model");
    }
    it("optimizes through released Toad JIT with the same numeric result") {
        auto repository = run_fixture(energy_dispatch::input, RF_SCENARIO_JIT_BUNDLE);
        check_within(repository.get("dispatch-1").metric, 2.72, 1e-10);
    }
}
