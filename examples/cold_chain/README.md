# Cold-chain recent exposure screening

Combine interval measurements, a shipment policy and an explicit three-observation RETE window to produce review decisions.

```text
Reading + Shipment → Toad degree-minutes → Exposure window → Review → Decision
```

Each interval contributes max(temperature - upper, 0) × minutes. RulesForge sums the most recent three cargo-1 observations after filtering by cargo. The host fires each reading checkpoint before adding the next record, and adds distinct review requests at two points.

## Run

Restore and load the existing released SDK environment from the [root README](../../README.md#build).
Run each command only after the preceding command succeeds:

```powershell
cmake --preset llvm-mingw-x64-release-user -DBUILD_TESTS=ON
cmake --build --preset llvm-mingw-x64-release-user --parallel 3
ctest --preset llvm-mingw-x64-release-user -R '^cold_chain_(test|demo\.example)$' -V --no-tests=error
```

The executable also accepts a deployed bundle path:

```powershell
./build/llvm-mingw-x64-release-user/bin/cold_chain_demo.exe ./build/llvm-mingw-x64-release-user/examples/cold_chain/bundle/bundle-interpreter.json
```

Use `bundle-jit.json` to explicitly select released Toad JIT. Every domain's
formal test also executes its typed computation in that mode; there is no fallback.

## Expected decisions

`review-1`: HOLD, recent exposure 30 degree-minutes. After two new cool observations evict older exposure, `review-2`: CONTINUE, recent exposure 10. Before three observations, a review returns WAIT.

The demo runs the input twice with fresh sessions against the same mock repository
and sink. It prints the decisions and `persisted=2 delivered=2 replay=idempotent`.
`metric` means recent degree-minutes in this example.

## Mock boundaries and verification

`input.hpp` contains immutable JSON responses returned by `MockInputApi`. They are
bound by `scenario.schema`; the host contains no second JSON parser or business-rule
implementation. RFL owns joins, gates and decisions. `calculations.toad` owns pure
computation. The [scenario host contract](../../docs/SCENARIOS.md#ownership-and-mock-apis)
defines ownership, capacity, atomic mock commits, idempotency and explicit delivery.
Mock persistence has no disk/crash durability and performs no real external actions.

Formal tests cover: Window eviction, isolation from another shipment, explicit warmup, rejected nonpositive intervals, and released Toad JIT.

This is an arrival-ordered length window on a stateful Program, not a ContinuousSession event-time example. The partition is explicitly cargo-1. CONTINUE means this recent-window policy passed; historical thermal damage is not erased, and the example does not assess product fitness.
