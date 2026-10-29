# 05: seven test apps of one shape in `Fenrir::Testing`, and `FixtureApp` deleted

**What to build:** the built-in binding's test app is written as the six packages' are, all seven sit in one namespace, and the seven `[tests]` sections differ only in the package name (spec: "Seven test apps of one shape, in one namespace").

- **`packages/FenrirScripting/Tests/ScriptingApp.hpp`** (new; `FixtureApp.hpp` deleted): `Fenrir::Testing::ScriptingApp : ExtensionTestApp<NoPackages>`, constructed as `ExtensionTestApp(FixtureFromEnvironment("FENRIR_TEST_FIXTURE"), {})`, with a header comment in the packages' wording. `Tests/CMakeLists.txt` lists it in place of `FixtureApp.hpp`. `FenrirScripting.binding.toml` and `FenrirMath.binding.toml` `[tests]` read `includes = ["ScriptingApp.hpp"]` and `setup = "Fenrir::Testing::ScriptingApp app;"` (Math's drops `ScriptingProbe.hpp` and its `component` line if the defaults file's already give them; it states them today because it does not name the defaults file, so keep what it needs).
- **The six `<Pkg>ScriptingApp.hpp`**: `namespace <Pkg>ScriptingTests` becomes `namespace Fenrir::Testing`; the `using Fenrir::Testing::...` re-exports and `Fenrir::Testing::` / `Fenrir::` qualifications inside go; helper constants and functions (`twoTicks`, `mainWindow`, the fixture type names, `WarningsWith`, the renderer's probe helpers) move with the app, renamed only where two packages' helpers would now collide in one namespace (a collision is a compile error in no executable, since each package's tests are their own executable, but name them apart anyway so a reader is not misled: prefix with the package where needed, `inputMainWindow` is not needed if only input has one). Every test source that named `<Pkg>ScriptingTests::` or opened that namespace follows (`grep -rln "ScriptingTests::\|namespace [A-Za-z]*ScriptingTests" packages/*/Scripting/Tests`).
- **The five package tomls** with `[tests]`: `setup = "Fenrir::Testing::<Pkg>ScriptingApp app;"`; nothing else changes. Regenerate (`fenrir-generate-bindings`): the generated tests change in their setup line only.

**Blocked by:** 01

**Touches:** `packages/FenrirScripting/Tests/ScriptingApp.hpp`, `packages/FenrirScripting/Tests/FixtureApp.hpp`, `packages/FenrirScripting/Tests/CMakeLists.txt`, `packages/FenrirScripting/api/FenrirScripting.binding.toml`, `packages/FenrirScripting/api/FenrirMath.binding.toml`, `packages/*/Scripting/Tests/*ScriptingApp.hpp`, `packages/*/Scripting/Tests/*.cpp` (the .NET-driven test sources, not the rules tests ticket 04 edits), `packages/*/Scripting/api/*.binding.toml`, `packages/*/Scripting/Tests/Generated/*DefaultsTests.cpp`, `packages/FenrirScripting/Tests/Generated/*DefaultsTests.cpp`

**Status:** resolved

- [x] `grep -rn "^namespace" packages/*/Scripting/Tests/*ScriptingApp.hpp packages/FenrirScripting/Tests/ScriptingApp.hpp` finds only `Fenrir::Testing`; `grep -rn "ScriptingTests::" packages` finds nothing; `FixtureApp.hpp` does not exist.
- [x] Every `[tests] setup` reads `Fenrir::Testing::<Name>ScriptingApp app;`; `fenrir-generate-bindings` re-run changes nothing further.
- [x] `ctest -N` lists the same test names; every `*Scripting` CTest passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Filed from the rules-harness grilling (spec decisions 7, 8, 9). Cut from ticket 01's branch, side by side with the 02 to 04 stack: its Touches are disjoint from theirs (the rules tests are 04's, the .NET-driven sources and apps are this ticket's).

Resolved: PR #228 merged into the spec's branch, and the spec #218 squash-merged into `dotnet-scripting` carrying it.
