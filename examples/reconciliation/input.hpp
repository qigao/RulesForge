#pragma once
#include "runtime.hpp"
namespace reconciliation {
using scenario_support::FactInput;
inline constexpr std::array input{
    FactInput{"UnitModel", R"({"device":"meter","scale":0.001,"offset":0,"tolerance":0.01})"},
    FactInput{"SourceRecord", R"({"key":"read-1","event":"a1","device":"meter","value":2500})"},
    FactInput{"ExternalRecord", R"({"key":"read-1","value":2.5})"},
    FactInput{"SourceRecord", R"({"key":"read-2","event":"a2","device":"meter","value":4800})"},
    FactInput{"ExternalRecord", R"({"key":"read-2","value":5})"},
    FactInput{"SourceRecord", R"({"key":"read-3","event":"a3","device":"meter","value":1000})"},
    FactInput{"SourceRecord", R"({"key":"read-4","event":"a4","device":"meter","value":1000})"},
    FactInput{"SourceRecord", R"({"key":"read-4","event":"b4","device":"meter","value":2000})"},
    FactInput{"ExternalRecord", R"({"key":"read-4","value":1})"},
    FactInput{"CheckRequest", R"({"id":"check-1","key":"read-1"})"},
    FactInput{"CheckRequest", R"({"id":"check-2","key":"read-2"})"},
    FactInput{"CheckRequest", R"({"id":"check-3","key":"read-3"})"},
    FactInput{"CheckRequest", R"({"id":"check-4","key":"read-4"})"}
};
}
