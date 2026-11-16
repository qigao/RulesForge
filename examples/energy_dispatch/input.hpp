#pragma once
#include "runtime.hpp"
namespace energy_dispatch {
using scenario_support::FactInput;
// Exhaustive Cartesian product: three one-hour periods, {-2, 0, +2} kW each.
inline constexpr std::array input{
    FactInput{"Site", R"({"id":"site-1","model":{"demand":[6,8,4],"renewable":[2,2,2],"price":[0.1,0.5,0.2],"initialKwh":4,"capacityKwh":6,"reserveKwh":2,"finalKwh":2,"maxGridKw":6,"wearPerKwh":0.02}})"},
    FactInput{"Candidate", R"({"id":"P00","rank":0,"plan":{"battery":[-2,-2,-2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P01","rank":1,"plan":{"battery":[-2,-2,0]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P02","rank":2,"plan":{"battery":[-2,-2,2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P03","rank":3,"plan":{"battery":[-2,0,-2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P04","rank":4,"plan":{"battery":[-2,0,0]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P05","rank":5,"plan":{"battery":[-2,0,2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P06","rank":6,"plan":{"battery":[-2,2,-2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P07","rank":7,"plan":{"battery":[-2,2,0]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P08","rank":8,"plan":{"battery":[-2,2,2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P09","rank":9,"plan":{"battery":[0,-2,-2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P10","rank":10,"plan":{"battery":[0,-2,0]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P11","rank":11,"plan":{"battery":[0,-2,2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P12","rank":12,"plan":{"battery":[0,0,-2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P13","rank":13,"plan":{"battery":[0,0,0]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P14","rank":14,"plan":{"battery":[0,0,2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P15","rank":15,"plan":{"battery":[0,2,-2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P16","rank":16,"plan":{"battery":[0,2,0]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P17","rank":17,"plan":{"battery":[0,2,2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P18","rank":18,"plan":{"battery":[2,-2,-2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P19","rank":19,"plan":{"battery":[2,-2,0]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P20","rank":20,"plan":{"battery":[2,-2,2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P21","rank":21,"plan":{"battery":[2,0,-2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P22","rank":22,"plan":{"battery":[2,0,0]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P23","rank":23,"plan":{"battery":[2,0,2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P24","rank":24,"plan":{"battery":[2,2,-2]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P25","rank":25,"plan":{"battery":[2,2,0]},"assessed":false})"},
    FactInput{"Candidate", R"({"id":"P26","rank":26,"plan":{"battery":[2,2,2]},"assessed":false})"},
    FactInput{"DispatchRequest", R"({"id":"dispatch-1"})"}
};
}
