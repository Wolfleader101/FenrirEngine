# What breaks when the engine is built as a subdirectory?

Type: task
Status: open
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
