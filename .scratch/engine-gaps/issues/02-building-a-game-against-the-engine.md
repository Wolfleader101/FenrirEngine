# A game outside `examples/` has no supported way to build against the engine

Status: needs-triage
Blocked by: none

## What is missing

Everything a program needs to stand on the engine without leaning on demo code:

- **No install, export or package** for the engine's CMake targets; the only route is a build that contains the engine's source (root `CMakeLists.txt:89-171`). No README covers consuming the engine.
- **`fenrir_add_shaders`** reads `FENRIR_SHADER_INCLUDE_DIR`, a non-cache variable set in the root `CMakeLists.txt`, so a caller outside that scope may not see it (`cmake/FenrirShaders.cmake:20,65`). Read from the source, not tried.
- **No engine-level function builds a game's C# script project.** `fenrir_add_demo_scripts` is in the demo scaffold and always passes the `Fenrir.Demo` contract (`examples/DemoScaffold/cmake/FenrirDemoApp.cmake:97-159`). `FenrirScripting.sln` does not hold the Physics, Animation, UI or Razor projects.
- **The start-up a game needs is demo code.** Installing the packages, installing scripting with every extension, copying asset folders beside the executable and registering smoke runs all live in `examples/DemoScaffold/`, built only with `FENRIR_BUILD_EXAMPLES`. ADR-0071 leaves lifting any of it into a package as "a later decision, taken only once a game asks for it".

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)), which lives in `games/FlightlessFrenzy/` and is meant to build as an outside program would. "How does the game build against the engine?" decides the game's side.

## Does anything plan it?

No map, spec or ticket. ADR-0071 names the trigger: a game asking.

## Does it block the game?

Partly. Inside this repo the game can be added to the root build and write its own start-up, so a first build is possible. It blocks the stated aim of using only public engine pieces, and it blocks moving the game to its own repo.

## Comments
