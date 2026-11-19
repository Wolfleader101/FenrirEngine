# How does C# create, switch and destroy scenes, and stop the app?

Type: grilling
Status: open
Blocked by: none
Settles: [C# cannot switch scenes, stop the app or parent entities](11-what-csharp-cannot-reach.md) (scenes and the app)

## Question

Flightless Frenzy restarts a level by destroying and recreating its scene, so that its physics world resets, and keeps the title screen and the level as separate scenes ([C++, C#, or a split?](../../flightless-frenzy/issues/04-cpp-csharp-or-a-split.md), [What is the game loop?](../../flightless-frenzy/issues/06-the-game-loop.md)). What does C# get?

- **Scenes:** create a named scene, make it active, destroy it; what a C# system in a scene being destroyed sees (it is mid-tick), and when the destroy takes effect (end of frame).
- **What a new scene starts with:** which packages install their per-scene state (physics world, renderer resources, UI roots) when C# creates one, and whether C# can say which, or every installed package always does.
- **Which scene a script runs in,** and whether one script assembly's systems run in every scene or per scene.
- **The app:** `App.Stop` from C#, beside `Window.RequestClose`; the tick rate and elapsed time as read-only values.
- **Scene files:** loading a scene from a file is the `fenrir-serialisation` map's ("How does C# define data asset types and reach scene and asset data?", its ticket 16). This ticket covers code-built scenes only, and its API must leave room for a scene loaded from a file to arrive through the same create call.

Outcome: a spec-ready design.

## Facts (checked 2026-11-19)

- C++ has scene create, switch and destroy (`packages/FenrirApp/include/FenrirApp/App.hpp:620-630`); C# has no binding for them and there is no Scene facade class.
- No `App.Stop` binding; `Window.RequestClose` is bound (`packages/FenrirWindow/Managed/Fenrir.Window/Generated/Window.cs:74`).
- C# `Time` is only `DeltaTime` and `TickRate` (`packages/FenrirScripting/Managed/Fenrir.Scripting/Time.cs`).
- Every package touches only `app.GetActiveScene()` (`packages/FenrirPhysics/src/Physics.cpp:22,49,60`, `packages/FenrirSkeleton/src/Skeletons.cpp:139,181`, renderer README:52); `ForEachScene` (`App.hpp:635`) is not for per-frame work.
