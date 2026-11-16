# RulesForge Examples

SDK application developers should start with the
[RulesForge + Toad DLL client guide](../docs/DEVELOPMENT_GUIDE.md). The examples
below are source-tree references; their support classes are not installed SDK
targets. Downstream clients use `RulesForge::RulesForge` and the public C ABI.

Run commands from the repository root after restoring the selected SDKs and
loading their environment as described in the [root README](../README.md#upstream-dependency-versions).
The introductory commands use the Windows LLVM-MinGW `native-release-user`
preset. The seven-scenario section uses `llvm-mingw-x64-release-user` with
`BUILD_TESTS=ON`.

Runnable examples keep their source and CMake files in `payments/`,
`script_program/`, `maintenance/`, `boiler_room/` and the seven domain directories
listed below. Shared preprocessing lives in `tools/`, shared sample documents in
`data/`, and the scenario mock host in `support/`. The older domain rule packs
retain their named directories below.

## Build and run

```powershell
cmake --preset native-release-user
cmake --build --preset native-release-user --parallel 3
ctest --preset native-release-user -R '^(capi_demo_.*|script_program_demo\.example|maintenance_(program_test|demo\.example)|documented_examples_test\.engine|examples_cli_and_data)$' --output-on-failure
```

The payments and Toad programs run through the existing CTest entries. The
rule-pack regression also compiles the documented domain rules and executes
the modular Gold-customer pricing scenario. CLI regressions check invalid data,
missing arrays/types and the scenario preprocessor.

| Example | Current scope |
|---|---|
| [Payments](#payments) | Runnable JSON/CSV C API example using imported schema |
| [RulesForge + Toad](#rulesforge--toad) | Runnable two-module RHS computation, typed results and transactional update |
| [Windowed maintenance](maintenance/README.md) | Waveform statistics, window eviction, cost optimization, rollback and generation isolation |
| [Boiler-room monitoring](boiler_room/README.md) | Nine source threads, RETE window evidence, correlated alerts and rule-constrained operating proposals with explained costs |
| [Production quality release](quality_release/README.md) | Measurement statistics, calibration joins, specification and dispersion gates |
| [Energy dispatch](energy_dispatch/README.md) | Exhaustive 27-plan search, trajectory costing, RETE feasibility and minimum-cost selection |
| [Cold-chain screening](cold_chain/README.md) | Interval exposure, an explicit cargo window, warmup and review decisions |
| [Production routing](production_routing/README.md) | Material/predecessor/qualification joins, cost ranking and completion-driven readiness |
| [Software release admission](release_gate/README.md) | Evidence joins, risk calculation, hard gates, freezes and approval |
| [Cross-system reconciliation](reconciliation/README.md) | Unit conversion, tolerance, missing counterparts and conflicting values |
| [Service entitlements](service_entitlements/README.md) | Usage validation, regional plan eligibility and exact integral-cent quotes |
| [Loan eligibility](loan-eligibility/README.md) | RFL compilation verified; external inputs still require schema migration |
| [Insurance pricing](insurance-pricing/README.md) | RFL compilation verified; external inputs still require schema migration |
| [Fraud detection](fraud-detection/README.md) | Dedicated C++ runner with flattened events and named entry point |
| [Order fulfillment](order-fulfillment/README.md) | RFL compilation verified; preprocessing supplied, schema migration required |
| [Travel booking](travel-booking/README.md) | RFL compilation verified; preprocessing supplied, schema migration required |
| [Modular rules](modular-rules/README.md) | Multi-file import and pricing execution verified |
| [Price plan](price-plan/price-plan-dispatch.rfl) | RFL compilation verified; input facts supplied by host code |

## Payments

```powershell
./build/native-release-user/bin/capi_demo.exe -r examples/payments/payments.rfl -j examples/payments/payments_test_data.json -m testOrders:Order -q OrdersWithDiscount -b order -f quantity,unitPrice,finalPrice
./build/native-release-user/bin/capi_demo.exe -r examples/payments/payments.rfl -c examples/payments/payments_test_data.csv -T Order -q OrdersWithDiscount -b order -f quantity,unitPrice,finalPrice
./build/native-release-user/bin/capi_demo.exe -r examples/payments/payments.rfl -c examples/payments/payments_test_data.csv -T Order --validation warn --trace --memory -q OrdersWithDiscount -b order -f quantity,finalPrice
```

RFL imports `payments.schema`; there is no separate CLI schema source. The
obsolete `-s` / `--schema` option is removed and rejected. JSON insertion,
mapping or binding errors stop the demo with a nonzero exit code before firing.
RFL `declare` remains for derived facts. Empty arrays are valid. The demo stops
at the first failed insertion, releases its resources and does not fire rules
over a partially loaded input.

The CLI uses the released Base SDK's `Salts::CmdParser` (`cmd_arger.h`).
Short and long options, `--name=value`, the two positional input paths, repeated
or comma-separated `--map` values and `--` are supported. Write short options
separately, such as `-v -h`. Help returns 0; malformed options return 1 with
diagnostics on stderr before RulesForge resources are created. At most 1024
`--map` arguments are accepted; excess input fails instead of truncating.

## RulesForge + Toad

[`script_program_demo.cpp`](script_program/script_program_demo.cpp) loads a Program bundle,
creates a session, inserts a JSON fact, fires the rules and closes its resources.
The formal example uses two script modules: `(amount + 5) * 2`, so amount 15
produces total 40 and marks the order done. Its CLI prints `fired=1`; the Program
regression separately checks the returned fact values.

```powershell
ctest --preset native-release-user -R '^script_program_demo\.example$' -V
```

The test build prepares the bundle, scripts, schema and native provider under
`build/native-release-user/rulesforge/tests/program-fixture`. To create your own
deployment, follow the [Script Program guide](../docs/SCRIPT_PROGRAM.md).
The executable accepts `<bundle.json> <fact-type> <fact-json>`.

## Windowed predictive maintenance

The [maintenance example](maintenance/README.md) combines waveform statistics in
Toad, a C++ CMeta module, a four-observation RulesForge window, constrained
maintenance decisions, forward chaining, transaction rollback and
generation-isolated policy updates.

```powershell
ctest --preset native-release-user -R '^maintenance_(program_test|demo\.example)$' -V
```

## Scenario preprocessing

The standard-library Python tool is
[`flatten_example_data.py`](tools/flatten_example_data.py).
It converts the shipped nested scenario documents to arrays of candidate facts.
It does not bind a schema or evaluate rules. Output is written under `build/`;
input and output must be different files.

```powershell
python examples/tools/flatten_example_data.py fraud examples/fraud-detection/fraud-test-data.json build/fraud-flat.json
python examples/tools/flatten_example_data.py order examples/order-fulfillment/order-test-data.json build/order-flat.json
python examples/tools/flatten_example_data.py travel examples/travel-booking/travel-test-data.json build/travel-flat.json
```

- Fraud: `accountProfiles`, `highRiskCountries`, `transactions` in scenario order.
- Order: `orders`, `orderItems`, `inventory`, `warehouses`, `promotions`.
  Each item inherits its enclosing `orderId`; conflicting IDs fail.
- Travel: `travelRequests`, `flightOptions`, `hotelOptions`, `visaRequirements`.

Run fraud detection after preprocessing:

```powershell
./build/native-release-user/bin/fraud_stream_runner.exe examples/fraud-detection/fraud-detection.rfl build/fraud-flat.json
```

The runner inserts transactions into `transaction-stream`, consolidates alerts
by transaction and highest severity, and sums emitted `FraudSignal` scores. It
uses internal C++ engine APIs; applications use the installed public C ABI.

For the other domain samples, move external-input declarations from RFL into a
DataBind `.schema` and import it from RFL before using `capi_demo`. Do not retain
a duplicate RFL declaration for the same imported type. Keep derived facts in
RFL. Preprocessing alone does not complete this schema migration or prove the
illustrative business outcomes in the scenario descriptions.

## Scenario suite architecture

The [scenario guide](../docs/SCENARIOS.md) maps each application to its expected
decisions and metric units, explains replacing mocks, and separates rule
feasibility from optimization search.

Run all seven domain demos, their formal regression tests and the shared mock API
tests through the existing preset:

```powershell
cmake --preset llvm-mingw-x64-release-user -DBUILD_TESTS=ON
cmake --build --preset llvm-mingw-x64-release-user --parallel 3
ctest --preset llvm-mingw-x64-release-user -L '^scenario$' -j 1 -V --no-tests=error
```

Each command must succeed before continuing. This label selects execution only;
the complete build graph remains enabled. Each demo accepts a bundle path and
can use either its generated `bundle-interpreter.json` or `bundle-jit.json`.

The domain scenarios under `quality_release/`, `energy_dispatch/`, `cold_chain/`,
`production_routing/`, `release_gate/`, `reconciliation/` and `service_entitlements/`
use the installed C ABI and released Toad provider. Each directory owns its schema,
RFL, computation module, mock input dataset, executable and formal tests.

The shared `support/` adapter owns only synchronous handle lifetimes, bounded mock
input, decision persistence and action delivery. Business constraints remain in
RFL; numeric transformations remain in Toad. We chose composition of these small
adapters instead of a new workflow runtime or a second rules interpreter.

The [ownership and capacity contract](../docs/SCENARIOS.md#ownership-and-mock-apis)
defines borrowed inputs, checkpoints, copied decisions and bounded mock storage.
The [failure and adapter contract](../docs/SCENARIOS.md#failure-and-production-adapters)
defines atomic commits, immutable IDs, explicit delivery and production
integration requirements. Mock state has no disk/crash durability.

Energy optimization searches a finite, documented candidate set. No external
solver or dependency is added. It proves a minimum over that set, not a global
optimum over continuous controls. A future large scheduling model can pass an
explicit model to a dedicated solver and return typed proposals through the same
computation boundary. Reverting these examples requires removing their directory
registrations; it does not require changing the engine or its public ABI.

Validation on 2026-11-16: Windows LLVM-MinGW x64 Release, released Toad 0.1.3,
complete build graph and **79/79 CTest passed**, including 15 scenario entries
and seven existing benchmarks. The new business/mock suites contain 47 passing
TinyTest cases; all seven business suites include an explicit JIT case. Raw local
results are `build/scenarios-full-ctest.log` and
`build/scenarios-full-results.xml`. Other platforms, sanitizers, real transports
and durable storage were not exercised by this qualification.

## Related documents

- [User guide](../docs/USER_GUIDE.md)
- [Data ingestion](../docs/DATA_INGESTION.md)
- [Public C ownership and error contract](../rulesforge/include/rules_forge.h)
- [DSL reference](../docs/dsl.md)
