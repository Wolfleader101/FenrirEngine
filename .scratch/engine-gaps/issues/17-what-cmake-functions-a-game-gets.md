# What CMake functions does the engine give a game?

Type: grilling
Status: open
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
