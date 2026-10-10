# 05: Standalone library boundary

**What to build:** The hosting core can be consumed by any C++ project, not just FenrirEngine. It has no dependency on engine packages and ships a CMake install/export package. A small consumer project outside the engine tree builds against it and loads and calls a component. Components share a declared set of contract assemblies as single copies. Engine builds with scripting OFF still never touch .NET.

**Blocked by:** 04

**Status:** resolved

- [x] The core target depends on no other engine package. The naming and namespace decision (keep `Fenrir` or go neutral) is recorded in the ADR from 01 or a new ADR.
- [x] `cmake --install` produces a package that a separate project can consume with `find_package`, including the managed bridge artefacts and native dependencies it needs.
- [x] A consumer project outside the engine tree configures, builds, starts the host, and invokes a component, using documented commands.
- [x] Consuming via `add_subdirectory` still works, and the engine's root build is unchanged.
- [x] Shared contract assemblies (review 01, D4): the host can declare a set of assemblies (for example the public scripting API that scripts compile against) that every component shares as a single copy, instead of loading a private one. The bridge stays shared. Version, culture and public-key mismatches are rejected, as the bridge's are today.
- [x] A check shows that two components see the same `Type` identity for a shared contract type, and that a component never loads a private copy of it.
- [x] With scripting OFF, an engine configure and build succeeds with a deliberately unusable `dotnet` on PATH.

## Comments

**Done.**
- **Rename:** the library is now **Iris** (`docs/adr/0003-name-the-hosting-library-iris.md`). It lives in `packages/Iris`, with namespace `Iris`, target `Iris::Iris` and bridge `Iris.Bridge`. It has no dependency on any engine package.
- **Bridge build:** CMake builds the bridge once (Release), re-running only when its sources change.
- **Install:** `cmake --install` produces a package containing:
  - the library and headers;
  - `nethost`;
  - the bridge in `share/Iris/bridge`;
  - `IrisConfig.cmake` with `Iris::Nethost` and an `iris_deploy(target)` helper.
- **Consumer:** `Examples/Consumer` is a standalone `find_package(Iris)` project. The `ConsumerPackage` CTest installs the build into a fresh prefix, then configures, builds and runs the consumer against it (passes in Debug and Release).
- **Shared contract assemblies (D4):** `HostConfiguration::sharedAssemblies` adds a `RegisterSharedAssembly` bridge export (table version 10). The decision is recorded in `docs/adr/0004-shared-contract-assemblies.md`. Native and managed checks show one contract copy and one type identity across components, and that the unshared case falls back to a private copy.
- **Engine root:**
  - With `FENRIR_ENABLE_SCRIPTING=OFF`, the root configure succeeds with `dotnet` removed from `PATH` and `DOTNET_ROOT` empty; the log mentions neither .NET nor Iris.
  - With it `ON`, Iris and its bridge build through `add_subdirectory`, and examples, tests and install rules default off.
  - Both root configures needed `-DASSIMP_BUILD_TESTS=OFF`. The vendored assimp is missing its `test/` directory, which is unrelated to Iris.
- 10/10 CTests pass in Debug and Release on Windows.

Instead of an unusable `dotnet` on `PATH`, the scripting-OFF check removed `dotnet` from `PATH` entirely. That is the stronger test: Iris fails to configure without `dotnet`, so a passing configure proves nothing looked for it.
