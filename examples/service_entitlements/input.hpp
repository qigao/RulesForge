#pragma once
#include "runtime.hpp"
namespace service_entitlements {
using scenario_support::FactInput;
inline constexpr std::array input{
    FactInput{"Customer", R"({"id":"customer-1","region":"CN","active":true})"},
    FactInput{"Usage", R"({"customer":"customer-1","period":"2026-11","units":220,"checked":false})"},
    FactInput{"Plan", R"({"id":"basic","region":"CN","baseCents":1000,"includedUnits":100,"unitCents":20,"creditCents":0,"rank":1,"offered":true})"},
    FactInput{"Plan", R"({"id":"pro","region":"CN","baseCents":2500,"includedUnits":300,"unitCents":10,"creditCents":200,"rank":2,"offered":true})"},
    FactInput{"Plan", R"({"id":"other-region","region":"US","baseCents":1,"includedUnits":1000,"unitCents":0,"creditCents":0,"rank":0,"offered":true})"},
    FactInput{"Plan", R"({"id":"disabled","region":"CN","baseCents":1,"includedUnits":1000,"unitCents":0,"creditCents":0,"rank":0,"offered":false})"},
    FactInput{"QuoteRequest", R"({"id":"quote-1","customer":"customer-1","period":"2026-11"})"}
};
}
