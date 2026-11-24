# 01: The engine builds as a subdirectory

Spec: [Building a game against the engine](../spec.md).

**What to build:** A game project that adds the engine with `add_subdirectory` (a short binary directory, the demo apps off, scripting on) configures, builds its own material shader with the engine's shader function, and links every package, with no workarounds of its own. The paths a game's targets need (the Iris bridge's directory, the shader include directory) reach it from the defining function's location or a cache variable rather than a directory variable; every engine target gives its consumers C++20 as a `PUBLIC` compile feature; and the engine's generated outputs (the API description tool's, the Iris bridge, the scripting SDK, each package's contract, Catch2) are written under the engine's own project build directory, not the top one. The outside project that proves it is created here in its first form: a material shader, an executable linking every package, and scripting's run-time files deployed beside it through the existing scripting deploy function, kept beside the CMake script tests in an `OutsideProject` folder, run by hand, not registered in CTest.

**Blocked by:** None (can start immediately)
**Touches:** `cmake/FenrirShaders.cmake`, `cmake/FenrirDescribeApi.cmake`, `cmake/FenrirCatch2.cmake`, `packages/Iris/CMakeLists.txt`, `packages/Iris/cmake/`, `packages/FenrirScripting/CMakeLists.txt`, `packages/FenrirScripting/cmake/`, `packages/FenrirUI/Managed/CMakeLists.txt`, the packages' `CMakeLists.txt` where the C++ standard is set, `CMakeLists.txt`, `cmake/Tests/OutsideProject/`

**Status:** resolved

Bottom of the stack 01, 02, 03, 05; 04 runs beside it off the base.

- [x] The outside project configures and builds with the engine as a subdirectory and no workaround in its own CMake (the throwaway version on the local `prototype/engine-as-subdirectory` branch is the starting point, with its two workarounds removed).
- [x] Its own material shader compiles with the engine's shader function, called from its own directory.
- [x] A target that links only the app package compiles as C++20 without setting the standard itself.
- [x] No engine output lands directly in the outside project's build root.
- [x] The engine still configures, builds and tests as the top-level project (`msvc-x64-debug`), the demo apps and their smoke runs included.
