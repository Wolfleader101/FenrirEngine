# 07: `ReloadWatcher` moves to `FenrirCore` with its tests; `FenrirScripting` uses it from there

**What to build:** the clockless, I/O-free reload decision that C# hot reload uses moves to `FenrirCore` as a public header, so the asset store's watcher (ticket 08) can use it too (ADR-0081). Its behaviour does not change.

- **The move:** `packages/FenrirScripting/src/ReloadWatcher.hpp` and `.cpp` become `packages/FenrirCore/include/FenrirCore/ReloadWatcher.hpp` and `src/ReloadWatcher.cpp`, in namespace `Fenrir` (out of `Detail`). The public header gets a doc block on every declaration.
- **Its tests:** `FenrirScripting/Tests/Rules/ReloadWatcherTests.cpp` moves to a new `FenrirCore/Tests/` with `fenrir_add_catch2()`, keeping its fake-clock cases unchanged.
- **Callers:** `GenerationLifecycle` includes it from `FenrirCore`. FenrirScripting already links FenrirCore; check that it does.

**Blocked by:** none.

**Touches:** `packages/FenrirCore/` (`CMakeLists.txt`, `include/FenrirCore/ReloadWatcher.hpp`, `src/ReloadWatcher.cpp`, a new `Tests/`), `packages/FenrirScripting/` (`src/ReloadWatcher.*` removed, `src/GenerationLifecycle.*`, `CMakeLists.txt`, `Tests/CMakeLists.txt`, `Tests/Rules/ReloadWatcherTests.cpp` removed), the root `CMakeLists.txt` if FenrirCore's tests need adding

**Status:** resolved

- [x] `ReloadWatcher`'s fake-clock tests pass in FenrirCore, unchanged in substance.
- [x] FenrirScripting builds and its tests pass with the watcher included from FenrirCore. `PublicHeaders` passes for FenrirCore.
- [x] Nothing in FenrirScripting still defines a reload watcher.
