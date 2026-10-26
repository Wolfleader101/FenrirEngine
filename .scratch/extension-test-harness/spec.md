Status: ready-for-agent

# Extension test harness: one harness for the packages' .NET-driven scripting tests

Origin: the architecture review of `dotnet-scripting` against `main` (2026-10-26), candidate 7 ("let natives be called from C++ tests, through one harness"), second half. The first half, `NativeCallHarness` for the natives' rules with no .NET, is `.scratch/native-guard/` (tickets 01 to 08). This spec covers what that left: the tests that still drive a package's scripting extension through a fixture script assembly, each with its own copy of the same harness.

The vocabulary is the repo's: *scripting extension*, *script assembly*, *Iris component* from `GLOSSARY.md`; *deepening a module*, *interface*, *implementation* from the codebase-design skill. No ADR: the change is to test code only, and nothing in it is hard to reverse.

## Problem statement

Six packages (assets, debug draw, input, physics, renderer, window) each keep a `Scripting/Tests/<Package>ScriptingApp.hpp`, 984 lines together, that are one interface with six implementations: read the fixture's path from `FENRIR_TEST_<PKG>_FIXTURE` (with the same stderr message), `Call<Result, Args...>(scripting, typeName, method, args...)` finding a type and static method in the loaded Iris component through `ScriptingProbe::Component` and invoking it with a `REQUIRE` at each step, `oneTick`, `noEntity` for C#'s `Entity.None`, and an App with the package installed and scripting running the fixture, stepped one frame at a time. A fix to the shared part (a clearer `INFO`, a new `REQUIRE`) is six edits, and the seventh package would copy a seventh.

What differs is small: which packages are installed and how (assets with its fixtures root, the renderer on the null backend, the window with none), the fixture's type-name constants, and a few helpers only one package has (assets' fixture shader and handle bits, debug draw's tessellated batches, input's feed, physics's `Place`, the renderer's probe reads).

## Solution

One header in FenrirScripting's tests, `Tests/ExtensionTestApp.hpp`, shared with package tests through `fenrir_scripting_tests_dir` as `HeadlessApp.hpp`, `ScriptingProbe.hpp` and `NativeCallHarness.hpp` are. It holds, in `Fenrir::Testing`:

- `FixtureFromEnvironment(variable)`: the fixture path CTest names, absolute, or empty with the message on stderr.
- `Call<Result, Args...>(scripting, typeName, method, args...)`, with the four `REQUIRE`s.
- `oneTick` and `noEntity`.
- `FixtureConfig(fixture, extensions)` and `LoggedWith(headless, level, text)`.
- `ExtensionTestApp<Packages>`: an App with no window, `Packages` installed into it, and scripting running the fixture with the given extensions, requiring that the scripts loaded. `Packages` is the package's own struct whose members are the installed packages, constructed from the App; they are the test app's members too. Members end in reverse order: scripting, then the packages, then the app. It steps frames (`Frame`, `Run`), plays changes back (`ApplyChanges`), and calls the fixture (`Call`).

Each package's `<Package>ScriptingApp.hpp` keeps only its own part, built on the shared header: its fixture variable, its extensions, its `Packages` struct, its type-name constants and the helpers only it has. The generated defaults tests (`Tests/Generated/`) are not edited: each package keeps the namespace, the struct name and the `scripting` member they name.

The behaviour each test asserts is unchanged: the same packages installed in the same order, the same extensions, the same `REQUIRE`s on loading, the same frames stepped.

## Out of scope

- `packages/FenrirWindow/Scripting/Tests/WindowScriptingTests.cpp` carries its own copy of `Call`, `FixtureFromEnvironment` and the config for an App with a hidden window; it is a test file, not a harness header, and is left for a later pass.
- `FixtureApp.hpp` (the built-in binding's defaults test, no extensions) stays as it is.
