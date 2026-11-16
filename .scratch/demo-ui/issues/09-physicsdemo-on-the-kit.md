# 09: PhysicsDemo on the kit

**What to build:** PhysicsDemo's column of named readouts becomes readouts in the demo panel, in C++ and through Razor in the C# twins. F2's debug-line category is also a choice row. The long key help lines become key rows, and Playground, Character and Vehicles get tutorials.

**Spec:** `.scratch/demo-ui/spec.md`, "Per-demo moves" (PhysicsDemo). The inventory is `.scratch/demo-ui/research/inventory.md`, "PhysicsDemo".

- **`ShowReadouts`:**
  - `SceneLooks.cpp`'s `ShowReadouts` becomes a kit helper of the demo's that keeps one readout per named entity in a scene section, updated each frame from the entity's `Name`, as today's text is.
  - Character, Joints, Vehicles, Motorcycle and Tank use it.
  - No screen text from (16, 16) is left, so the messages' corner is clear.
- **C# twins:** Character (C#), Joints (C#) and Vehicles (C#) mount Razor panels (`razorPanel`) of `Readout`s, replacing `scripts/Looks.cs`'s `ShowReadouts` text.
- **F2:**
  - The app's debug-line category is an app section's `Choice` (off, colliders, contacts, velocities, bounds, joints, characters, vehicles, all), kept in step with `AddDebugCategoryCycling`, so F2 moves the choice and the choice sets the category.
  - `appKeyRows` replaces `appKeyHelp`.
  - The smoke runs' F2 presses stay.
- **Key rows** for every scene, gamepad bindings as their own rows.
- **Tutorials:**
  - Playground (first): the scene list, F1, F2 and the free-fly camera.
  - Character: move, jump, crouch and push.
  - Vehicles: drive, brake, the camera, the trailer and the pad.
  - Steps may end on the action they teach.
- **Gamepad:** the kit never takes it, so the pad drives the vehicles and the character whatever is focused.
- **README:** a "The demo panel" section and rewritten hand checks.

**Blocked by:** 05

**Touches:** `examples/PhysicsDemo/`

**Status:** ready-for-agent

- [ ] No `ScreenText` remains in `examples/PhysicsDemo/` (C++ or C#).
- [ ] `PhysicsDemo-Smoke-null` passes with scripting off and on, the F2 presses included.
- [ ] Hand checks, on OpenGL:
  - each scene's readouts follow the bodies
  - the debug-line choice and F2 agree
  - a gamepad drives the car with a kit row focused
  - the tutorials show once and step
  - the C# twins' Razor readouts work
- [ ] Clang and MSVC x64 debug build without new warnings.
