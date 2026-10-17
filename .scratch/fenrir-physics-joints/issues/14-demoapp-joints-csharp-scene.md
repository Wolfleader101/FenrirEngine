# 14: DemoApp Joints (C#) scene

**What to build:** A DemoApp scene built entirely by a C# script shows the four joint kinds the C++ scene lacks (spec: "DemoApp: Joints (C#)", "DemoApp: scene switching"). C++ places only the `CSharpScene` marker; `scripts/Physics/Joints.cs` builds everything in one system, linking to entities it created in that system, with frames from `Joint.FrameFromWorld`. F4 now steps Default → Joints (C++) → Joints (C#) → Default; without scripting it still toggles Default ⇄ Joints (C++).

- **Breakable bridge:** planks hinged end to end between two static posts; the middle hinge has a `BreakForce`, read through `EventReader<JointBroken>` and logged.
- **Kind swap:** the middle joint swaps between hinge and fixed by assigning `Kind`.
- **Motorised lift:** a slider with limits, its motor driven through `Kind.Slider`.
- **Spring rope:** a distance joint with a spring holding a bobbing weight from a world point, frames from the world overload.
- **Limb:** a swing-twist arm with cone limits and an orientation motor.
- **Keys:** B drop a weight on the bridge (made at runtime with `Entity.Create`); R repair; K swap the middle joint; 1 cycle the lift motor (off, up, down, position); 2 toggle the limb motor between rest and a raised pose; 0 log every joint's state. Registered for the smoke run. Readings go into joint entity names every frame.

**Blocked by:** 13 (Joint state and JointBroken in C#).

**Status:** ready-for-agent

- [ ] The scene builds with no warning and no inactive joint (checked by hand).
- [ ] B breaks the bridge with one logged event and R repairs it; K visibly stiffens the bridge; 1 drives the lift between its limits and to a position; the rope bobs; the limb stays in its cone and 2 raises it; names show readings live (checked by hand).
- [ ] With `Joints` lines on, every joint's frames coincide at rest (checked by hand).
- [ ] `FENRIR_DEMO_SCENE` with `FENRIR_DEMO_KEYS=1` runs the scene through its keys with no errors, in Debug and Release.
