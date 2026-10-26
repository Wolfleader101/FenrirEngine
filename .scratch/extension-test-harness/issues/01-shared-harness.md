# 01: One ExtensionTestApp for the six packages' .NET-driven scripting tests

**What to build:** `packages/FenrirScripting/Tests/ExtensionTestApp.hpp` (spec: "Solution") and the six package headers shrunk onto it.

- **`ExtensionTestApp.hpp`**: `Fenrir::Testing::FixtureFromEnvironment`, `Call<Result, Args...>`, `oneTick`, `noEntity`, `FixtureConfig`, `LoggedWith` and `ExtensionTestApp<Packages>` (an App with no window, `Packages` installed, scripting running the fixture with the given extensions; `Frame`, `Run`, `ApplyChanges`, `Call`, `Entities`, `Get`).
- **Each `packages/<Package>/Scripting/Tests/<Package>ScriptingApp.hpp`**: its fixture variable, its extensions, its `Packages` struct (assets with the fixtures root, debug drawing with the Render-phase capture, input, debug drawing and physics with interpolation off, the renderer on the null backend over assets, none for the window), its type-name constants and the helpers only it has. The namespace, struct name and `scripting` member the generated defaults tests name stay.
- **Order**: assets, debug draw, input, physics and window first; the renderer last, after native guard ticket 08 (its renderer port, which edits `packages/FenrirRenderer/Scripting/Tests/`) has merged. If it has not, the PR lands without the renderer and names it as the follow-up.
- **Tests**: no test file changes beyond includes and names; no `Generated/` file changes; no `packages/*/Scripting/src/` changes.

**Blocked by:** none

**Touches:** `packages/FenrirScripting/Tests/ExtensionTestApp.hpp`, `packages/FenrirAssets/Scripting/Tests/AssetsScriptingApp.hpp`, `packages/FenrirDebugDraw/Scripting/Tests/DebugDrawScriptingApp.hpp`, `packages/FenrirInput/Scripting/Tests/InputScriptingApp.hpp`, `packages/FenrirPhysics/Scripting/Tests/PhysicsScriptingApp.hpp`, `packages/FenrirRenderer/Scripting/Tests/RendererScriptingApp.hpp`, `packages/FenrirWindow/Scripting/Tests/WindowScriptingApp.hpp`, `packages/*/Scripting/Tests/CMakeLists.txt`

**Status:** ready-for-agent

- [ ] Every scripting test target (`Fenrir<Package>Scripting`, `-NoOps`, `-Display` and the rules executables) passes on the msvc-x64-debug preset, asserting what it did before.
- [ ] The six harness headers hold only their package-specific lines: no `FromEnvironment`, no `Call`, no `FixtureConfig`, no `oneTick` definition of their own.
- [ ] `check_tickets.py` and clang-format pass.

## Comments

Filed from architecture-review candidate 7's second half, after native guard tickets 01 to 07 merged.
