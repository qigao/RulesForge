# Manufacturing operation routing

Find an available worker/machine combination only after material and predecessor requirements are satisfied.

```text
PlanRequest + Operation + predecessor + Material + Worker + Machine → Toad estimate → ranked assignment
```

Duration = nominal hours / machine speed. Cost = duration × (worker hourly cost + machine hourly cost). RETE performs qualification and readiness joins, then selects the lowest candidate cost with a deterministic rank tie break. A Completion event updates the predecessor fact; a fresh request can then plan the successor.

## Run

Restore and load the existing released SDK environment from the [root README](../../README.md#build).
Run each command only after the preceding command succeeds:

```powershell
cmake --preset llvm-mingw-x64-release-user -DBUILD_TESTS=ON
cmake --build --preset llvm-mingw-x64-release-user --parallel 3
ctest --preset llvm-mingw-x64-release-user -R '^production_routing_(test|demo\.example)$' -V --no-tests=error
```

The executable also accepts a deployed bundle path:

```powershell
./build/llvm-mingw-x64-release-user/bin/production_routing_demo.exe ./build/llvm-mingw-x64-release-user/examples/production_routing/bundle/bundle-interpreter.json
```

Use `bundle-jit.json` to explicitly select released Toad JIT. Every domain's
formal test also executes its typed computation in that mode; there is no fallback.

## Expected decisions

`plan-cut`: ASSIGN `cut-fast`, cost 42. `plan-coat`: WAIT while cutting is incomplete. After a Completion event for cut, a new coat request selects `coat-1`, cost 40. An unavailable cheap machine never becomes a candidate.

The demo runs the input twice with fresh sessions against the same mock repository
and sink. It prints the decisions and `persisted=2 delivered=2 replay=idempotent`.
`metric` means modeled cost in this example.

## Mock boundaries and verification

`input.hpp` contains immutable JSON responses returned by `MockInputApi`. They are
bound by `scenario.schema`; the host contains no second JSON parser or business-rule
implementation. RFL owns joins, gates and decisions. `calculations.toad` owns pure
computation. The [scenario host contract](../../docs/SCENARIOS.md#ownership-and-mock-apis)
defines ownership, capacity, atomic mock commits, idempotency and explicit delivery.
Mock persistence has no disk/crash durability and performs no real external actions.

Formal tests cover: Qualified minimum-cost selection, material gating, event-driven successor readiness, zero-speed failure, and released Toad JIT.

Assignments are proposals per request. They do not reserve resources or optimize overlapping jobs across a scheduling horizon. Persisted WAIT decisions remain historical; later requests use new IDs. The sample is not a durable workflow runtime.
