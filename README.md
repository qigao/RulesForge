# RulesForge

RulesForge is a RETE-based rule engine for C/C++ embedding. It supports dynamic schema and data binding, then executes rules through a JIT-backed runtime.

Internally, RFL describes rules and working memory, TurboScript::DataBind handles schema-aware data binding, RETE plans rule propagation, and supported rule kernels run through the JIT execution path.

English: `README.md`
简体中文: `docs/zh-CN/README.md`

## What Is In This Repo

- `rulesforge/`: core C++ engine library
- `parser/`: standalone RFL parser library
- `capi/`: public shared library and C API in [`include/rule_forge.h`](/C:/projects/cpp/rulesforge/include/rule_forge.h)
- `docs/`: product docs, DSL reference, deployment notes, and examples
- `tools/rulesforge_schema_compiler/`: schema/code generation tool

## Stable Entry Points

If you are new to the project, start from the C API:

- Main API: [`include/rule_forge.h`](/C:/projects/cpp/rulesforge/include/rule_forge.h)
- Shared types: [`include/ruleforge_types.h`](/C:/projects/cpp/rulesforge/include/ruleforge_types.h)

The C++ engine API is available, but it is lower-level and spread across `rulesforge/include` and `parser/include`. For syntax details, use [`docs/dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md), which is already aligned with the current parser/runtime.

## Build

Prerequisites:

- CMake 3.20+
- A C17/C++20 toolchain
- Ninja
- `vcpkg`
- local packages for `TurboNet` and `TurboScript`

The top-level CMake expects these package roots:

- `TURBONET_ROOT`
- `TURBOSCRIPT_ROOT`
- `TURBO_UTILS`

Typical configure flow:

```bash
cmake -S . -B build \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DTURBONET_ROOT=/path/to/TurboNet \
  -DTURBOSCRIPT_ROOT=/path/to/TurboScript \
  -DTURBO_UTILS=/path/to/TurboNet

cmake --build build
ctest --test-dir build --output-on-failure
```

This repository also ships platform presets under `presets/`. Run `cmake --list-presets` and use the preset set that exists in your environment.

## 5-Minute Quickstart

The fastest real path is the bundled `capi_demo` executable.

1. Build the project.
2. Run the loan eligibility example:

```bash
./build/bin/capi_demo \
  -r docs/examples/loan-eligibility/loan-eligibility.rfl \
  -j docs/examples/loan-eligibility/loan-applications-sample.json \
  -m applications:com.bank.loan.LoanApplication \
  -q LoanDecisions \
  -b decision \
  -f applicationId,approved,approvedAmount,reason
```

You should see the rules compile, facts load, rules fire, and the query output printed from the `LoanDecisions` query.

## Documentation Map

- Quickstart: [`docs/QUICKSTART.md`](/C:/projects/cpp/rulesforge/docs/QUICKSTART.md)
- Beginner guide: [`docs/BEGINNER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/BEGINNER_GUIDE.md)
- User guide: [`docs/USER_GUIDE.md`](/C:/projects/cpp/rulesforge/docs/USER_GUIDE.md)
- DSL reference: [`docs/dsl.md`](/C:/projects/cpp/rulesforge/docs/dsl.md)
- Product readiness: [`docs/PRODUCT_READINESS.md`](/C:/projects/cpp/rulesforge/docs/PRODUCT_READINESS.md)
- Error catalog: [`docs/ERROR_CATALOG.md`](/C:/projects/cpp/rulesforge/docs/ERROR_CATALOG.md)
- C API contract: [`docs/C_API_CONTRACT.md`](/C:/projects/cpp/rulesforge/docs/C_API_CONTRACT.md)
- TurboScript DataBind/parser comparison: [`docs/TURBOSCRIPT_DATABIND_PARSER_COMPARISON.md`](/C:/projects/cpp/rulesforge/docs/TURBOSCRIPT_DATABIND_PARSER_COMPARISON.md)
- Deployment: [`docs/DEPLOYMENT.md`](/C:/projects/cpp/rulesforge/docs/DEPLOYMENT.md)
- Examples index: [`docs/examples/README.md`](/C:/projects/cpp/rulesforge/docs/examples/README.md)

## Current Review Summary

Before this doc pass, the repo had several product-facing documentation problems:

- no root English README
- stale examples that referenced missing headers or APIs such as `fact_builder.hpp` and `get_facts_of_type()`
- build commands that did not match the actual CMake preset/layout
- old product naming such as `Drills`
- Chinese and English entry docs pointing at the wrong files

Those entry-point docs have now been rewritten around the current code and public ABI.
