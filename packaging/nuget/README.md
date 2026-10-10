# RulesForge.Native

Prebuilt native RulesForge SDK for consumers of the stable C ABI.

Package layout:

```
sdk/
  linux-x64/
  linux-arm64/
  windows-x64/
  macos-arm64/
```

Each RID contains the installed RulesForge CMake package, public `rules_forge.h`, and the native shared library/import library.

Dependencies:

- latest published (including prereleases) `Salts.Native`
- latest published (including prereleases) `SaltsUtils.Native`

These GitHub packages are deliberately not encoded as NuGet transitive dependencies because `.nuspec` dependencies cannot float. Consumers must restore both explicitly with `Version="*-*"` (or equivalent latest resolution) before configuring RulesForge.

DataBind is supplied only through SaltsUtils. There is no independent DataBind package or root.

The package intentionally contains only the installed RulesForge consumer surface. Build trees, tests, generated intermediate parser files, and third-party vcpkg packages are not copied into the package.

Typical CMake consumption restores the matching RID trees for RulesForge, Salts, and SaltsUtils, sets `SALTS_ROOT` and `SALTS_UTILS_ROOT`, then uses:

```cmake
find_package(RulesForge CONFIG REQUIRED
  PATHS "${RULES_FORGE_ROOT}"
  NO_DEFAULT_PATH)
target_link_libraries(my_app PRIVATE RulesForge::RulesForge)
```

The 0.9.3-rc.1 qualification uses Salts 2.3.0-rc.5 and SaltsUtils 4.3.0-rc.2, with DataBind ABI 10. Each SDK manifest records the exact producer versions and source commit. CMake reports the numeric package version 0.9.3; the C API and NuGet package report the full prerelease version.
