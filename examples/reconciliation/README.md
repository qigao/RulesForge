# Cross-system unit reconciliation

Reconcile records from two mock systems while distinguishing unit differences, missing records and conflicting source values.

```text
CheckRequest + SourceRecord + ExternalRecord + UnitModel → Toad normalization/difference → Decision
```

Normalized value = source value × scale + offset; the absolute difference is compared with an explicit RFL tolerance. Conflicting values for one business key are classified before a comparison can select a source. Source units are Wh and external units kWh in the supplied meter fixture.

## Run

Restore and load the existing released SDK environment from the [root README](../../README.md#build).
Run each command only after the preceding command succeeds:

```powershell
cmake --preset llvm-mingw-x64-release-user -DBUILD_TESTS=ON
cmake --build --preset llvm-mingw-x64-release-user --parallel 3
ctest --preset llvm-mingw-x64-release-user -R '^reconciliation_(test|demo\.example)$' -V --no-tests=error
```

The executable also accepts a deployed bundle path:

```powershell
./build/llvm-mingw-x64-release-user/bin/reconciliation_demo.exe ./build/llvm-mingw-x64-release-user/examples/reconciliation/bundle/bundle-interpreter.json
```

Use `bundle-jit.json` to explicitly select released Toad JIT. Every domain's
formal test also executes its typed computation in that mode; there is no fallback.

## Expected decisions

`check-1`: MATCH after 2500 Wh becomes 2.5 kWh. `check-2`: MISMATCH with difference 0.2. `check-3`: MISSING counterpart. `check-4`: CONFLICT between two source values.

The demo runs the input twice with fresh sessions against the same mock repository
and sink. It prints the decisions and `persisted=4 delivered=4 replay=idempotent`.
`metric` means absolute normalized difference in this example.

## Mock boundaries and verification

`input.hpp` contains immutable JSON responses returned by `MockInputApi`. They are
bound by `scenario.schema`; the host contains no second JSON parser or business-rule
implementation. RFL owns joins, gates and decisions. `calculations.toad` owns pure
computation. The [scenario host contract](../../docs/SCENARIOS.md#ownership-and-mock-apis)
defines ownership, capacity, atomic mock commits, idempotency and explicit delivery.
Mock persistence has no disk/crash durability and performs no real external actions.

Formal tests cover: All four classifications, tolerance-aware floating comparison, conflicting external records, invalid scale failure, and released Toad JIT.

This demonstrates bounded in-memory reconciliation, not database change capture or distributed deduplication. Source business keys, decision IDs and transport event IDs have different meanings. No identity conversion is substituted when a model is invalid.
