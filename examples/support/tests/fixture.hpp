#pragma once
#include "runtime.hpp"
#include <tinytest.hpp>

namespace scenario_support {
inline MockDecisionApi run_fixture(std::span<FactInput const> input,
                                   char const* bundle = RF_SCENARIO_BUNDLE) {
    Program program(bundle);
    MockDecisionApi repository;
    MockActionApi actions;
    evaluate(program, MockInputApi(input), repository, actions);
    return repository;
}
}
