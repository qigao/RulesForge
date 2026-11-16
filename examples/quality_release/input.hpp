#pragma once
#include "runtime.hpp"
namespace quality_release {
using scenario_support::FactInput;
inline constexpr std::array input{
    FactInput{"ProductSpec", R"({"batch":"lot-1","limits":{"lower":9,"upper":11,"maxDispersion":0.01}})"},
    FactInput{"Calibration", R"({"machine":"press-1","valid":true})"},
    FactInput{"Batch", R"({"id":"lot-1","machine":"press-1","samples":{"values":[9.9,10,10.1,10,10.1,9.9]},"assessed":false})"}
};
}
