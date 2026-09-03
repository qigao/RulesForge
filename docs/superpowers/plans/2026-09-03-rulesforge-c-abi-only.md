# RulesForge C ABI-Only Build Plan

> **For Codex:** Execute this plan task-by-task, preserving the C++ implementation while exposing only the installed C ABI.

**Goal:** Remove the standalone RulesForge C++ static-library product so C and C++ hosts both consume the shared C ABI.

**Architecture:** Compile the engine as an internal CMake object library and fold those objects into the final `RulesForge` shared target. Keep the parser archive internal, install only the C header/shared target, and describe the C ABI as the sole supported integration contract.

**Tech Stack:** C17 ABI, C++20 implementation, CMake 3.20+, MSVC/Ninja, CTest.

---

### Task 1: Capture the existing artifact behavior

**Files:**
- Inspect: `rulesforge/CMakeLists.txt`
- Inspect: `parser/CMakeLists.txt`
- Inspect: `capi/CMakeLists.txt`

- [x] Configure, build, and test the unchanged Release preset.
- [x] Confirm the old build emits `rulesforge/RulesForge.lib` as a standalone static engine archive.
- [x] Confirm the install rules export only `RulesForge` and the C headers.

### Task 2: Make the C ABI the only product boundary

**Files:**
- Modify: `rulesforge/CMakeLists.txt`
- Modify: `ProjectOptions.cmake`
- Modify: `docs/README.md`
- Modify: `docs/dsl.md`
- Modify: `docs/examples/README.md`

- [x] Change the engine implementation target from `STATIC` to `OBJECT`.
- [x] Remove the unused static/shared product option.
- [x] Document that C++ applications also integrate through `rules_forge.h` and the shared library.

### Task 3: Verify build, tests, package, and artifacts

**Files:**
- Verify: `build/Msvc-Release/`
- Verify: `build/Msvc/`
- Verify: installed RulesForge package

- [x] Fresh-configure, build, and run Release tests.
- [x] Verify the final target produces `RulesForge.dll` and its import library without a separate engine archive.
- [x] Fresh-configure, build, and run Debug tests.
- [x] Install both configurations and validate an installed C consumer.
- [x] Review the final diff and commit the isolated branch.
