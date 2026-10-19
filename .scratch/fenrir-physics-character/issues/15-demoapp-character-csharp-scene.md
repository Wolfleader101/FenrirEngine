# 15: DemoApp Character (C#) scene

**What to build:** A DemoApp scene built entirely by a C# script shows the C# character surface working live, with its own C# follow camera (spec: "DemoApp: the two F5 scenes and the follow camera"). C++ places only the `CSharpScene` marker with id 3 (`CSharpScenes::characterId`) and registers the scene after Character (C++), so F5 steps Default → Character (C++) → Character (C#) → Default; without scripting F5 still toggles Default ⇄ Character (C++).

- **Camera:** a C# `CharacterCamera` system writes the scene's `DemoCamera`, which ticket 03's `DemoCameras` copies into the camera; C frees it and snaps back, Q/E orbit, walking relative to the camera's yaw.
- **Character:** a capsule with a `Collider` and a controller adding gravity from `PhysicsWorld.Gravity`, jumping and steering the written-back `Velocity` toward `input × speed` by at most `acceleration × dt`.
- **Level:** a slide ramp (`StopOnSlope = false` at the start); a crouch tunnel reading `ShapeBlocked`; an ice patch whose friction, read from `GroundCollider`'s `PhysicsMaterial`, lowers the acceleration cap; a trigger pad logging `TriggerEnter` naming the character; one kinematic lift so `CarriedVelocity` shows; a landing counter (from comparing `State.Ground` with last tick).
- **Keys:** the same letters for the same jobs as C++ (WASD/arrows, Shift, Space, Q/E, C, X, T, L, P, and 1 toggling `StopOnSlope`), plus F dash (`MoveCast` ahead filtered with `Layers`, teleport to the safe distance, log the hit's name and distance) and K push out (teleport half into a test wall, log `Penetrations`' count and deepest depth, apply `Depenetrate`). The character's name shows its readouts every frame.
- **Autopilot:** P walks the slide ramp, ice, tunnel, trigger pad and lift. The smoke run presses P, about 25 L presses, P again, taps each scene key twice, F and K once, and finally T.
- It does not repeat the C++ scene's push masses, paddle, ball, second character, scale or mesh keys.

**Blocked by:** 13 (Motion tests in C#); 14 (Characters in C#).

**Status:** resolved, built on branch `claude/character-csharp-scene-bb0c63`; the hand checks below wait for the engine developer. The scene is `examples/DemoApp/scripts/Physics/Character.cs`; C++ adds only `CSharpScenes::characterId`, `characterName` and `Character()` and registers it in `main.cpp` right after `CharacterScene::Scene()`, under `FENRIR_SCRIPTING`. Where the build settled something the ticket left open:

- **Slides:** with no walk on a slope `StopOnSlope` does not hold, the controller does not brake, so the slide speeds up at gravity's pace (about 4 m/s down the ramp) rather than creeping; on flat ground it brakes as usual. Steering towards zero with no walk, as the C++ scene does, left a 0.08 m/s creep.
- **Ice:** the acceleration cap is scaled by the ground's friction over the default 0.5, at least a tenth: 3 m/s² on the ice (friction 0.05) against 30 elsewhere. The name shows the friction while it is not the default.
- **Layers:** the character is in its own layer; a ghost pane north of the start masks it out, so the character walks through it and the dash (filtered like the character) passes it and stops at the test wall behind.
- **Push out (K)** puts the pose half into the test wall, logs `Penetrations` there and teleports the character straight to that pose plus `Depenetrate`'s offset, so the move's own recovery never does the work.
- **The look** is an entity of its own moved onto the character each frame: scaling the character's Transform would scale its collider.
- **Smoke run:** 40 L presses, not about 25, so the autopilot finishes its route (about 32 s); T is among the keys tapped twice, so F dashes from the start into the test wall. In clang Debug and Release it logs the slide, the ice, `shapeBlocked` through the tunnel, `TriggerEnter`/`TriggerExit` naming the character, `carried (0, 1.5, 0)` after the lift, the dash stopping 7.68 m on at the test wall, and one penetration of 0.30 m pushed out by 0.32 m, with no warnings or errors.
- **Playground.cs** now logs trigger and contact events only while its own scene is active: it was logging this scene's trigger pad too.
- Space also sends `Controls.cs`'s spark burst in this scene, as it does in Character (C++).

- [x] `FENRIR_DEMO_SCENE="Character (C#)"` with `FENRIR_DEMO_KEYS=1` runs the autopilot and keys with no errors, in Debug and Release.
- [ ] F5 steps Default → Character (C++) → Character (C#) → Default, logging each scene's key help; without scripting F5 toggles Default ⇄ Character (C++). (Default's help lists "F5 Character (C++), F5 Character (C#)"; the stepping is by hand.)
- [ ] The scene builds with no warning; the C# camera follows, the character walks, jumps and slides down the ramp; it is slow to start and stop on ice; the tunnel behaves as in C++ (checked by hand). (The script builds with no warning.)
- [ ] F stops short and logs the hit; K logs penetrations and resolves out; the trigger pad logs `TriggerEnter` naming the character; the lift shows `CarriedVelocity`; the landing counter rises once per landing (checked by hand).
