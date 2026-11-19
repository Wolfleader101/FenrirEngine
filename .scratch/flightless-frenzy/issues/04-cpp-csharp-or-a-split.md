# C++, C#, or a split?

Type: grilling
Status: resolved
Blocked by: 01

## Question

What language is the game written in? All C++, all C# on FenrirScripting with Razor panels for its UI, or a split, and if a split, where the line falls (for example: gameplay and UI in C#, with C++ only for the program's entry point and anything C# cannot reach).

Rest the answer on what the inventory says C# can and cannot reach today. Each thing the chosen language cannot reach is an engine gap, not a reason to write that part in the other language quietly. Decide also how game logic is tested in the chosen language.

## Answer

Grilled 2026-11-19. **The game is written in C#; C++ is only the host program.**

- **Gameplay and UI are C#** on FenrirScripting, with Razor panels for the HUD and menus. Hot reload suits tuning movement and levels, and the scripting layer is the engine surface most in need of a real client. The costs, accepted: the gaps below, and a .NET runtime on the machine.
- **The C++ host** starts the app, installs the packages and scripting with its extensions, and registers the game's material shaders. It holds no gameplay. Shader registration stays there for good, since shaders are built by CMake. Anything else C# cannot reach is fixed in the engine and does not grow the host.
- **Scenes:** the game is not designed around a single scene. Destroying and recreating a scene is how a level restarts, because that resets the physics world with it. The engine binds scene create, switch and destroy for C# before the game's restart and title-screen tickets; the first slice does not need it.
- **Parenting:** the engine lets C# parent entities before the game needs it. There is no stand-in (a cage holding a friend, an effect following Kiki, a camera rig), and it likely blocks an early ticket. Both are in [C# cannot switch scenes, stop the app or parent entities](../../engine-gaps/issues/11-what-csharp-cannot-reach.md).
- **Game state that is not an entity** (the timer, the score, the current state) lives as components on one "run" entity for now. The engine should let C# define resources, and the game moves to them when it can: [C# cannot define resources](../../engine-gaps/issues/13-csharp-defined-resources.md), not blocking.
- **Tests:** the game's rules (timer, scoring, power-up state, jump and glide maths, enemy decisions) are plain C# types with no engine calls, unit-tested with `dotnet test` in `tests/`. The C# systems are thin adapters and are not unit-tested. The C++ host has no logic and no tests.

So the layout from "How does the game build against the engine?" keeps `src/` (the host), `scripts/`, `shaders/`, `assets/` and `tests/`, and the script-project build helper is needed.
