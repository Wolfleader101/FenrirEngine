# 05: Standalone library boundary

**What to build:** The hosting core can be consumed by any C++ project, not just FenrirEngine. It has no dependency on engine packages and ships a CMake install/export package. A small consumer project outside the engine tree builds against it and loads and calls a component. Engine builds with scripting OFF still never touch .NET.

**Blocked by:** 04

**Status:** ready-for-agent

- [ ] The core target depends on no other engine package. The naming and namespace decision (keep `Fenrir` or go neutral) is recorded in the ADR from 01 or a new ADR.
- [ ] `cmake --install` produces a package that a separate project can consume with `find_package`, including the managed bridge artefacts and native dependencies it needs.
- [ ] A consumer project outside the engine tree configures, builds, starts the host, and invokes a component, using documented commands.
- [ ] Consuming via `add_subdirectory` still works, and the engine's root build is unchanged.
- [ ] With scripting OFF, an engine configure and build succeeds with a deliberately unusable `dotnet` on PATH.
