# RulesForge Deployment Guide

RulesForge is an embedded library, not a network service. The embedding process
owns isolation, scheduling, durable state, and delivery to external systems.

## Build and Package

The supported installed surface is:

- `include/rules_forge.h` for the C ABI;
- the `RulesForge` shared library;
- exported CMake package files under `lib/cmake/RulesForge`.

Build and test with the repository CMake presets. Package the header and binary
from the same build; verify `ruleforge_get_version()` during startup when the
host has a strict version requirement.

### SDK Alignment

RulesForge now requests Salts 2.3 and SaltsUtils 4.3 in both its source build and
exported CMake package. Salts 2.3 uses same-minor package compatibility; the old
`find_package(Salts 2.1)` request is rejected by the 2.3 SDK. DataBind is
provided by SaltsUtils under the exported target `Salts::DataBind`; its required
runtime version is at least 3.0.0 and its ABI must be exactly 10. Both configure
and `ruleforge_init()` enforce the DataBind contract.

The 2026-10-10 Windows x64 Release qualification uses the user-selected
[Salts.Native 2.3.0-rc.4](https://github.com/qigao/salts/releases/tag/v2.3.0-rc.4)
and [SaltsUtils.Native 4.3.0-rc.2](https://github.com/qigao/salts-utils/releases/tag/v4.3.0-rc.2).
Unicode is now owned by Salts; SaltsUtils requires the imported `Salts::Unicode`
and no longer exports its own copy. SaltsUtils's producer manifest records Salts
rc.2; the consumer combination tested here uses rc.4. Upgrade and roll back both
SDKs together. Salts 2.3 changes native ABI/SONAME; matching DataBind ABI 10 alone
does not make an old/new SDK mixture safe.

| Package | Windows manifest source | Restored NuGet SHA-256, matching the GitHub release asset |
| --- | --- | --- |
| Salts.Native 2.3.0-rc.4 | `233a0d1c2086808d85160ad70010e13b4c63d5c8` | `a9175909b27cf74152bc319b52c83c26c7a473e92450900d94d936a3290b9559` |
| SaltsUtils.Native 4.3.0-rc.2 | `c96a47b05292e3bbebba0b016c0b18c71bce8cde` | `579c639a43c1e6702a67717f5df9f43b5b571ed6f73f8e19d56d7ad0fd8ad153` |

Resolve current product packages, including release candidates, from the qigao GitHub Packages NuGet
feed, then expose their matching `sdk/<RID>` prefixes through the profile's
`SALTS_ROOT` and `SALTS_UTILS_ROOT`. The Windows Release preset uses packages
restored into this repository's ignored `build/native-sdk-rc` directory, leaving
shared SDK installations untouched. Other profiles retain their explicit SDK
roots, which must also be upgraded to the required SDK family before use. Inspect
each SDK's manifest to confirm its version, commit, and profile;
the manifest alone does not prove that installed files match a published package.
Published native SDK packages contain Release libraries; Debug/ASan presets
require separately built matching Debug SDKs.

[vcpkg-cache](https://github.com/qigao/vcpkg-cache) supplies the shared toolchain,
overlay ports, and third-party binary cache, while the product repositories own
the native SDK packages. Keep the current manifest and cache preset in use;
provide a `GITHUB_TOKEN` with `read:packages` in the parent environment for feed
access. Configure consumes installed SDKs; it does not restore product packages.

With NuGet CLI on `PATH` and `GITHUB_TOKEN` loaded into the parent process from
the ignored `.env` file or a credential provider, restore the latest published
packages for the Windows Release preset:

```text
nuget install Salts.Native -Prerelease -Source https://nuget.pkg.github.com/qigao/index.json -ConfigFile cmake/vcpkg-cache.nuget.config -OutputDirectory build/native-sdk-rc -ExcludeVersion -NonInteractive -NoHttpCache -DirectDownload
nuget install SaltsUtils.Native -Prerelease -Source https://nuget.pkg.github.com/qigao/index.json -ConfigFile cmake/vcpkg-cache.nuget.config -OutputDirectory build/native-sdk-rc -ExcludeVersion -NonInteractive -NoHttpCache -DirectDownload
```

These update commands supply no version argument and allow prereleases. The
qualification above restored the explicitly requested rc.4 / rc.2 versions;
that record does not pin future restoration. A future Salts 2.3.0-rc.5 can pass
the CMake version check if it retains the 2.3 compatibility contract, but it still
requires a new build and CTest run. CMake sees the numeric 2.3.0 version, not the
RC suffix. If restoration selects an incompatible family, configuration must
fail; do not fall back to an older SDK or bypass package checks.

For another upgrade, stage the pair in a new empty directory and switch this
profile's roots and build directory together. Do not overwrite a qualified SDK
or reuse a build directory containing the previous generation's runtime DLLs.
Keep package directories managed by NuGet, not by local SDK installation.
Neither CMake presets nor NuGet automatically read `.env`; never commit the
token or put its expanded value on a command line.

Upgrading from DataBind ABI 8 requires rebuilding RulesForge and its consumers
against the same headers and libraries. The C++ fact UUID alternative now uses
`cmeta_uuid_t` from `cmeta_uuid.h`, with the corresponding `cmeta_uuid_*`
operations from Salts 2.x. RulesForge's public C function signatures and status
codes are unchanged. Keep an ABI 8 deployment and its dependencies together for
rollback; do not mix an older DataBind runtime with this build.

DataBind 3 validates Binary declarations during schema loading, including loads
for text input. Such declarations must order fixed fields before groups and
variable data. Update incompatible rule-pack schemas explicitly; RulesForge
reports schema errors without rewriting declarations. Reordering fields can
change the binary layout, so version the schema and migrate binary producers
and consumers together. The migration's test fixtures only reorder fields used
by name in text payloads; their JSON/YAML/CSV/XML payloads retain the same names
and values.

From a Visual Studio developer shell on Windows, run:

```text
cmake --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
```

This run used the isolated `build/Msvc-Release-rc` directory. Configure and the
complete build succeeded; the focused C API/value/session/RHS/continuous set
passed 6/6, then the full non-benchmark CTest preset passed **38/38** in 2.82 s.
The preset excludes benchmark-labelled tests. Debug/ASan, other platforms,
installation and independent installed consumers were not run in this upgrade.
The prior `build/native-sdk` and `build/Msvc-Release` trees remain untouched;
rollback requires the matching prior RulesForge build/configuration and the
whole SDK pair, not swapping old DLLs into this build.

Use the corresponding `linux-release-user` presets on Linux.

## Lifecycle

Compile each rule pack into a knowledge base once, then stop mutating it before
sharing it. Native-predicate registration is an internal C++ facility, not a
public C API deployment step. Create separate stateful or continuous sessions
for independent workloads. RulesForge has no TurboScript execution-mode setting.

Never share a mutable session concurrently. If an async framework moves work
between threads, dispatch all calls for a session and its DataBind streams to
one fixed owner thread; a serialized executor that migrates work is insufficient
for the stream thread-affinity contract.

The internal session rejects nested fire and reset during an active fire call
before modifying state. The guard is released on normal return, early return
and exceptions; independent sessions can still execute from a listener. This
does not make sessions thread-safe or permit destruction during execution.
The C API retains its existing generic-error mapping for these exceptions.
The admission implementation and real listener tests were reused from commit
`8576a5af4d71ed347c513933ceae7f67695eedeb`. After integration, the complete
Release build succeeded, the focused session/RHS/C API set passed 3/3, and
`ctest --preset win-release-user --output-on-failure` passed 38/38 in 0.83 s.
Debug/ASan qualification remains pending matching Debug SDKs.

## Input Boundary

Treat `.schema` and RFL files as one versioned rule pack. Validate both during
CI using representative JSON/YAML/CSV/XML/binary payloads. Reject a deployment when
schema imports, rule compilation, or sample binding fails.
DataBind assumes trusted schemas. Payload validation and execution limits do not
make arbitrary third-party schema or rule packs an isolation boundary.

Choose complete-document APIs for bounded payloads already in memory. Choose
incremental streams for chunked input. The stream API does not provide an event
loop; see [Data ingestion](./DATA_INGESTION.md).

## Capacity and Backpressure

For stateful sessions, bound `fire_all_rules` and the number of inserted facts
at the host boundary. Destroy or explicitly clear sessions according to the
application lifecycle.

The firing count does not bound matching, one RHS, or total allocated bytes.
Input bytes, field sizes, queued work and retained results need host limits;
the [bounded execution design](./architecture/bounded-execution.md) describes
the remaining engine work. TurboScript limits do not extend through a native
RulesForge call automatically.

For continuous sessions, configure all capacity fields from expected event and
output rates. A `DRAIN_REQUIRED` result is backpressure: drain before accepting
more input. Acknowledge result batches in order so pending-result capacity is
released. Destroy acknowledged result handles when their snapshots are no longer
needed; acknowledgement does not free them or compact replay history.

## Failure Model

- Check every returned status and capture the last error before another API call.
- Treat compilation and schema errors as deployment failures.
- Destroy failed DataBind streams; do not retry them in place.
- Recreate an inconsistent session from authoritative input.
- Persist continuous input outside RulesForge when restart recovery is required.
- Do not introduce fallback parsing with a different schema or semantics.

## External Side Effects

Rules filter and derive facts; they do not call external services. Read queries
or continuous output snapshots after a successful commit, then perform side
effects in host code. Use a stable host run/session identity plus the result batch
ID, or domain event IDs, when delivery may be retried. Batch IDs alone are not
unique across sessions or restarts.

## Optional TurboScript And Plugin Hosts

RulesForge does not require TurboScript or Salts Plugin. Hosts that embed both
engines use their separate public C APIs, explicit value conversion, and separate
runtime owners. Do not install internal RulesForge C++ headers as a scripting
bridge or treat ExprTk values as RulesForge facts.

For a future native plugin adapter, verify Plugin ABI, application contract,
canonical Function/Interface signatures and lease coverage before invocation.
The inspected TurboScript 3.0.9 uses `turboscript.module` v1; the local migration
branch's v2 open/close and retryable unload are different contracts. Stop calls
and finish dependent cleanup before releasing the DLL lease. See
[the integration decision](./architecture/overview.md#turboscript--plugin-集成选择)
and [bounded execution](./architecture/bounded-execution.md) for unresolved
budget, transaction and retryable-close requirements.

The [ecosystem release matrix](./architecture/overview.md#同批生态发布与宿主边界)
also records TurboWasm 0.2.0, TurboDB 2.3.2, SaltsNet 1.1.0-rc.1 and CHttp
2.1.0-rc.1. These are optional host components, not dependencies installed or
qualified by this RulesForge build. In particular, CHttp Service/Web hosts must
migrate to `CHttp::App`; TurboWasm's NuGet package does not contain its WASI or
MIR backends. Qualify each host combination separately.

## Release Verification

### Native CI matrix and caches

The [native CI workflow](../.github/workflows/release-sdk.yml) runs on `master`,
`ci/**` pushes, pull requests to `master`, and manual dispatch. Each row uses
versioned user presets and performs the complete configured build, formal CTest
suite, and installation. `fail-fast: false` preserves evidence from all rows.

| Runner | Architecture / compiler | User preset | Qualification |
| --- | --- | --- | --- |
| Ubuntu 24.04 | x64 / GCC | `ci-linux-x64` | Release build, tests, install |
| Ubuntu 24.04 ARM | ARM64 / GCC | `ci-linux-arm64` | Native Release build, tests, install |
| Windows 2025 | x64 / MSVC | `ci-windows-x64` | Release build, tests, install in `VsDevCmd` |
| macOS 15 | ARM64 / AppleClang | `ci-macos-arm64` | Release build, tests, install |

The published SDKs are Release builds, so this matrix does not claim coherent
Debug/ASan qualification. Android/iOS device execution and benchmarks are also
outside this matrix. Benchmarks retain their local default but are excluded
from the CI build graph using `ENABLE_BENCHMARKS=OFF`. Existing tests and examples
stay enabled. AppleClang matches the published macOS SDK's native ABI.

Three separate caches serve different purposes:

- **ccache:** both C and C++ compiler launchers, 1 GB per matrix row, keyed by
  runner/profile, shared toolchain revision and preset/manifest inputs. Each run
  writes a unique cache key and restores the latest compatible prefix. Compiler
  content and headers remain part of ccache's own key; no unsafe sloppiness is
  enabled. Statistics are uploaded and included in the job summary.
- **vcpkg:** the canonical `qigao/vcpkg-cache` setup remains a read-only NuGet
  consumer, with a writable local binary cache persisted separately. Keys include
  platform/profile, cache-contract revision and manifest. Source builds on a cache
  miss remain part of normal manifest resolution.
- **Native SDK payloads:** NuGet package payloads may be reused, but every run
  resolves `*-*` with `--no-cache --force-evaluate`, including new RCs such as
  Salts rc.5. No assets/lock file, CMake build tree or installed SDK is cached.
  An incompatible latest SDK fails the normal CMake checks, without downgrade.

JUnit, CTest logs, configure logs, ccache statistics and installed artifacts are
retained for 14 days. Workflow permissions are read-only for source and packages;
this matrix does not publish packages or modify tags. Cache writes stay in GitHub
Actions storage. Fork PRs still require package access; missing access is a visible
failure, not grounds to expose a PAT or use `pull_request_target` for untrusted code.

The release branch incorporates the 0.9.2 baseline, including its WHILE-limit
fixes, licenses, and package metadata. The matrix runs the existing formal CTest
suite; the separate installed-package consumer project is not part of this graph.

For an RC, dispatch `release-sdk.yml` on the release branch with
`prepare_release=true`. The same build/test/install jobs produce four SDK
artifacts and a `rulesforge-native-nuget` package artifact with SHA-256 checksums.
After the whole run succeeds, create the version tag at that exact SHA and
dispatch `native-nuget-release.yml` from the tag with `ci_run_id`, `release_sha`,
and `tag`. The publisher accepts only a successful preparation from this
repository and a release branch, verifies the exact SHA/tag and checksum, and
uploads the accepted package unchanged. It does not rebuild or overwrite an
existing package. RC releases do not replace the latest stable release.

References: [ccache compiler support](https://ccache.dev/platform-compiler-language-support.html),
[cache key and configuration semantics](https://ccache.dev/manual/latest.html),
[GitHub runner images](https://github.com/actions/runner-images).

### Release checklist

- build the installable library and examples from a clean preset;
- run focused parser, engine, C API, DataBind, and continuous tests;
- compile a pure C consumer against the installed header;
- run every shipped example command;
- validate rule packs and representative payloads in CI;
- verify resource limits with peak-sized inputs;
- run sanitizer builds for changes touching ownership or stream lifecycle;
- record the RulesForge version, rule-pack version, and schema version together.

## Related Documents

- [User guide](./USER_GUIDE.md)
- [Data ingestion](./DATA_INGESTION.md)
- [Continuous engine](./CONTINUOUS_RULE_ENGINE_DESIGN.md)
- [DSL reference](./dsl.md)
