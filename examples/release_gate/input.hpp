#pragma once
#include "runtime.hpp"
namespace release_gate {
using scenario_support::FactInput;
inline constexpr std::array input{
    FactInput{"Environment", R"({"id":"production","minCoverage":90,"maxRisk":20,"maxHigh":2,"frozen":false})"},
    FactInput{"TestReport", R"({"build":"build-7","coverage":96,"passed":true})"},
    FactInput{"SecurityReport", R"({"build":"build-7","critical":0,"high":1})"},
    FactInput{"Change", R"({"build":"build-7","size":100,"rollbackMinutes":4})"},
    FactInput{"Approval", R"({"build":"build-7","environment":"production","approved":true})"},
    FactInput{"Review", R"({"id":"review-7","build":"build-7","environment":"production","assessed":false})"}
};
}
