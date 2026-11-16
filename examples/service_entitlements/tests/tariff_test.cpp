#include "input.hpp"
#include "tests/fixture.hpp"
using namespace scenario_support;
suite("Service entitlement and plan selection") {
    it("selects the cheapest enabled regional plan in integral cents") {
        auto repository = run_fixture(service_entitlements::input);
        check_equal(repository.get("quote-1").target.view(), std::string_view("pro"));
        check_equal(repository.get("quote-1").metric, 2300.0);
    }
    it("changes the preferred plan when usage changes") {
        auto input = service_entitlements::input;
        input[1].json = R"({"customer":"customer-1","period":"2026-11","units":110,"checked":false})";
        auto repository = run_fixture(input);
        check_equal(repository.get("quote-1").target.view(), std::string_view("basic"));
        check_equal(repository.get("quote-1").metric, 1200.0);
    }
    it("does not grant a plan to an inactive customer") {
        auto input = service_entitlements::input;
        input[0].json = R"({"id":"customer-1","region":"CN","active":false})";
        auto repository = run_fixture(input);
        check_equal(repository.get("quote-1").action.view(), std::string_view("DENY"));
    }
    it("validates usage even for an inactive customer") {
        auto input = service_entitlements::input;
        input[0].json = R"({"id":"customer-1","region":"CN","active":false})";
        input[1].json = R"({"customer":"customer-1","period":"2026-11","units":-1,"checked":false})";
        check_throws_with(run_fixture(input), "usage outside bounded tariff model");
    }
    it("rejects prices outside the exact integer arithmetic model") {
        auto input = service_entitlements::input;
        input[2].json = R"({"id":"basic","region":"CN","baseCents":1000,"includedUnits":100,"unitCents":1000001,"creditCents":0,"rank":1,"offered":true})";
        check_throws_with(run_fixture(input), "tariff outside exact integer model");
    }
    it("retains the same price in released Toad JIT") {
        auto repository = run_fixture(service_entitlements::input, RF_SCENARIO_JIT_BUNDLE);
        check_equal(repository.get("quote-1").metric, 2300.0);
    }
}
