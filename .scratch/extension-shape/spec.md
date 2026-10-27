Status: ready-for-agent

# Extension shape: a scripting extension's declarations, maker and build come from one owner

Origin: the second architecture review of `dotnet-scripting` (2026-10-27, after the 2026-10-26 review's eight candidates were built or closed), candidate 1 of seven, grilled to fifteen decisions the same day.

The vocabulary is the repo's: *scripting extension*, *extension state*, *native function*, *call context*, *binding*, *API description*, *built-in extension* from `GLOSSARY.md`; *module*, *interface*, *implementation*, *deep*, *shallow*, *seam*, *locality*, *leverage* from the codebase-design skill.

## Problem statement

Six packages (Assets, DebugDraw, Input, Physics, Renderer, Window) each carry a scripting extension, and each spells the extension's shape by hand in four places where one owner could.

**The natives' declarations.** Iris.Bindings writes each native function's C++ declaration into the generated registration `.cpp` (`src/Generated/<Pkg>Registration.cpp`), a file nothing can include. So the declarations exist nowhere a compiler can share: the registration has its copy, the definitions in `src/<Pkg>Scripting.cpp` are checked against it only at link, and five rules tests (`Tests/NoOps.cpp`, `NoInput.cpp`, `Rules.cpp`) repeat the declarations by hand, 83 lines in all, each under the comment "no header the engine sees declares them". The built-in extension does the same in `src/Bindings.cpp`, which declares `Generated::RegisterScripting` itself before calling it.

**The extension-making function.** Each package's public `X::ScriptingExtension()` is the same four statements: set `name` to the binding's name, `SetState` a fresh extension state, set `sharedAssemblies` to the contract's file name, call the generated `Register<Pkg>`. The two literals (`"FenrirInput"`, `"Fenrir.Input.dll"`) are the binding's name and namespace, which the generator already has. Physics spells its function `PhysicsScriptingExtension()`; the other five are `<Area>::ScriptingExtension()`.

**The build.** Each package's `Scripting/CMakeLists.txt` (69 to 77 lines) and `Scripting/Tests/CMakeLists.txt` (62 to 90 lines) are the same file with the package name substituted: the contract's custom command and target, a `fenrir_deploy_<pkg>_scripting` function, the library, the describe and generate lines, the fixture assembly's custom command, two test executables with the shared test headers listed by path, and the `add_test` lines. 879 lines across twelve files, of which the differences are the package name, which contracts a contract or fixture builds against, Window's lack of a selection of its own, DebugDraw's lack of a package description, and Physics's `--order decl` and compile-fail cases. These are the most-edited files of the last thirty PRs on the branch (56 touches), because every pipeline change lands in all twelve.

Each copy is shallow: its interface (the package name) is as wide as its implementation. A seventh package would copy the twelve files, the four statements and the declaration block again.

## Solution

### A generated natives header

Iris.Bindings writes one more file per binding with `[natives]`: the **natives header**, named by a new `[natives] header` key, by convention `src/Generated/<Pkg>Natives.hpp` (the built-in binding's is `packages/FenrirScripting/src/Generated/FenrirScriptingNatives.hpp`). It holds what the registration `.cpp` declares today: each native's declaration in its namespace, and the registration function (or the maker, below). The `[registration] includes` key, "what the natives' declarations name", moves to `[natives] includes`, since the declarations now live in the header; the header includes `FenrirScripting/ScriptingExtension.hpp` and those includes.

The header is private to the library: the natives take the extension state, a private type under `src/`, so a public header would publish it. Three kinds of file include it:

- the generated registration `.cpp`, which declares nothing itself;
- the natives' source (`src/<Pkg>Scripting.cpp`, `src/Bindings.cpp`), so every definition sits under its generated declaration and a signature that drifts from the committed description fails in the library's own compile, where today the registration's `&Ns::F` fails at the link of whatever first uses the library. describe-api's natives mode reads definitions only, so the included declarations change nothing it describes;
- the rules tests, through the `../src` include directory they already have for the state. Their hand-written declaration blocks are deleted.

### A generated extension maker

`[registration]` gains an optional key, `extension = "Fenrir::Generated::MakeInputExtension"`. When present, the generated `.cpp` defines

```cpp
Fenrir::ScriptingExtension MakeInputExtension(std::shared_ptr<Fenrir::InputScripting> state);
```

which sets `name` to the binding's name (the configuration file's stem, `FenrirInput`), calls `SetState(state)`, sets `sharedAssemblies` to `{"<namespace>.dll"}` (`Fenrir.Input.dll`), fills the four registration lists and the functions as `RegisterInput` does today, and returns the extension. The register function becomes file-local; the natives header declares the maker in its place. The state's type is the natives' extension state (their second parameter), which the natives description already records. The state is passed in, not constructed inside, so a state that later takes constructor arguments needs no generator change.

The six public functions become one line, `return Generated::MakeInputExtension(std::make_shared<InputScripting>());`, and Input's keeps its `resources.push_back(InputClaimsResource(input))` after it, since `by-hand` registrations stay the hand-written function's. Physics's function is renamed `Physics::ScriptingExtension()` so the six are spelled alike. When the key is absent, as in `FenrirScripting.binding.toml` (the built-in extension is Install's own, not a package's), today's `Register<Name>` is generated and declared in the header.

### One CMake module

`packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake`, included from FenrirScripting's `CMakeLists.txt` after `FenrirScriptingDeploy.cmake`, so its functions exist in every extension directory the root adds after FenrirScripting. Convention over parameters: each function derives every path from the package name (`<Pkg>`, as in `Input`) and the calling directory, and a package passes a keyword only where it deviates.

- `fenrir_scripting_contract(<Pkg> [USES <Pkg>...])`: builds `../Managed/Fenrir.<Pkg>/Fenrir.<Pkg>.csproj` into `${CMAKE_BINARY_DIR}/fenrir-<pkg>/contract`, sets the `FENRIR_<PKG>_CONTRACT_DIR` cache variable, adds the `Fenrir<Pkg>Contract` target depending on `FenrirScriptingContract` and each used contract, passes `-p:Fenrir<Used>ContractDir=` for each, and records the contract in a global property for the deploy.
- `fenrir_scripting_extension(<Pkg> SOURCES <files...> LINK <targets...> [NO_SELECTION] [DESCRIPTIONS <api.json...>])`: the `Fenrir<Pkg>Scripting` static library with `include` public and `src` private, linking `FenrirScripting` and `LINK`; it lists the generated files itself (`src/Generated/Fenrir<Pkg>Registration.cpp`, `src/Generated/Fenrir<Pkg>Natives.hpp`, `include/Fenrir<Pkg>Scripting/Generated/Fenrir<Pkg>Mappings.hpp`), so a package lists only its hand-written files; then `fenrir_describe_api(... api/Fenrir<Pkg>Scripting.api.toml)` unless `NO_SELECTION` (Window), `fenrir_describe_api_natives(... api/Fenrir<Pkg>.binding.toml)`, and `iris_generate_binding(Fenrir<Pkg> api/Fenrir<Pkg>.binding.toml <DESCRIPTIONS>)`, defaulting to `../api/Fenrir<Pkg>.api.json api/Fenrir<Pkg>Scripting.api.json` (DebugDraw passes its own description only). It adds `Tests` when `FENRIR_BUILD_TESTS`.
- `fenrir_script_fixture(<Pkg> [USES <Pkg>...])`: the fixture assembly `Fenrir.<Pkg>ScriptFixture.dll` from `Fixture/` and `Generated/*.cs`, into `${CMAKE_CURRENT_BINARY_DIR}/fixture`, with the contract properties for `<Pkg>` and each used package, and the `Fenrir<Pkg>ScriptFixture` target.
- `fenrir_scripting_tests(<Pkg> SOURCES <files...> [TEST_ARGS <args...>])`: the `Fenrir<Pkg>ScriptingTests` executable with the package's sources, `Generated/Fenrir<Pkg>DefaultsTests.cpp`, and FenrirScripting's `ExtensionTestApp.hpp`, `HeadlessApp.hpp` and `ScriptingProbe.hpp`; include directories for the calling directory and FenrirScripting's `Tests`; links `Fenrir<Pkg>Scripting` and Catch2; deploys scripting and the package's contract; depends on the fixture; `add_test(NAME Fenrir<Pkg>Scripting ...)` with `TEST_ARGS`, timeout 300, and `FENRIR_TEST_<PKG>_FIXTURE` in the environment.
- `fenrir_scripting_rules_tests(<Pkg> SOURCES <files...> [TEST_ARGS <args...>])`: the `Fenrir<Pkg>ScriptingRulesTests` executable with the package's sources, FenrirScripting's `HeadlessApp.hpp` and `NativeCallHarness.hpp`; include directories for FenrirScripting's `Tests`, FenrirScripting's `src` (the harness's leak, candidate 2 of the same review, now in one place rather than six) and the package's `src`; `add_test(NAME Fenrir<Pkg>ScriptingRules ...)` with `TEST_ARGS`, timeout 60.
- `fenrir_deploy_scripting(<target> [EXTENSIONS <Pkg>...])` in `FenrirScriptingDeploy.cmake` gains the list: each named contract is copied from the global property, as the six `fenrir_deploy_<pkg>_scripting` functions do today. Those six are deleted; the two example call sites (`examples/PhysicsDemo/CMakeLists.txt`, `examples/DemoScaffold/Tests/CMakeLists.txt`) change.

Each package's `Scripting/CMakeLists.txt` becomes its header comment and three calls; its `Tests/CMakeLists.txt` three. Physics keeps its `TEST_ARGS --order decl` on both test functions and its compile-fail block as its own lines after the calls. Timeouts are the module's and not configurable. FenrirScripting's own `CMakeLists.txt` and `Tests/CMakeLists.txt` stay off the module: its contract is the SDK, its fixture builds twice for the reload tests, and the built-in extension is not a package's.

### Out of scope

- The harness's include of FenrirScripting's `src` (candidate 2): the module encodes it once, and the ticket says so.
- Per-package state headers, `*ScriptingApp.hpp` test fixtures, and the natives themselves.
- Any change to describe-api.

## Decisions (from the grilling)

1. Three stacked tickets: header, CMake module, maker; the maker may be dropped if its ticket proves to delete too little.
2. The header is private, `src/Generated/<Pkg>Natives.hpp`, named by `[natives] header`; the registration and rules tests include it.
3. The natives' source includes the header, moving drift errors to the library's own compile.
4. The maker is generated, takes the state, folds `Register<Pkg>` in; Physics's function is renamed to match the five.
5. The module lives at `packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake`, included from FenrirScripting's CMakeLists.
6. Convention over parameters; `NO_SELECTION`, `DESCRIPTIONS`, `USES`, `TEST_ARGS` are the escape keywords; one `fenrir_deploy_scripting` with `EXTENSIONS` replaces the six deploy functions.
7. The header and maker are pinned by Iris.Bindings' tests; the module by the build and CI's regenerate-and-diff.
8. `extension` is an optional `[registration]` key; the built-in binding gets a header and no maker.
9. The header declares the maker (or register function) too; `[registration] includes` becomes `[natives] includes`.
10. The module lists the generated files itself.
11. FenrirScripting's own CMakeLists stay off the module.
12. The rules-test include of FenrirScripting's `src` is encoded once, in the module, until candidate 2.
13. All six packages migrate in the module's ticket; `TEST_ARGS` is the only test pass-through; timeouts are fixed.
14. ADR-0046 records the shape; `GLOSSARY.md`'s *scripting extension* entry gains one sentence; Iris.Bindings' README changes with the tool, in tickets 01 and 03.
15. Slug `extension-shape`; tickets `01-natives-header`, `02-cmake-module`, `03-extension-maker`.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | `[natives] header` and `includes` in Iris.Bindings; seven headers regenerated; registration, natives sources and rules tests include them; 83 hand-written declarations deleted; README | none |
| 02 | `FenrirScriptingExtension.cmake`, `EXTENSIONS` on the deploy; six packages' twelve CMakeLists onto the module; two example call sites | 01 |
| 03 | `[registration] extension` in Iris.Bindings; six makers generated; six public functions one line; Physics renamed; README | 01 |

01 and 03 share Iris.Bindings' `CppWriter.cs`, `BindingConfig.cs`, every `src/Generated/` and every binding toml; 02 lists the header 01 adds. 02 and 03 touch no file in common, so both are cut from 01's branch and target it, and may run side by side; whichever merges second rebases. Branches `extension-shape-01-natives-header`, `-02-cmake-module`, `-03-extension-maker`.

## Done looks like

- `grep -rn "const CallContext&" packages/*/Scripting/Tests/*.cpp` finds no declaration: every rules test includes `Generated/Fenrir<Pkg>Natives.hpp`.
- No `src/Generated/*Registration.cpp` declares a function; each includes its natives header. `src/Bindings.cpp` declares no `Generated::` function.
- Six public `<Area>::ScriptingExtension()` functions, one line each plus Input's resource; no `PhysicsScriptingExtension`.
- Each `packages/*/Scripting/CMakeLists.txt` is under 25 lines and each `Tests/CMakeLists.txt` under 20, Physics's compile-fail block excepted; `grep -rn "function(fenrir_deploy_" packages` finds only `fenrir_deploy_scripting`.
- `fenrir-describe-api` and `fenrir-generate-bindings` are no-ops after each committed regeneration.
- The msvc-x64-debug preset builds and tests locally; the other presets are CI's.
