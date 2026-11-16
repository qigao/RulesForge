# Software release admission

Combine test, vulnerability, change, environment and approval evidence into an explainable release decision.

```text
TestReport + SecurityReport + Change → Toad Risk → ordered RFL policy gates → Decision
```

Risk = 100 - coverage + 5 × high vulnerabilities + 0.01 × change size + 0.5 × rollback minutes. Hard vulnerability/test gates take precedence over freeze, risk review and approval gates. APPROVE additionally matches every positive prerequisite; it is not a default path.

## Run

Restore and load the existing released SDK environment from the [root README](../../README.md#build).
Run each command only after the preceding command succeeds:

```powershell
cmake --preset llvm-mingw-x64-release-user -DBUILD_TESTS=ON
cmake --build --preset llvm-mingw-x64-release-user --parallel 3
ctest --preset llvm-mingw-x64-release-user -R '^release_gate_(test|demo\.example)$' -V --no-tests=error
```

The executable also accepts a deployed bundle path:

```powershell
./build/llvm-mingw-x64-release-user/bin/release_gate_demo.exe ./build/llvm-mingw-x64-release-user/examples/release_gate/bundle/bundle-interpreter.json
```

Use `bundle-jit.json` to explicitly select released Toad JIT. Every domain's
formal test also executes its typed computation in that mode; there is no fallback.

## Expected decisions

`review-7`: APPROVE production, risk 12. Critical vulnerabilities return BLOCK, a deployment freeze returns DEFER, and a pending or missing approval returns WAIT. Each decision records its rule-selected reason.

The demo runs the input twice with fresh sessions against the same mock repository
and sink. It prints the decisions and `persisted=1 delivered=1 replay=idempotent`.
`metric` means illustrative risk score in this example.

## Mock boundaries and verification

`input.hpp` contains immutable JSON responses returned by `MockInputApi`. They are
bound by `scenario.schema`; the host contains no second JSON parser or business-rule
implementation. RFL owns joins, gates and decisions. `calculations.toad` owns pure
computation. The [scenario host contract](../../docs/SCENARIOS.md#ownership-and-mock-apis)
defines ownership, capacity, atomic mock commits, idempotency and explicit delivery.
Mock persistence has no disk/crash durability and performs no real external actions.

Formal tests cover: Positive admission, hard-gate precedence, freeze versus approval, malformed evidence rejection, and released Toad JIT.

The risk formula and thresholds are illustrative. The mock action API records the result without deploying software or changing permissions. Partial evidence cannot qualify for APPROVE; the host treats a scenario with no decision as an error.
