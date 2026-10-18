# 14: DemoApp Joints (C#) scene

**What to build:** A DemoApp scene built entirely by a C# script shows the four joint kinds the C++ scene lacks (spec: "DemoApp: Joints (C#)", "DemoApp: scene switching"). C++ places only the `CSharpScene` marker; `scripts/Physics/Joints.cs` builds everything in one system, linking to entities it created in that system, with frames from `Joint.FrameFromWorld`. F4 now steps Default → Joints (C++) → Joints (C#) → Default; without scripting it still toggles Default ⇄ Joints (C++).

- **Breakable bridge:** planks hinged end to end between two static posts; the middle hinge has a `BreakForce`, read through `EventReader<JointBroken>` and logged.
- **Kind swap:** the middle joint swaps between hinge and fixed by assigning `Kind`.
- **Motorised lift:** a slider with limits, its motor driven through `Kind.Slider`.
- **Spring rope:** a distance joint with a spring holding a bobbing weight from a world point, frames from the world overload.
- **Limb:** a swing-twist arm with cone limits and an orientation motor.
- **Keys:** B drop a weight on the bridge (made at runtime with `Entity.Create`); R repair; K swap the middle joint; 1 cycle the lift motor (off, up, down, position); 2 toggle the limb motor between rest and a raised pose; 0 log every joint's state. Registered for the smoke run. Readings go into joint entity names every frame.

**Blocked by:** 13 (Joint state and JointBroken in C#).

**Status:** resolved

- [ ] The scene builds with no warning and no inactive joint (checked by hand).
- [ ] B breaks the bridge with one logged event and R repairs it; K visibly stiffens the bridge; 1 drives the lift between its limits and to a position; the rope bobs; the limb stays in its cone and 2 raises it; names show readings live (checked by hand).
- [ ] With `Joints` lines on, every joint's frames coincide at rest (checked by hand).
- [x] `FENRIR_DEMO_SCENE` with `FENRIR_DEMO_KEYS=1` runs the scene through its keys with no errors, in Debug and Release.

## Answer

Built as specified: `examples/DemoApp/scripts/Physics/Joints.cs` builds and runs the scene; C++ only places the marker (`CSharpScenes::jointsId` 2, `CSharpScenes::Joints()`) and registers it after Joints (C++), so F4 steps Default → Joints (C++) → Joints (C#) → Default. Without scripting nothing registers it, so F4 still toggles Default ⇄ Joints (C++).

- **Two build frames, one system.** `FrameFromWorld` reads the bodies' world matrices, but an entity made by `Entity.Create` gets its Transform only when the system returns. So `JointsBuilder` first makes the bodies and the joint entities, each `Joint` linked to bodies made earlier in the same call, with the joints disabled and the moving bodies kinematic so nothing moves (`CSharpScene.Built` = 1). On the next frame it gives each joint its frames from `FrameFromWorld`, enables it and makes the bodies dynamic (`Built` = 2).
- **Bridge:** four 2 kg planks, built at their equilibrium sag for four equal links (inner planks 10° below level, outer ones' tangent three times that), so the hinges rest at about 0 rad with their frames together. It holds about 55 N of tension at rest. The middle hinge breaks over 250 N; a 10 kg weight from B breaks it at about 630 N.
- **Rope:** its frames come from the world overload at the weight's top, with FrameB moved to the hook. A distance joint's frames are apart by its length, so with the joints lines on they do not coincide.
- **Limb:** it starts with an 8 rad/s swing about world X, so it swings against its 1 rad cone once before its 1 Hz orientation motor settles it at rest.
- **Checked by running** (`FENRIR_DEMO_SCENE="Joints (C#)" FENRIR_DEMO_KEYS=1`, keys 0 K K B 0 R 1 1 1 1 2 2 0) in clang Debug and Release: the scene built with 8 joints and no warning. K swapped the kind both ways. B broke the middle hinge with one logged `JointBroken`, and R repaired it without a second break. 1 cycled the lift's motor, 2 raised and lowered the limb, and 0 logged every state: the rope at 1.85 m (1.6 m plus a 0.25 m stretch), the lift at 0 m holding 49 N. No errors.
- **Left for the hand check:** the items above marked "checked by hand".
