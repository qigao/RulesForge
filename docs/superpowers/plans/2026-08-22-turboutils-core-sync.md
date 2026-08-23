# RulesForge TurboUtils Core Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild and install RulesForge against the current TurboUtils and TurboParser SDKs without changing its public rule, DataBind, ownership, or error semantics.

**Architecture:** RulesForge owns its C ABI marker, while TurboUtils and TurboParser remain one-way installed dependencies. TurboUtils STL is linked only by targets that compile the hash implementation, and current tlog/TinyTest entry points replace removed compatibility surfaces.

**Tech Stack:** C++20, C17 C API, CMake Presets, TurboUtils::Core/STL/TinyTest, TurboParser::Parser/DataBind, TinyTest.

**Spec:** User request to fix `C:\projects\cpp\rulesforge` after the TurboUtils Core migration.

## Global Constraints

- Preserve the existing RulesForge C function signatures, status codes, DataBind ABI 8 contract, serialization behavior, and ownership rules.
- Preserve the pre-existing changes in `CMakeUserPresets.json`, `docs/dsl.md`, and `presets/Compilers.json`.
- Use installed TurboUtils and TurboParser headers and exported targets only.
- Verify the rebuilt installed DLL no longer imports removed TurboUtils Core symbols.

---

### Task 1: Restore C API and dependency boundaries

**Files:**
- Create: `include/rules_forge_export.h`
- Modify: `include/rules_forge.h`
- Modify: `capi/CMakeLists.txt`
- Modify: `rulesforge/CMakeLists.txt`
- Modify: `parser/CMakeLists.txt`

**Interfaces:**
- Consumes: `TurboUtils::Core`, `TurboUtils::STL`, `TurboParser::Parser`, and `TurboParser::DataBind`.
- Produces: `RULES_FORGE_C_API` declarations and correctly linked `RulesForge`, `rfl_parser`, and `rules_forge` targets.

- [ ] **Step 1: Verify the failing build boundary**

Run: `cmake --build --preset win-release-user --target rules_forge`

Expected: FAIL on removed `CXX_C_API`, missing parser/DataBind headers, and the old hash header.

- [ ] **Step 2: Add the RulesForge-owned export header and target marker**

Define `RULES_FORGE_API`/`RULES_FORGE_C_API`, replace `CXX_C_API`, replace `SHARED_CXX` with `RULES_FORGE_BUILD`, and install the export header with the existing public include directory.

- [ ] **Step 3: Link each implementation dependency at its owning target**

Link `TurboUtils::STL`, `TurboParser::Parser`, and `TurboParser::DataBind` privately where their implementation headers/functions are compiled; retain existing public dependencies required by installed headers.

- [ ] **Step 4: Rebuild the focused target**

Run: `cmake --build --preset win-release-user --target rules_forge`

Expected: export and include-boundary errors are gone; any remaining current-SDK failures are reported directly.

### Task 2: Migrate logging and STL source usage

**Files:**
- Modify: `rulesforge/include/core/logging_control.hpp`
- Modify: `rulesforge/src/core/rfl_rete_defs.cpp`

**Interfaces:**
- Consumes: current one-message `TLOG_*` macros and `<turbostl/hash_map.h>`.
- Produces: the same preformatted log text and the same byte-hash result.

- [ ] **Step 1: Preserve the existing local formatter and pass its completed message to tlog**

Replace two-argument raw `TLOG_*` calls with one-message calls; do not change log levels or placement.

- [ ] **Step 2: Include the STL-owned hash header**

Replace `<turbo_hash.h>` with `<turbostl/hash_map.h>` while keeping `turbo_hash_bytes` call semantics.

- [ ] **Step 3: Build RulesForge and parser targets**

Run: `cmake --build --preset win-release-user --target RulesForge rfl_parser rules_forge`

Expected: PASS.

### Task 3: Migrate C++ TinyTest consumers

**Files:**
- Modify: C++ files that include `tinytest.h` or use removed typed assertion aliases.

**Interfaces:**
- Consumes: installed `tinytest.hpp` C++ assertions.
- Produces: unchanged test intent with current `check_equal`, comparison, string, container, pointer, and tolerance assertions.

- [ ] **Step 1: Build the test targets to expose removed TinyTest surfaces**

Run: `cmake --build --preset win-release-user`

Expected: FAIL only where old C++ TinyTest aliases remain.

- [ ] **Step 2: Switch C++ tests/benchmarks to `tinytest.hpp` and current assertions**

Preserve each assertion's actual/expected order, memory length, floating tolerance, and exception type.

- [ ] **Step 3: Run focused release smoke tests**

Run: `cmake --build --preset win-release-user --target rulesforge_release_smoke`

Expected: PASS.

### Task 4: Full verification, install, and downstream ABI proof

**Files:**
- Verify only; install artifacts go to the configured external package prefix.

**Interfaces:**
- Consumes: the rebuilt RulesForge package.
- Produces: a DLL usable by TurboFlow with no removed `turbo_hash_bytes` Core import.

- [ ] **Step 1: Fresh configure and full build**

Run: `cmake --fresh --preset win-release-user` then `cmake --build --preset win-release-user`.

- [ ] **Step 2: Run full RulesForge CTest**

Run: `ctest --preset win-release-user --output-on-failure`.

- [ ] **Step 3: Install the verified release package**

Run: `cmake --build --preset win-release-user --target install`.

- [ ] **Step 4: Verify DLL imports and TurboFlow regression tests**

Inspect `rules_forge.dll` imports, rebuild TurboFlow so its runtime copy is refreshed, then run the previously blocked TurboFlow CTest suite.

- [ ] **Step 5: Review the final patch**

Run: `git diff --check`, legacy API scans with `rg.exe`, and `git status --short`; confirm the three pre-existing dirty files remain preserved.
