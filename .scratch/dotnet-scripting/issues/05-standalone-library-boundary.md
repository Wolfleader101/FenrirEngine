# 05: Standalone library boundary

**What to build:** The hosting core can be consumed by any C++ project, not just FenrirEngine. It has no dependency on engine packages and ships a CMake install/export package. A small consumer project outside the engine tree builds against it and loads and calls a component. Components share a declared set of contract assemblies as single copies. Engine builds with scripting OFF still never touch .NET.

**Blocked by:** 04

**Status:** ready-for-agent

- [ ] The core target depends on no other engine package. The naming and namespace decision (keep `Fenrir` or go neutral) is recorded in the ADR from 01 or a new ADR.
- [ ] `cmake --install` produces a package that a separate project can consume with `find_package`, including the managed bridge artefacts and native dependencies it needs.
- [ ] A consumer project outside the engine tree configures, builds, starts the host, and invokes a component, using documented commands.
- [ ] Consuming via `add_subdirectory` still works, and the engine's root build is unchanged.
- [ ] Shared contract assemblies (review 01, D4): the host can declare a set of assemblies (for example the public scripting API that scripts compile against) that every component shares as a single copy, instead of loading a private one. The bridge stays shared. Version, culture and public-key mismatches are rejected, as the bridge's are today.
- [ ] A check shows that two components see the same `Type` identity for a shared contract type, and that a component never loads a private copy of it.
- [ ] With scripting OFF, an engine configure and build succeeds with a deliberately unusable `dotnet` on PATH.
