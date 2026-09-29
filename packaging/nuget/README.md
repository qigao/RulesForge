# RulesForge.Native

Prebuilt native RulesForge SDK for consumers of the stable C ABI.

Package layout:

```
sdk/
  linux-x64/
  windows-x64/
```

Each RID contains the installed RulesForge CMake package, public `rules_forge.h`, and the native shared library/import library.

Dependencies:

- `Salts.Native 1.8.3`
- `SaltsUtils.Native 4.1.3`

DataBind is supplied only through SaltsUtils. There is no independent DataBind package or root.

The package intentionally contains only the installed RulesForge consumer surface. Build trees, tests, generated intermediate parser files, and third-party vcpkg packages are not copied into the package.

Typical CMake consumption restores the matching RID trees for RulesForge, Salts, and SaltsUtils, sets `SALTS_ROOT` and `SALTS_UTILS_ROOT`, then uses:

```cmake
find_package(RulesForge 0.9 CONFIG REQUIRED
  PATHS "${RULES_FORGE_ROOT}"
  NO_DEFAULT_PATH)
target_link_libraries(my_app PRIVATE RulesForge::RulesForge)
```
