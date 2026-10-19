# 15: DemoApp Character (C#) scene

**What to build:** A DemoApp scene built entirely by a C# script shows the C# character surface working live, with its own C# follow camera (spec: "DemoApp: the two F5 scenes and the follow camera"). C++ places only the `CSharpScene` marker with id 3 (`CSharpScenes::characterId`) and registers the scene after Character (C++), so F5 steps Default → Character (C++) → Character (C#) → Default; without scripting F5 still toggles Default ⇄ Character (C++).

- **Camera:** a C# `CharacterCamera` system writes the scene's `DemoCamera`, which ticket 03's `DemoCameras` copies into the camera; C frees it and snaps back, Q/E orbit, walking relative to the camera's yaw.
- **Character:** a capsule with a `Collider` and a controller adding gravity from `PhysicsWorld.Gravity`, jumping and steering the written-back `Velocity` toward `input × speed` by at most `acceleration × dt`.
- **Level:** a slide ramp (`StopOnSlope = false` at the start); a crouch tunnel reading `ShapeBlocked`; an ice patch whose friction, read from `GroundCollider`'s `PhysicsMaterial`, lowers the acceleration cap; a trigger pad logging `TriggerEnter` naming the character; one kinematic lift so `CarriedVelocity` shows; a landing counter (from comparing `State.Ground` with last tick).
- **Keys:** the same letters for the same jobs as C++ (WASD/arrows, Shift, Space, Q/E, C, X, T, L, P, and 1 toggling `StopOnSlope`), plus F dash (`MoveCast` ahead filtered with `Layers`, teleport to the safe distance, log the hit's name and distance) and K push out (teleport half into a test wall, log `Penetrations`' count and deepest depth, apply `Depenetrate`). The character's name shows its readouts every frame.
- **Autopilot:** P walks the slide ramp, ice, tunnel, trigger pad and lift. The smoke run presses P, about 25 L presses, P again, taps each scene key twice, F and K once, and finally T.
- It does not repeat the C++ scene's push masses, paddle, ball, second character, scale or mesh keys.

**Blocked by:** 13 (Motion tests in C#); 14 (Characters in C#).

**Status:** ready-for-agent

- [ ] `FENRIR_DEMO_SCENE="Character (C#)"` with `FENRIR_DEMO_KEYS=1` runs the autopilot and keys with no errors, in Debug and Release.
- [ ] F5 steps Default → Character (C++) → Character (C#) → Default, logging each scene's key help; without scripting F5 toggles Default ⇄ Character (C++).
- [ ] The scene builds with no warning; the C# camera follows, the character walks, jumps and slides down the ramp; it is slow to start and stop on ice; the tunnel behaves as in C++ (checked by hand).
- [ ] F stops short and logs the hit; K logs penetrations and resolves out; the trigger pad logs `TriggerEnter` naming the character; the lift shows `CarriedVelocity`; the landing counter rises once per landing (checked by hand).
