# 18: API stabilisation pass

**What to build:** A second full review and cleanup over the finished API, followed by a freeze of the public headers as the stable surface that engine integration (and other C++ projects) will build on. Includes the docs, a glossary, and an honest record of which platforms have been tested.

**Blocked by:** 05, 08, 17

**Status:** resolved

- [x] The review is written up like ticket 01's. Its findings are fixed or explicitly deferred.
- [x] Naming, result shapes, ownership semantics and error statuses are consistent across every public type.
- [x] Public headers contain no SDK types, OS handles or managed implementation details.
- [x] The README/usage guide covers every feature with short examples, plus deployment requirements (runtime, bridge, native libraries, component dependencies).
- [x] The glossary defines the core terms (host, bridge, component, handle, release vs collection, generation, internal call).
- [x] The consumer project from ticket 05 is updated to the final API and builds and runs.
- [x] A platform matrix states what has been verified on Windows, Linux and macOS, and notes CI as future work.

## Comments

### Resolution

- **Review:** `.scratch/dotnet-scripting/review-18.md`, in ticket 01's format. It covers:
  - consistency, header hygiene, ease of use (written as the script host ticket 19 will need) and performance (measured: about 120 ns per primitive call);
  - a capability comparison with Mono and Coral, documentation, and platforms.
  - The rule for each finding: fix anything that would be breaking later; defer anything additive, with a named follow-up.
- **Fixed:**
  - `CallStatus::BufferTooSmall` was removed: wire status 4 is now `InvalidResponse`, with no bridge ABI change.
  - `Array::FromObject` is private, reached through `Detail::ArrayAccess`.
  - The block helpers moved out of `Method.hpp` into a private `StringBlock.hpp`.
  - `CallResult` gained `operator*` and `operator->`, with an rvalue overload. It deliberately has no `operator bool`.
  - `InitializationResult` and `HostFxrDiscoveryResult` gained `Succeeded()`.
  - There is a new umbrella header, `<Iris/Iris.hpp>`.
  - Stale header comments and the compile-time error text were corrected.
  - The diagnostic buffer is no longer zeroed on every call (about 10–30% faster calls).
- **Docs:** the README covers:
  - every feature with examples, and a table of which C++ types cross where;
  - deployment requirements (runtime, bridge, nethost, components, shared assemblies, internal calls registered before load; no bridge reference needed);
  - threading, a Mono/Coral comparison, the checks and a platform matrix.
  - `GLOSSARY.md` at the repository root defines the core terms, including generation, release and collection.
  - ADR-0006 records the freeze: version 0.2.0, `SameMajorVersion`, additive-only, and a breaking change needs an ADR.
- **Consumer:** `Examples/Consumer` is a small script host on the final API:
  - one include;
  - discovering `IScript` types;
  - an object and an instance method;
  - a string, a struct and an array;
  - a call into native code;
  - a log sink.
  - `ConsumerPackage` installs, builds and runs it in Debug and Release.
- **Platforms:** Windows x64 / MSVC 19.44 is verified in Debug and Release. Linux and macOS have not been run (WSL Ubuntu 24.04 is available, but needs a .NET 10 SDK). CI is future work.
- **Deferred (additive):**
  - empty handles;
  - log sink before initialization;
  - member enumeration and attributes;
  - an object's runtime type;
  - enums;
  - an unload wait;
  - a fast invocation path and a benchmark.
  - Ticket 19 sets their priority and the threading model.
