# Service entitlement and tariff selection

Choose the least expensive enabled regional plan for an active account and a validated usage period.

```text
Usage → Toad validation → Customer + eligible Plans → Toad integral-cent Quotes → RETE minimum → Decision
```

Amount in cents = max(0, baseCents + max(0, units - includedUnits) × unitCents - creditCents). Each integer input is bounded to [0, 1,000,000], so every product/sum remains below 2^53 in Toad's numeric representation. No fractional-cent rounding is performed. The credit floor at zero is part of this example's tariff contract.

## Run

Restore and load the existing released SDK environment from the [root README](../../README.md#build).
Run each command only after the preceding command succeeds:

```powershell
cmake --preset llvm-mingw-x64-release-user -DBUILD_TESTS=ON
cmake --build --preset llvm-mingw-x64-release-user --parallel 3
ctest --preset llvm-mingw-x64-release-user -R '^service_entitlements_(test|demo\.example)$' -V --no-tests=error
```

The executable also accepts a deployed bundle path:

```powershell
./build/llvm-mingw-x64-release-user/bin/service_entitlements_demo.exe ./build/llvm-mingw-x64-release-user/examples/service_entitlements/bundle/bundle-interpreter.json
```

Use `bundle-jit.json` to explicitly select released Toad JIT. Every domain's
formal test also executes its typed computation in that mode; there is no fallback.

## Expected decisions

At 220 units, `quote-1` selects `pro` for 2300 cents. At 110 units, `basic` wins for 1200 cents. A cheaper disabled plan and a plan in another region are excluded. An inactive customer receives DENY.

The demo runs the input twice with fresh sessions against the same mock repository
and sink. It prints the decisions and `persisted=1 delivered=1 replay=idempotent`.
`metric` means integral cents in this example.

## Mock boundaries and verification

`input.hpp` contains immutable JSON responses returned by `MockInputApi`. They are
bound by `scenario.schema`; the host contains no second JSON parser or business-rule
implementation. RFL owns joins, gates and decisions. `calculations.toad` owns pure
computation. The [scenario host contract](../../docs/SCENARIOS.md#ownership-and-mock-apis)
defines ownership, capacity, atomic mock commits, idempotency and explicit delivery.
Mock persistence has no disk/crash durability and performs no real external actions.

Formal tests cover: Regional/enabled eligibility, usage-driven plan changes, account denial, invalid usage and tariff bounds, and released Toad JIT.

This models integer-cent quotes, not tax accounting, settlement or arbitrary-precision money. Usage is validated before eligibility, including for inactive customers. Other currencies, rounding rules and tariff bounds require an explicit model change.
