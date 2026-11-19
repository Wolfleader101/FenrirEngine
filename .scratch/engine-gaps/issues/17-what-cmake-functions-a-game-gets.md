# What CMake functions does the engine give a game?

Type: grilling
Status: resolved
Blocked by: 16
Settles: [A game cannot build its scripts or copy its assets without demo code](02-building-a-game-against-the-engine.md)

## Question

With [What breaks when the engine is built as a subdirectory?](16-build-the-engine-as-a-subdirectory.md) known, design the engine's public build surface for a game:

- **The script-project function**: its name (for example `fenrir_add_scripts`), its arguments (the `.csproj`, the contracts it compiles against, the target it deploys beside), what it deploys (the script assembly, the contracts, scripting's run-time files, Razor support when the project has `.razor` files), and how `fenrir_add_demo_scripts` (`examples/DemoScaffold/cmake/FenrirDemoApp.cmake:97-163`) becomes a thin caller of it with the `Fenrir.Demo` contract and demo Razor files passed in.
- **The asset-copy function**: copying one or more folders beside the executable, post-build or as a dependency so edits are picked up; how `fenrir_add_demo_app` (same file, lines 50-65) becomes a caller of it.
- **Where they live** (`cmake/`, included by the root `CMakeLists.txt` so a parent sees them after `add_subdirectory`).
- **The top-level fixes** for each breakage the task found: cache or target properties in place of directory variables, `PROJECT_SOURCE_DIR` or `CMAKE_CURRENT_*` in place of `CMAKE_SOURCE_DIR`.
- **How it stays working:** a minimal outside project kept in the repo (for example under `tests/`) that CI configures and builds, or none, given that CI builds only the presets in `build-and-test.yml` today.
- **Documentation:** a README section on how a game consumes the engine.

ADR-0071 makes this the "game asks" moment for the build helpers only; the demo UI kit stays in the scaffold. Outcome: a spec-ready design.

## Answer

Grilled 2026-11-19 in round 2b of [Engine gaps for Flightless Frenzy](../map.md); every recommendation was accepted. It rests on [What breaks when the engine is built as a subdirectory?](16-build-the-engine-as-a-subdirectory.md). **Spec-ready.**

- **Two functions in a new `cmake/FenrirGame.cmake`,** included by the engine's root so a parent has them after `add_subdirectory`:
  - `fenrir_add_scripts(<target> PROJECT <csproj> CONTRACTS <list>)` builds the game's C# project and deploys beside `<target>` the script assembly (under `scripts/`, with its `.deps.json`), the named contracts, scripting's run-time files (`nethost`, the Iris bridge, `Fenrir.Scripting`, `Fenrir.Math`), and Razor support when the project has `.razor` files.
  - `fenrir_copy_assets(<target> DIRECTORIES <dirs> DESTINATION <dir>)` copies asset folders beside `<target>` so edits are picked up on the next build.
  - `fenrir_add_demo_scripts` and `fenrir_add_demo_app` (`examples/DemoScaffold/cmake/FenrirDemoApp.cmake`) become thin callers that pass the `Fenrir.Demo` contract, the demo Razor files and the shared demo assets.
- **Contracts for editors:** `fenrir_add_scripts` writes a props file into the build directory listing each contract's path and the scripting SDK's props, so the game's `.csproj` imports it with one documented line and an editor sees the contracts after the first configure. It also keeps `obj/` out of the game's sources.
- **Scope fixes:**
  - `IRIS_BRIDGE_DIR` and the shader include path are found from the function's own file (`CMAKE_CURRENT_FUNCTION_LIST_DIR`) or a cache variable, not a directory variable (`packages/Iris/cmake/Bridge.cmake:14`, `packages/Iris/CMakeLists.txt:31-34`, `cmake/FenrirShaders.cmake:20`).
  - C++20 goes on the engine's targets as a `PUBLIC` compile feature, so every consumer gets it, not only those that link Iris.
  - The engine's `CMAKE_BINARY_DIR` outputs move under its own `PROJECT_BINARY_DIR`, so they cannot clash with a game's.
  - Warnings and `compile_commands.json` stay the game's own settings.
- **A missing contract is an error:** when .NET fails to start because a contract is missing, scripting logs an error naming it rather than running on silently.
- **Path length:** no engine change. The README tells a game to give the engine a short binary directory (`add_subdirectory(<engine> f)`); a deep worktree uses `scripts\short-path.ps1` as today.
- **Keeping it working:** a minimal outside project in `tests/outside-project/` (a shader, a link to every package, a C# script), run by hand and before a game's first ticket; no CI job for now, since one would build the whole engine twice on every PR.
- **README:** a "Building a game against the engine" section: the options set before `add_subdirectory` (`FENRIR_BUILD_EXAMPLES` off), C++20, the .NET 10 SDK, the short binary directory, submodules with `protocol.file.allow=always` (and `core.longpaths` for HarfBuzz), and the two functions.
