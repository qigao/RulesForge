#pragma once
#include "runtime.hpp"
namespace production_routing {
using scenario_support::FactInput;
inline constexpr std::array input{
    FactInput{"Operation", R"({"id":"root","predecessor":"root","skill":"none","hours":0,"complete":true})"},
    FactInput{"Operation", R"({"id":"cut","predecessor":"root","skill":"cutting","hours":2,"complete":false})"},
    FactInput{"Operation", R"({"id":"coat","predecessor":"cut","skill":"coating","hours":1,"complete":false})"},
    FactInput{"Material", R"({"operation":"cut","ready":true})"},
    FactInput{"Material", R"({"operation":"coat","ready":true})"},
    FactInput{"Worker", R"({"id":"alice","skill":"cutting","hourlyCost":24,"rank":1,"available":true})"},
    FactInput{"Worker", R"({"id":"bob","skill":"coating","hourlyCost":20,"rank":2,"available":true})"},
    FactInput{"Machine", R"({"id":"cut-slow","skill":"cutting","speed":1,"hourlyCost":10,"rank":1,"available":true})"},
    FactInput{"Machine", R"({"id":"cut-fast","skill":"cutting","speed":2,"hourlyCost":18,"rank":2,"available":true})"},
    FactInput{"Machine", R"({"id":"cut-offline","skill":"cutting","speed":100,"hourlyCost":0,"rank":0,"available":false})"},
    FactInput{"Machine", R"({"id":"coat-1","skill":"coating","speed":1,"hourlyCost":20,"rank":3,"available":true})"},
    FactInput{"PlanRequest", R"({"id":"plan-cut","operation":"cut","done":false})"},
    FactInput{"PlanRequest", R"({"id":"plan-coat","operation":"coat","done":false})"}
};
}
