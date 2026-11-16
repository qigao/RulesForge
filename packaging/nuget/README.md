# RulesForge.Native

Native SDK for the stable RulesForge C ABI. Each SDK contains `include/rules_forge.h`,
the shared library/import library, and `lib/cmake/RulesForge/RulesForgeTargets.cmake`.
Archives carry a `rulesforge-sdk-manifest.json` with the exact source commit,
producer workflow, profile and upstream versions.

The package layout is `sdk/windows-x64`, `sdk/linux-x64`, `sdk/macos-arm64` and
`sdk/android-arm64-v8a`. Build trees, tests, internal C++ engine headers and vcpkg
package trees are not installed as public SDK headers.

Dependencies are the exact published split SDKs documented in the root
[README](../../README.md#upstream-dependency-versions): Base 0.1.8, CMeta 0.1.7,
Utils 0.1.4, CNet 0.1.12, Toad 0.1.3 and vcpkg-cache 0.1.3. DataBind remains part of Utils.
No floating `Salts.Native` or `SaltsUtils.Native` NuGet dependency is declared.

Restore matching SDK/compiler/triplet roots before configuring a native consumer.
`RulesForgeConfig.cmake` enforces the producer's exact Base/CMeta/Utils/Toad
versions through `SALTS_BASE_ROOT`, `CMETA_ROOT`, `SALTS_UTILS_ROOT` and `TOAD_ROOT`.

```cmake
find_package(RulesForge CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE RulesForge::RulesForge)
```

The application calls the RulesForge DLL through `rules_forge.h`. For the current
LLVM-MinGW profile, the runtime/import filenames are `libRulesForge.dll` and
`libRulesForge.dll.a`. The imported target selects the matching files; source-tree
engine and example support targets are not downstream dependencies.

The [development guide](../../docs/DEVELOPMENT_GUIDE.md) covers a complete DLL
client and business deployment bundle. Program execution also requires the
matching `rulesforge_toad` provider, Toad runtime and dynamic dependency closure;
the RulesForge DLL alone is not the entire deployment.

The project version follows the current CMake source version, 0.1.0. Publishing
requires an explicit matching tag and successful complete CI build/test/install
jobs. Android verification covers cross-build and ELF alignment, not device
execution. Existing historical packages are not changed by this source migration.
