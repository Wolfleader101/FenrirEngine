# What breaks when the engine is built as a subdirectory?

Type: task
Status: resolved
Blocked by: none
Settles: [A game cannot build its scripts or copy its assets without demo code](02-building-a-game-against-the-engine.md)

## Question

Build a throwaway outside project (in a scratch folder or on a `prototype/` branch, never merged) that adds the engine with `add_subdirectory` and `FENRIR_BUILD_EXAMPLES` off, the way [How does the game build against the engine?](../../flightless-frenzy/issues/03-how-the-game-builds-against-the-engine.md) settled. It compiles one material shader with `fenrir_add_shaders`, links `FenrirApp`, `FenrirRenderer`, `FenrirPhysics`, `FenrirUI` and `FenrirScripting`, and builds one C# script project by hand.

Record every place the engine's CMake assumes it is the top-level project, with file and line: the known ones are `FENRIR_SHADER_INCLUDE_DIR` as a directory-scoped variable (`cmake/FenrirShaders.cmake:20`) and `CMAKE_SOURCE_DIR` in `packages/FenrirAnimation/CMakeLists.txt:40` and `packages/FenrirSkeleton/CMakeLists.txt:36`. Also record what a C# script project needs from the build to find the contracts and scripting's run-time files, and what a parent project must set (options, presets, the .NET SDK, submodule transport).

This is a task, not a decision: nothing is fixed here. The answer lists the breakages and the facts [What CMake functions does the engine give a game?](17-what-cmake-functions-a-game-gets.md) decides on.

## Facts (checked 2026-11-19)

- `cmake/FenrirShaders.cmake:20-21` sets `FENRIR_SHADER_INCLUDE_DIR` from `CMAKE_CURRENT_LIST_DIR`, so the path is right, but it is a plain variable in the engine's directory scope; whether `fenrir_add_shaders` called from the game's directory sees it is what the task checks (used at :65).
- `CMAKE_SOURCE_DIR`: `packages/FenrirAnimation/CMakeLists.txt:40` and `packages/FenrirSkeleton/CMakeLists.txt:36` (`cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} ...)`), `packages/Iris/CMakeLists.txt:4` (a top-level check).
- `CMAKE_BINARY_DIR` (it becomes the game's build directory): `cmake/FenrirCatch2.cmake:19`, `cmake/FenrirDescribeApi.cmake:16,22,70`, `packages/FenrirScripting/CMakeLists.txt:9`, `packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake:101`, `packages/FenrirUI/Managed/CMakeLists.txt:19,24`, `packages/Iris/cmake/Bridge.cmake:14`, `packages/Iris/cmake/IrisBindings.cmake:17,26`; test-only uses in `packages/Iris/Examples/Hosting` and `packages/Iris/Tests`.
- `PROJECT_SOURCE_DIR`: `cmake/Tests/CMakeLists.txt:7`. `packages/FenrirRenderer/Tests/CMakeLists.txt:53` reaches `examples/assets` (tests only).

## Answer

Done 2026-11-19 on a throwaway outside project: `prototype/outside-game/` on the local branch `prototype/engine-as-subdirectory` (23ace03c), never merged; its `FINDINGS.md` holds every finding with file and line. Built with `msvc-x64-debug`, scripting on, examples and tests off. With two workarounds inside the outside project, its executable links and runs headless, and its C# script loads and logs, exiting 0.

**What breaks:**

1. **`IRIS_BRIDGE_DIR` does not reach the parent, so configure fails** for any game with scripts (not on the ticket's list). `packages/Iris/cmake/Bridge.cmake:14` sets it as a plain variable and `packages/Iris/CMakeLists.txt:31-34` passes it up one scope only, to the engine root; `iris_deploy` then stops at `packages/Iris/cmake/IrisDeploy.cmake:8-9` when the game calls `fenrir_deploy_scripting`. Worked around by setting it again in the game's scope.
2. **`FENRIR_SHADER_INCLUDE_DIR` is empty in the parent's scope, so a game's shader fails to build** (`cmake/FenrirShaders.cmake:20`, used at :65): the command ends in a bare `--include`. Worked around the same way.
3. **The nested binary tree pushes a glslang object path to 261 characters** (`OSDependent/.../stub.cpp.obj`, C1083), so building everything exits 1 though the game's own targets build. Only short directory names helped.
4. **The game must set C++20 itself:** the engine sets `CMAKE_CXX_STANDARD` only in its own scope (`CMakeLists.txt:5-6`). A target linking only `FenrirApp` fails on `std::string_view`; one linking `FenrirScripting` gets C++20 by accident through Iris's public `cxx_std_20` (`packages/Iris/Native/CMakeLists.txt:54`).

**Works, differently:**

- The `cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR})` checks (FenrirSkeleton:36, FenrirAnimation:40) queue on the game's directory and give the right result; Iris's top-level check (`packages/Iris/CMakeLists.txt:4`) answers the same either way.
- Every `CMAKE_BINARY_DIR` output (`describe-api/`, `iris/bridge`, `fenrir-scripting/`, `fenrir-<pkg>/contract`, Catch2 with tests) lands in the game's build root; nothing breaks, since readers use cache variables or global properties, but names could clash. glslang's `include/` and CPack files from glm, FreeType and SDL land there too.
- The game's sources are missing from `compile_commands.json` and build at `/W3`: those settings are scoped to the engine's directory.
- Engine functions and cache variables reach the game.

**C# scripts:** the project imports the built SDK's `$(FenrirScriptingSdk)Fenrir.Scripting.props` (the in-repo props are out of reach from outside), needs a `HintPath` reference and a `-p:Fenrir<Pkg>ContractDir=` per contract, and `-p:BaseIntermediateOutputPath` to keep `obj/` out of the game's sources; an editor sees no contracts. Deployed beside the executable: `nethost.dll`, the `Iris.Bridge` files, `Fenrir.Scripting.dll`, `Fenrir.Math.dll`, each contract (`Fenrir.Physics.dll`), and `scripts/<name>.Scripts.dll` with its `.deps.json`. A missing contract makes .NET fail to start quietly and the app runs on without scripts.

**What a parent sets:** the options before `add_subdirectory` (`FENRIR_BUILD_EXAMPLES` defaults on), the .NET 10 SDK, its own presets run through `scripts\msvc.ps1`, and submodules fetched with `protocol.file.allow=always` (HarfBuzz also needed `core.longpaths=true`).
