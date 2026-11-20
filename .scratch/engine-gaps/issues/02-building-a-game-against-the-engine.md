# A game cannot build its scripts or copy its assets without demo code

Status: ready-for-agent
Blocked by: none

## What is missing

A game is a CMake project of its own that adds the engine with `add_subdirectory` (a submodule, or a path), with `FENRIR_BUILD_EXAMPLES` off. Install, export and packaging are **not** asked for: that route was chosen in [How does the game build against the engine?](../../flightless-frenzy/issues/03-how-the-game-builds-against-the-engine.md). Three things stand in its way:

1. **No engine function builds a game's C# script project.** `fenrir_add_demo_scripts` (`examples/DemoScaffold/cmake/FenrirDemoApp.cmake:97-163`) builds `<dir>/<name>.Scripts.csproj`, passes each built contract's directory, and deploys the result with the contracts and scripting's run-time files beside the executable. It is demo code: it always passes the `Fenrir.Demo` contract and the demo Razor files, and it does not exist with examples off. The engine needs the same thing without the demo parts.
2. **No engine function copies an asset folder beside the executable.** The post-build `copy_directory` into `<exe dir>/assets` is inside `fenrir_add_demo_app` (same file, lines 50-65), mixed with the shared demo assets.
3. **The engine's CMake has not been run as a subdirectory of another project.** Known from reading, not tried: `fenrir_add_shaders` reads `FENRIR_SHADER_INCLUDE_DIR`, a plain variable set in the engine's own directory scope (`cmake/FenrirShaders.cmake:20`), which a parent project calling the function will not see. `packages/FenrirAnimation/CMakeLists.txt:40` and `packages/FenrirSkeleton/CMakeLists.txt:36` defer a check to `CMAKE_SOURCE_DIR`, which becomes the game's directory. There may be more; the test is a small outside project that adds the engine, compiles a shader, builds a script project and links.

Also: no README says how a game consumes the engine.

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)), which is a project of its own under `games/FlightlessFrenzy/` and uses nothing from `examples/`.

## Does anything plan it?

No. ADR-0071 leaves lifting scaffold pieces into the engine as "a later decision, taken only once a game asks for it"; this is the game asking, for the build helpers only. The game writes its own start-up code and wants none of the demo UI kit.

## Does it block the game?

Yes, its first ticket: the game cannot configure against the engine until item 3 holds, and cannot build scripts (if it has C#) or find its assets without items 1 and 2. It was agreed these are engine work done ahead of the game's first ticket, not copies written inside the game.

## Comments

- 2026-11-19, charting [Engine gaps for Flightless Frenzy](../map.md): settled by [What breaks when the engine is built as a subdirectory?](16-build-the-engine-as-a-subdirectory.md), [What CMake functions does the engine give a game?](17-what-cmake-functions-a-game-gets.md).
- 2026-11-19, round 2 of [Engine gaps for Flightless Frenzy](../map.md): spec-ready in [What CMake functions does the engine give a game?](17-what-cmake-functions-a-game-gets.md), from what [What breaks when the engine is built as a subdirectory?](16-build-the-engine-as-a-subdirectory.md) found.
- 2026-11-19: the spec is written, [Building a game against the engine](../../fenrir-game-build/spec.md), `ready-for-agent` for `/to-tickets`.
- 2026-11-19: tickets cut, the tickets of [Building a game against the engine](../../fenrir-game-build/spec.md), `01` to `05` (stack 01, 02, 03, 05; 04 beside).
