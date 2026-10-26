# 01: A `ComponentLoader` seam under `GenerationLifecycle`, with an Iris adapter, a fake, and the reload rules as Rules tests

**What to build:** The seam the lifecycle loads through, so its rules are tested with no .NET (spec: "`ComponentLoader`: the seam under `GenerationLifecycle`").

- **`src/ComponentLoader.hpp`** (new): `LoadedComponent` (abstract: `Release()`, `WaitForUnload(timeout)`) and `ComponentLoader` (abstract: `Load(path) -> CallResult<std::unique_ptr<LoadedComponent>>`), each with a Doxygen block in the spec's words.
- **`src/GenerationLifecycle.{hpp,cpp}`**: `Configure` takes `ComponentLoader&` in place of `Iris::Host*`; `m_component` and each `Retired` hold `std::unique_ptr<LoadedComponent>`; `Hooks::reconcile` and `Hooks::start` take `LoadedComponent&`; `Component()` returns `LoadedComponent*`. Behaviour, log messages and ordering are unchanged (this is a refactor; the .NET reload tests pin them). The header names nothing of Iris beyond `CallResult` and `ComponentUnloadState`.
- **`src/Scripting.cpp`**: a private `IrisComponentLoader` over the `State`'s `Iris::Host`, whose `LoadedComponent` owns the `Iris::Component`, with `static Iris::Component& Of(LoadedComponent&)`. The `reconcile` and `start` hooks and `Scripting::LoadedComponent()` go through `Of`. `State` owns the loader beside `host` and configures the lifecycle with it.
- **`Tests/Rules/FakeComponentLoader.hpp`** (new, test code): the fake with the spec's knobs (fail the Nth load or a given path; `Pending` for the first N waits of a token, `Collected` after; refuse a release with `InUse`) and its records (paths loaded in order, tokens released).
- **`Tests/Rules/GenerationLifecycleTests.cpp`** (new) in `FenrirScriptingRulesTests`: a scratch directory holding a dummy assembly file (and a `.deps.json` naming a second dummy dll beside it), hooks that record their calls, and the fake loader. Cases are the rules below.
- **`Tests/CMakeLists.txt`**: the new test file and header added to the Rules executable's sources.
- **`README.md`** (Tests section): one sentence naming the lifecycle Rules tests and the fake loader.

**Blocked by:** none

**Touches:** `packages/FenrirScripting/src/ComponentLoader.hpp`, `packages/FenrirScripting/src/GenerationLifecycle.hpp`, `packages/FenrirScripting/src/GenerationLifecycle.cpp`, `packages/FenrirScripting/src/Scripting.cpp`, `packages/FenrirScripting/Tests/Rules/`, `packages/FenrirScripting/Tests/CMakeLists.txt`, `packages/FenrirScripting/README.md`

**Status:** ready-for-agent

- [ ] Rules tests (no .NET), each one behaviour: `Load` copies the assembly and the dlls its deps file names and loads the copy, not the original; a load the loader refuses reports why and leaves no generation; `reconcile` refusing a generation releases it and leaves no generation; `start` failing releases the generation (after `stop`) and reports the start's diagnostic; `Reload` runs `beforeReload`, `stop`, releases the old generation, sweeps, then loads a new copy; when the new build fails to load, the previous copy is loaded again and the failure is what `Reload` returns; when the new build loads and fails to start, the previous copy is restored; when the restore fails too, no generation is loaded and the error says so; `Sweep` deletes a retired generation's copy only once the loader reports it collected, and warns once while it is pending; `Reload` called from inside `beforeReload` is `NotInitialized` and the outer reload completes; `Release` removes every copy and the empty scratch directory; before `Configure` every operation does nothing and reports nothing loaded.
- [ ] `grep -n "Iris::" packages/FenrirScripting/src/GenerationLifecycle.hpp` finds only `CallResult` and `ComponentUnloadState`.
- [ ] The .NET reload tests in `ScriptingTests.cpp` pass unchanged.
- [ ] `check_tickets.py`, clang-format and the msvc-x64-debug preset pass.

## Comments

Filed from the generation lifecycle grilling (spec decisions 1, 2, 3, 8). Ticket 02 stacks on this one.
