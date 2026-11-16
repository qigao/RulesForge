#pragma once
#include "runtime.hpp"
namespace cold_chain {
using scenario_support::FactInput;
inline constexpr std::array input{
    FactInput{"Shipment", R"({"id":"cargo-1","upper":6,"budget":25})"},
    FactInput{"Reading", R"({"id":"r1","shipment":"cargo-1","temperature":5,"minutes":10,"processed":false})", true},
    FactInput{"Reading", R"({"id":"r2","shipment":"cargo-1","temperature":10,"minutes":5,"processed":false})", true},
    FactInput{"Reading", R"({"id":"r3","shipment":"cargo-1","temperature":8,"minutes":5,"processed":false})", true},
    FactInput{"Review", R"({"id":"review-1","shipment":"cargo-1","done":false})", true},
    FactInput{"Reading", R"({"id":"r4","shipment":"cargo-1","temperature":5,"minutes":10,"processed":false})", true},
    FactInput{"Reading", R"({"id":"r5","shipment":"cargo-1","temperature":5,"minutes":10,"processed":false})", true},
    FactInput{"Review", R"({"id":"review-2","shipment":"cargo-1","done":false})"}
};
}
