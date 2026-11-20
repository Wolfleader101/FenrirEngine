# How does the game build against the engine?

Type: grilling
Status: resolved
Blocked by: 01

## Question

The game lives in `games/FlightlessFrenzy/` in this repo, apart from `examples/`, and builds against the engine as an outside program would (settled while charting). How, exactly?

- Which CMake targets it links, and whether it is part of the root build behind an option or a project of its own that finds the engine.
- Whether it may use anything from `examples/DemoScaffold` or `examples/assets`, or only public engine packages.
- Where its C# project sits and how it joins `FenrirScripting.sln`, if the game has C# ("C++, C#, or a split?" decides that; this ticket covers both outcomes or waits for it).
- Where its assets live and how they reach the built program.
- Whether CI builds it and runs its tests.
- What would have to change to move it to its own repo later; anything that makes that hard is an engine gap.

## Answer

Grilled 2026-11-19. **The rule: treat the game as its own repo that happens to sit in this one.** It leans on no demo code.

- **A project of its own.** `games/FlightlessFrenzy/` has its own top-level `CMakeLists.txt` and its own presets. It adds the engine by path with `add_subdirectory`, exactly as it would add a git submodule, with `FENRIR_BUILD_EXAMPLES` off. The engine's root `CMakeLists.txt` and presets know nothing about the game; there is no `FENRIR_BUILD_GAMES` option. Moving to a real repo is changing that one path to a submodule. The cost, accepted: a second build tree that compiles the engine again, apart from the engine's own build.
- **Install, export and packaging are not asked of the engine.** A submodule added with `add_subdirectory` is the route for a game in another repo.
- **Only public engine pieces.** The game links package targets (`FenrirApp`, `FenrirRenderer`, `FenrirPhysics`, `FenrirUI` and the rest it uses) and calls only CMake functions from the engine's `cmake/` and `packages/`. With examples off, the scaffold and demo targets do not exist, so this holds by construction.
- **Nothing from `examples/`.** No `DemoScaffold`, no `examples/assets`. The game writes its own start-up: installing the packages, its scenes, its camera.
- **Two build helpers move into the engine first**, as engine work ahead of the game's first ticket: building a C# script project and deploying it beside the executable, and copying an asset folder beside the executable. The script-project half is needed only if "C++, C#, or a split?" brings in C#. Tracked in [A game cannot build its scripts or copy its assets without demo code](../../engine-gaps/issues/02-building-a-game-against-the-engine.md), which also holds what the engine's CMake assumes about being the top-level project.
- **Layout:**

  ```
  games/FlightlessFrenzy/
    CMakeLists.txt
    CMakePresets.json
    README.md
    src/        C++ (the host at least)
    scripts/    the C# project, if the game has C#
    shaders/    the game's material shaders
    assets/     copied beside the executable
    tests/
  ```

  The C# project sits in a solution of the game's own (`FlightlessFrenzy.sln`), not in `FenrirScripting.sln`.
- **Tests:** the game has its own unit tests. It has **no automated smoke run**.
- **CI:** none for the game for now.

Which of the directories exist, and how tests are written, follow "C++, C#, or a split?".
