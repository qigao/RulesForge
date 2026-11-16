# Production quality release

Join a batch's six measurements, product specification and machine calibration before issuing RELEASE, RECHECK or HOLD.

```text
Batch + ProductSpec → Toad statistics → Metrics + Calibration → Decision
```

Toad computes the population mean and variance, then dispersion = variance / (upper - lower)². It separately reports whether every sample lies inside the specification. RFL owns the calibration, hard-limit and dispersion gates.

## Run

Restore and load the existing released SDK environment from the [root README](../../README.md#build).
Run each command only after the preceding command succeeds:

```powershell
cmake --preset llvm-mingw-x64-release-user -DBUILD_TESTS=ON
cmake --build --preset llvm-mingw-x64-release-user --parallel 3
ctest --preset llvm-mingw-x64-release-user -R '^quality_release_(test|demo\.example)$' -V --no-tests=error
```

The executable also accepts a deployed bundle path:

```powershell
./build/llvm-mingw-x64-release-user/bin/quality_release_demo.exe ./build/llvm-mingw-x64-release-user/examples/quality_release/bundle/bundle-interpreter.json
```

Use `bundle-jit.json` to explicitly select released Toad JIT. Every domain's
formal test also executes its typed computation in that mode; there is no fallback.

## Expected decisions

`lot-1`: RELEASE, target `press-1`, dispersion approximately 0.00166666667. An expired calibration produces HOLD even when the samples pass. Wide but in-range samples produce RECHECK; an out-of-range sample produces HOLD.

The demo runs the input twice with fresh sessions against the same mock repository
and sink. It prints the decisions and `persisted=1 delivered=1 replay=idempotent`.
`metric` means normalized population variance in this example.

## Mock boundaries and verification

`input.hpp` contains immutable JSON responses returned by `MockInputApi`. They are
bound by `scenario.schema`; the host contains no second JSON parser or business-rule
implementation. RFL owns joins, gates and decisions. `calculations.toad` owns pure
computation. The [scenario host contract](../../docs/SCENARIOS.md#ownership-and-mock-apis)
defines ownership, capacity, atomic mock commits, idempotency and explicit delivery.
Mock persistence has no disk/crash durability and performs no real external actions.

Formal tests cover stable release, calibration gates, dispersion versus hard-limit
failures, invalid-model propagation without persistence/delivery, rejected
schema-invalid input, persistence failure after computation, and released Toad JIT.

This is a six-sample demonstration policy, not a qualified process capability model. Missing required evidence is not a release. Decisions are immutable snapshots; a new assessment must use a new decision ID.
