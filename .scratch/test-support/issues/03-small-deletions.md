# 03: The physics drift sections, a Rules test for queries, and `ScriptAssemblyLoader`

**What to build:** three small changes the review found (spec: "Small deletions").

- **Physics drift sections** (`packages/FenrirPhysics/Scripting/Tests/PhysicsScriptingTests.cpp`): the four cases titled "...a drifted one fails the load naming the field" (today at lines 641, 930, 1319, 1561) lose the sections that register a `Drifted*` stand-in and assert the load fails, and the `Drifted*` structs and their `Iris::StructMapping` specialisations go. A section that proves something else (the vehicle case's "shared with C#" half) stays, under a title saying what remains; a case left with no section is deleted. If the physics `fenrir_scripting_tests` call or CMake gave the tests `../src` only for the stand-ins' mappings, drop it.
- **`packages/FenrirScripting/Tests/Rules/QueriesTests.cpp`** (new, in `FenrirScriptingRulesTests`): six cases through `Queries.hpp` on an `EntityList` and a `ComponentRegistry` the test fills: every listed component matches and only those rows are counted; an unregistered id makes `CountQueryRows` return -1 and `FillQueryRows` negative; capacity truncates and the return is the rows written; a row is the entity value then the component addresses in id order (compare with `EntityList`'s own pointers); an empty id list; a registry whose generation changed between a count and a fill. Follow `RegistryTableTests.cpp`'s style.
- **The rename**: `src/ComponentLoader.hpp` → `src/ScriptAssemblyLoader.hpp` with `ComponentLoader` → `ScriptAssemblyLoader` and `LoadedComponent` → `LoadedScriptAssembly`; `Tests/Rules/FakeComponentLoader.hpp` → `FakeScriptAssemblyLoader.hpp` with `FakeComponentLoader` → `FakeScriptAssemblyLoader`; every mention in `GenerationLifecycle.hpp`, `GenerationLifecycle.cpp`, `Scripting.cpp`, `GenerationLifecycleTests.cpp`, `packages/FenrirScripting/CMakeLists.txt`, `Tests/CMakeLists.txt` and `README.md` follows (`git mv`, then a whole-word replace, then read each hit). Doc comments that said "component" for the loaded assembly say "script assembly". Method names do not change.

**Blocked by:** 02

**Touches:** `packages/FenrirPhysics/Scripting/Tests/PhysicsScriptingTests.cpp`, `packages/FenrirPhysics/Scripting/Tests/CMakeLists.txt`, `packages/FenrirScripting/Tests/Rules/`, `packages/FenrirScripting/Tests/CMakeLists.txt`, `packages/FenrirScripting/src/ComponentLoader.hpp`, `packages/FenrirScripting/src/ScriptAssemblyLoader.hpp`, `packages/FenrirScripting/src/GenerationLifecycle.hpp`, `packages/FenrirScripting/src/GenerationLifecycle.cpp`, `packages/FenrirScripting/src/Scripting.cpp`, `packages/FenrirScripting/CMakeLists.txt`, `packages/FenrirScripting/README.md`

**Status:** claimed

- [x] `grep -c "Drifted" packages/FenrirPhysics/Scripting/Tests/PhysicsScriptingTests.cpp` is 0 and `FenrirPhysicsScripting` passes.
- [x] `QueriesTests.cpp` has six cases and `FenrirScriptingRules` passes.
- [x] `grep -rn "ComponentLoader\|LoadedComponent" packages/FenrirScripting docs .scratch` finds nothing outside git history; the msvc-x64-debug preset builds and every scripting test passes.
- [x] `check_tickets.py` and clang-format pass.

## Comments

Filed from the test-support grilling (spec decisions 5, 6, 7). Cut from `test-support-02-test-split`. The SystemHost seam spec (under grilling) also edits `Scripting.cpp`; whichever lands second rebases.
