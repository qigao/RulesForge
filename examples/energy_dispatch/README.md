# Finite energy dispatch optimization

Select a feasible three-period battery plan with the lowest modeled grid-energy and battery-wear cost.

```text
27 Candidate plans + Site → Toad trajectory/cost → RETE Feasible plans → minimum-cost Decision
```

The mock API supplies the exhaustive Cartesian product {-2, 0, +2} kW over three one-hour periods. Positive power discharges the battery. Toad computes SOC, grid extrema and cost. RFL admits plans only when SOC stays within [reserve, capacity], final SOC reaches its target, no power is exported and grid demand stays below its limit. Two NOT joins reject candidates with a cheaper feasible competitor or an earlier equal-cost rank.

## Run

Restore and load the existing released SDK environment from the [root README](../../README.md#build).
Run each command only after the preceding command succeeds:

```powershell
cmake --preset llvm-mingw-x64-release-user -DBUILD_TESTS=ON
cmake --build --preset llvm-mingw-x64-release-user --parallel 3
ctest --preset llvm-mingw-x64-release-user -R '^energy_dispatch_(test|demo\.example)$' -V --no-tests=error
```

The executable also accepts a deployed bundle path:

```powershell
./build/llvm-mingw-x64-release-user/bin/energy_dispatch_demo.exe ./build/llvm-mingw-x64-release-user/examples/energy_dispatch/bundle/bundle-interpreter.json
```

Use `bundle-jit.json` to explicitly select released Toad JIT. Every domain's
formal test also executes its typed computation in that mode; there is no fallback.

## Expected decisions

`dispatch-1`: PROPOSE `P08`, battery schedule [-2, 2, 2], cost 2.72. With maxGridKw = 4, the winner becomes `P16`, [0, 2, 0], cost 2.84. With maxGridKw = 1, the result is NO_PLAN.

The demo runs the input twice with fresh sessions against the same mock repository
and sink. It prints the decisions and `persisted=1 delivered=1 replay=idempotent`.
`metric` means modeled cost in this example.

## Mock boundaries and verification

`input.hpp` contains immutable JSON responses returned by `MockInputApi`. They are
bound by `scenario.schema`; the host contains no second JSON parser or business-rule
implementation. RFL owns joins, gates and decisions. `calculations.toad` owns pure
computation. The [scenario host contract](../../docs/SCENARIOS.md#ownership-and-mock-apis)
defines ownership, capacity, atomic mock commits, idempotency and explicit delivery.
Mock persistence has no disk/crash durability and performs no real external actions.

An independent C++ enumeration checks the feasible-set minimum. Formal tests also
cover tightened-grid and infeasible cases, invalid physical inputs, and released
Toad JIT.

The optimum is exact over these 27 discrete plans. Periods are one hour, battery efficiency is 1, and initial/final SOC are 4/at least 2 kWh. This is not a continuous-control optimum or a production dispatch controller. Requests produce proposals; the action API remains a mock.

## What a solver would add

A solver searches for values of decision variables that satisfy constraints and
minimize an objective. Here the variables are three battery actions, the
constraints are SOC/grid limits, and the objective is grid cost plus battery wear.
The example can enumerate every assignment because there are only 3³ = 27.

With T periods, that enumeration grows as 3^T. At 24 periods it already contains
282,429,536,481 candidates. A larger deployment would formulate the model for an
appropriate linear, mixed-integer or constraint-programming solver instead of
materializing that entire set as facts. RulesForge can admit a planning request,
apply policy and validate typed proposals; a solver owns the optimization search.
Toad or an admitted native module can form the computation boundary. This example
adds no solver dependency and makes no claim about that future integration.
