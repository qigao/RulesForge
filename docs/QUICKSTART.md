# RulesForge Quickstart

This guide uses the public C API and the bundled `capi_demo` tool because that path matches the current install surface and examples in this repository.

## 1. Build

Requirements:

- CMake 3.20+
- C17/C++20 compiler
- Ninja
- `vcpkg`
- local package roots for `TurboNet`, `TurboScript`, and `TurboNet`

Generic configure command:

```bash
cmake -S . -B build \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DTURBONET_ROOT=/path/to/TurboNet \
  -DTURBOSCRIPT_ROOT=/path/to/TurboScript \
  -DTURBO_UTILS=/path/to/TurboNet

cmake --build build
```

If your machine already has repo-specific presets, `cmake --list-presets` will show them.

## 2. Run A Real Example

Use the bundled loan example:

```bash
./build/bin/capi_demo \
  -r docs/examples/loan-eligibility/loan-eligibility.rfl \
  -j docs/examples/loan-eligibility/loan-applications-sample.json \
  -m applications:com.bank.loan.LoanApplication \
  -q LoanDecisions \
  -b decision \
  -f applicationId,approved,approvedAmount,reason
```

What each flag means:

- `-r`: RFL rules file
- `-j`: JSON data file
- `-m`: map one JSON array to one fact type as `array:type`
- `-q`: query to execute after firing rules
- `-b`: query binding name to print
- `-f`: comma-separated fields to print

## 3. What Happens At Runtime

The demo does this:

1. `ruleforge_init()`
2. `ruleforge_kb_create()`
3. `ruleforge_kb_load_drl()`
4. `ruleforge_session_create()`
5. load facts from JSON or CSV
6. `ruleforge_session_fire_all_rules()`
7. `ruleforge_session_query()`

Those APIs are declared in [`include/rule_forge.h`](/C:/projects/cpp/rulesforge/include/rule_forge.h).

## 4. Try CSV Instead

```bash
./build/bin/capi_demo \
  -r capi/examples/payments.rfl \
  -c capi/examples/payments_test_data.csv \
  -T com.example.pricing.Order \
  -q OrdersWithDiscount \
  -f quantity,unitPrice,finalPrice
```

## 5. Next Documents

- Need plain-language onboarding: [`BEGINNER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/BEGINNER_GUIDE.md)
- Need runtime model and integration choices: [`USER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/USER_GUIDE.md)
- Need exact language support: [`dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md)
- Need production advice: [`DEPLOYMENT.md`](/C:/projects/cpp/rulesforge/docs/DEPLOYMENT.md)
