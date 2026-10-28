Status: ready-for-agent

# Test support: FenrirScripting's tests reach it through its interface, and its test files are sized by theme

Origin: the second architecture review of `dotnet-scripting` (2026-10-27), candidate 2 (NativeCallHarness stops exposing FenrirScripting's implementation) rolled up with the rest of candidate 7's small deletions (the 5655-line test file, the physics drift cases, a Rules test for the query module, the component-loader rename), grilled to eight decisions on 2026-10-28. The review's other candidates are built or being grilled.

The vocabulary is the repo's: *native function*, *call context*, *extension state*, *script assembly*, *Iris component*, *admission*, *generation* from `GLOSSARY.md`; *module*, *interface*, *implementation*, *seam*, *adapter*, *locality* from the codebase-design skill.

## Problem statement

**The harness leaks the implementation.** `packages/FenrirScripting/Tests/NativeCallHarness.hpp` is the test seam for native functions: a call context over a headless app and a change buffer, with no .NET. It includes FenrirScripting's private `src/ChangeBuffer.hpp` and `src/ComponentRegistry.hpp` and returns `Detail::ApplyResult`, so every executable that uses it is given FenrirScripting's `src` directory: the six package rules tests (through `fenrir_scripting_rules_tests` in `cmake/FenrirScriptingExtension.cmake`) and FenrirScripting's own two. The module also lists the four shared test headers (`HeadlessApp.hpp`, `ScriptingProbe.hpp`, `ExtensionTestApp.hpp`, `NativeCallHarness.hpp`) by path for every test executable. A package's tests see FenrirScripting's implementation because a header they include does.

**One file holds every scripting test.** `Tests/ScriptingTests.cpp` is 5655 lines and 115 cases, all tagged `[scripting]`, run with `--order decl`. Its first 630 lines are a test-only scripting extension and the `CallIn` helpers every case uses. A reader looking for the admission tests, the query tests or the reload tests searches the one file.

**Four physics cases re-prove a rule FenrirScripting pins.** `PhysicsScriptingTests.cpp` has four cases ("...a drifted one fails the load naming the field") that each build a hand-written `Drifted*` struct and `Iris::StructMapping` to show that a mirror whose layout differs from C#'s fails the load. FenrirScripting's own tests pin that rule for a component, an event, a shared struct and a resource; physics's four add nothing but stand-ins to maintain.

**The query module has no test of its own.** `src/Queries.hpp` exposes `CountQueryRows` and `FillQueryRows` behind a two-function interface, and only two C# cases reach them.

**`ComponentLoader` uses the word the glossary reserves.** The glossary says engine code says *component* for ECS data and *Iris component* for a loaded assembly, and names what this loader loads a *script assembly*. `ComponentLoader`, `LoadedComponent` and `FakeComponentLoader` say the other thing.

## Solution

### `FenrirScriptingTestSupport`

A static library under `packages/FenrirScripting/Tests/Support/`, added under `FENRIR_BUILD_TESTS`, whose public include directory holds the four shared headers moved from `Tests/`: `HeadlessApp.hpp`, `ScriptingProbe.hpp`, `ExtensionTestApp.hpp`, `NativeCallHarness.hpp`. The harness keeps its interface (`GetApp`, `Logger`, `Context`, `ApplyChanges`, `PendingChanges`) over a pimpl: `NativeCallHarness.cpp` in the library holds `Impl` with the headless app, the registry and the change buffer, and is the one file that includes the private headers. `ApplyChanges` returns `ApplyOutcome { std::size_t applied; std::size_t skipped; }`, declared in the header, in place of `Detail::ApplyResult`. The library links `FenrirScripting` and `Catch2::Catch2` (the probe and the headless app's recording logger need no Catch2; if nothing does, it links FenrirScripting alone) and adds FenrirScripting's `src` to its own private include path, so the leak is confined to one target inside FenrirScripting's tree. The friend declaration for `Testing::NativeCallHarness` in `ScriptingExtension.hpp` stays.

`fenrir_scripting_tests` and `fenrir_scripting_rules_tests` link the library, drop the four header paths and the `tests_dir` include, and the rules function drops `${tests_dir}/../src`; a package's rules test keeps `${CMAKE_CURRENT_SOURCE_DIR}/../src` for its extension state. FenrirScripting's own `Tests/CMakeLists.txt` links the library for both executables; its Rules executable keeps `../src`, since those tests exercise the private modules by design. ADR-0046's consequence line about the leak stays as the record it is.

### The test file split

`ScriptingTests.cpp` becomes two headers and about eight sources under `Tests/`, the cases moved unchanged:

- `TestExtension.hpp`: the test-only extension (the `TestNative` and `TestExtension` namespaces, lines 37 to 630 today) and `FixtureCalls.hpp`: the `CallIn` helpers and the fixture type names.
- Sources named by theme, each under 1000 lines, as the case titles group them: `HostingTests.cpp` (load, missing contract, clock, shutdown, Iris messages), `ReloadTests.cpp` (reload, the watcher, changed layouts across generations), `AdmissionTests.cpp` (mismatched and duplicate components, events, shared structs, resources; names an extension cannot take), `ScriptComponentTests.cpp` (script components, refs, the hierarchy), `QueryTests.cpp`, `EventTests.cpp`, `ResourceTests.cpp`, `NativeTests.cpp` (strings, warnings, stateless natives, the not-running answer), `TaggedUnionTests.cpp`. The builder of the ticket settles the exact set from the titles; the names above are the shape.

The test runs with `--order decl`, and Catch2 orders cases across files by file order on the command line, then declaration. Any case that shares a scripting install with an earlier one, or otherwise depends on it, moves into the same file in the same relative order; the ticket's PR names every such pair it found. No case is renamed or edited; the tag stays `[scripting]`; `add_test` stays as it is.

### Small deletions

- **Physics drift cases.** In `PhysicsScriptingTests.cpp` the four cases at (today) lines 641, 930, 1319 and 1561 lose their drift sections, the `Drifted*` structs and their mapping specialisations; a section that proves something physics-specific (the vehicle case's "shared with C#" half) stays under a title that says what remains. If nothing else needed the physics tests' `../src` include (the module's `fenrir_scripting_tests` does not add it; check the package's own call), it goes.
- **A Rules test for the query module.** `Tests/Rules/QueriesTests.cpp` pins `CountQueryRows` and `FillQueryRows` through `Queries.hpp`: every listed component matches; an unregistered id gives a negative result from both; capacity truncates the rows and the return says how many were written; a row is the entity value then the component addresses in id order; an empty id list; a registry whose generation changed between calls. Six cases, in the Rules executable.
- **The rename.** `ComponentLoader` → `ScriptAssemblyLoader`, `LoadedComponent` → `LoadedScriptAssembly`, `FakeComponentLoader` → `FakeScriptAssemblyLoader`, the two files renamed with them, and every mention in `GenerationLifecycle.hpp/.cpp`, `Scripting.cpp`, `README.md`, both CMake lists and the Rules tests. Method names (`Load`, `Release`, `WaitForCollection`) do not change.

### Out of scope

- FenrirScripting's own Rules tests' include of `src`: they test private modules through their interfaces, which is what the Rules executable is for.
- Any change to what the harness does; only where its implementation lives.
- The SystemHost seam (its own spec, under grilling), which adds Rules tests of its own and edits `Scripting.cpp`; this spec's rename touches `Scripting.cpp` too, so whichever lands second rebases.

## Decisions (from the grilling)

1. One spec, three stacked tickets (harness library, test file split, small deletions), since all three edit `Tests/CMakeLists.txt`.
2. `FenrirScriptingTestSupport` under `Tests/Support/` owns the four shared headers and the harness's implementation behind a pimpl; `ApplyOutcome` replaces the private result type; the library alone sees `src`.
3. The module links the library and stops listing headers and FenrirScripting's `src`; FenrirScripting's own Rules tests keep `src`.
4. `ScriptingTests.cpp` splits by theme into files under 1000 lines with two helper headers, declaration order preserved for dependent cases.
5. The four physics drift cases lose their drift sections and stand-ins; physics-specific sections stay.
6. Six Rules cases for the query module.
7. `ComponentLoader` becomes `ScriptAssemblyLoader` (with `LoadedScriptAssembly`, `FakeScriptAssemblyLoader`).
8. No ADR, no glossary change.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | `Tests/Support/` library, the harness pimpl and `ApplyOutcome`, the module and FenrirScripting's test CMake onto it | none |
| 02 | `ScriptingTests.cpp` split into helper headers and themed sources | 01 |
| 03 | Physics drift sections, `QueriesTests.cpp`, the `ScriptAssemblyLoader` rename | 02 |

Branches `test-support-01-support-library`, `-02-test-split`, `-03-small-deletions`; each cut from the previous one, each PR targeting it.

## Done looks like

- `grep -rn "ChangeBuffer.hpp\|ComponentRegistry.hpp\|ApplyResult" packages/FenrirScripting/Tests/Support/*.hpp` finds nothing; `grep -n "tests_dir}/../src" packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake` finds nothing.
- No file under `packages/FenrirScripting/Tests/` is over 1000 lines; `ScriptingTests.cpp` does not exist; `ctest -N` lists the same test names as before.
- `grep -c "Drifted" packages/FenrirPhysics/Scripting/Tests/PhysicsScriptingTests.cpp` is 0; `Tests/Rules/QueriesTests.cpp` exists with six cases; `grep -rn "ComponentLoader\|LoadedComponent" packages/FenrirScripting` finds nothing.
- Every scripting test passes under the msvc-x64-debug preset after each ticket; the other presets are CI's.
