# What do the joints demo scenes show, and how do several scenes switch?

Type: grilling
Status: resolved
Blocked by: 23, 24

## Question

The joints spec ships two DemoApp scenes: one built in C++ and one built in C# (like `scripts/Physics/Playground.cs`, where C++ places only a marker), both switched like the F3 "Physics (C#)" scene (`examples/DemoApp/src/CSharpPhysicsScene.hpp`). This graduates the map's "DemoApp demo scenes" fog. Builds on [Which joint kinds are there, and what does each expose?](23-joint-kinds-and-settings.md) and [What joint state can game code read, and how do joints break?](24-joint-state-and-breaking.md).

- **Switching:** one key per scene (F4, F5) that returns to Default when pressed again, or one key cycling through every scene; whether `SwitchOnF3` generalises into one scene switcher for all demo scenes.
- **Contents:** what each scene shows (doors, chains, a motorised wheel, a breakable bridge, a swing-twist limb), and how the two scenes differ so the C# one proves the C# surface rather than repeating the C++ one.
- **Interaction:** keys or clicks that drive motors, break joints or kick bodies.
- **Debug lines:** a `Joints` category (anchors, axes, limits) drawn in the F2 cycle.
- **The spike's scene:** whether "Spike: a hinged door and a hanging chain through the proposed model" becomes the C++ scene.
- **Hand checks:** what the engine developer verifies by hand.

From "What joint state can game code read, and how do joints break?" (24): joints break on `breakForce`/`breakTorque` (one step over), physics sets `enabled = false` and sends `JointBroken`, and re-enabling repairs. Candidates: a breakable bridge or chain that breaks under a dropped weight, with a key to repair it (and see it re-break if still loaded); an on-screen readout from `PhysicsWorld::JointState` (a door's angle from `position`, a link's load from `force`) proving the readings in the hand check. The `Joints` debug lines might colour broken (disabled) joints differently.

From "How do jointed bodies stop colliding?" (25): jointed bodies do not collide by default (`collideConnected = false`), only the directly joined pair, and a broken joint's bodies collide again from the next step. Candidates: a chain whose links overlap at the rest pose without jitter, showing links two apart still colliding; a key toggling `collideConnected` on a door so it visibly catches on its frame; a breaking chain whose snapped link falls onto its neighbour. Hand check: a jointed body's trigger child still reports the other body.

From "What happens at a joint's edges?" (26): candidates for interaction and hand checks: a key destroying one chain link (its joints go inactive, the lower chain falls, one warning each); a key teleporting a jointed body alone versus with its partners; editing a sleeping door's motor target waking it. The `Joints` debug lines might show inactive joints apart from disabled (broken) ones.

From "Spike: a hinged door and a hanging chain through the proposed model" (27): the spike's scene (branch `prototype/fenrir-physics-joints-spike`, `examples/DemoApp/src/JointsSpikeScene.hpp`, F4) is a working candidate for the C++ scene: a door hinged to a scaled static post under a "Door frame" entity, an eight-link chain from a world point, a spare joint, and keys (0–9, B, R, P, L) for every hand check, each logging what it did, with a `FENRIR_JOINTS_SPIKE=keys` smoke mode. Drawing each joint's two frames as axes (dimmed when disabled) was the most useful hand-check aid, which argues for the `Joints` debug-line category. It used F4 by copying `SwitchOnF3`, so a shared scene switcher is now wanted by two scenes. A breakable joint is best placed high in a chain (the spike's chain joint 1, 15 N): a bottom joint carries too little to break once the chain rests.

From "What does C# get for joints?" (28): the C# scene must exercise the C# surface the tests cannot judge live. C++ places only a marker; C# builds the bodies, joint entities and links in one system (some links to entities created in that same system) with frames from `Joint.FrameFromWorld`, including its world overload. A key drives a motor through a kind's view (`joint.Kind.Hinge.Motor.TargetVelocity = …`), and a key swaps a kind by assignment. An on-screen readout from `PhysicsWorld.GetJointState` every frame shows an angle and a load. A breakable joint is read through `EventReader<JointBroken>`, logged, and repaired by a key writing `Enabled = true`. It need not repeat the C++ scene's edge-case keys (destroy a link, teleport), which C# reaches unchanged. The query is now `GetJointState` in C++ too.

## Answer

**There are two joints scenes on F4: "Joints (C++)" (the spike's scene, rewritten) and "Joints (C#)". Between them they show all six kinds live.** One shared DemoApp scene switcher replaces `SwitchOnF3` and the spike's copied `SwitchOnF4`. It gives each feature family one F-key, so it scales to the later families. Joint frames are drawn by a new engine debug-line category, not by the scenes.

**Facts found in the code** (on `dotnet-scripting` and the spike branch):
- **F2 desyncs after a switch.** `PhysicsDemo::m_debugMode` is one index for the whole app, but `PhysicsWorld::SetDebugDraw` is per world. After F3, the logged mode and the new scene's lines can disagree.
- **Taken keys:** F1 (camera), F2, F3, WASD, Shift and Esc. J is used only in the C# physics scene.
- **No text overlay.** The DemoApp shows live values by writing them into an entity's `Name`, shown in the Scene Hierarchy (`ScriptingDemo::ShowBobPhase`). C# can do the same with `Name.SetText`.
- **No `Joints` debug category.** `DebugDraw` has `Colliders`, `Contacts`, `Velocities` and `Bounds`. The spike drew its frames from inside its scene.

### Decisions

1. **One F-key per feature family.** Each press steps through that family's scenes and then back to Default:
   - F3: Default → Physics (C#) → Default. This is today's behaviour, because the foundation's C++ scene is Default.
   - F4: Default → Joints (C++) → Joints (C#) → Default.
   - F5–F8 are left for the character controller, vehicles, ragdolls and soft bodies.
   - Pressing another family's key jumps to that family's first scene.
   - A family's key steps only through the scenes that registered for it. Without scripting, F4 toggles Default ⇄ Joints (C++), and F3 does nothing.
   - Rejected: one key per scene (ten scenes would use up F3–F12), and one key cycling every scene (too slow once there are ten).
2. **A DemoApp scene switcher, `DemoScenes`** (`examples/DemoApp/src/DemoScenes.hpp`), not engine code. Each scene registers its name, its family key, a one-line key help logged on entry, and optionally a key sequence for smoke runs. The switcher:
   - **re-applies the F2 mode to each newly active world.** The F2 mode becomes app-wide, which fixes the desync above;
   - **reads `FENRIR_DEMO_SCENE=<name>`** and starts in that scene. This replaces the spike's `FENRIR_JOINTS_SPIKE`;
   - **with `FENRIR_DEMO_KEYS=1`, presses the active scene's registered keys.** It sends them as real `KeyboardKeyEvent`s, one a second after two seconds of settling, so C++ and C# readers both see them as a keyboard would make them. The spike instead called `OnKey` directly, which C# cannot use.

   Each scene still reads its keys in every scene and acts only while it is active, as `Playground.cs` does, so a key pressed in one scene does nothing in another.
3. **One `CSharpScene` marker, with a scene id.** `CSharpScene { int Scene; int Built; }` replaces `CSharpPhysicsScene { Built }` and is registered once in the demo's scripting extension. `Scene` is an id both languages share. `Playground.cs` and a new `scripts/Physics/Joints.cs` each build where they find their own id unbuilt.
4. **`DebugDraw::Joints`, in FenrirPhysics and its C# mirror.** It is included in `All`, and the F2 cycle gains "joints" before "all". It draws each joint's two frames as axes, as the spike did: `frameA` long (red, green, blue) and `frameB` short (pink, yellow, cyan). The frames coincide at the rest pose and while the joint holds.
   - A **disabled** (broken) joint's axes are dimmed.
   - An **inactive** joint draws only the frames on its surviving bodies, in grey.
   - **No limit arcs or cones** yet. The spike found axes enough, and limit shapes can be added later.
   - The demo scenes draw no frames of their own.
5. **Readings go into joint entity names.** Each scene writes its joints' live `GetJointState` readings into their joint entities' `Name` every frame, for example "Door hinge: 0.73 rad, 12 N" or "Chain joint 1: 39 N". This works the same from C#, which an ImGui panel would not. Key 0 still logs every joint's full state. There is no new overlay.
6. **"Joints (C++)" is the spike's scene, rewritten against the final API.**
   - **It keeps:** the door (a hinge with limit, friction and motor, on a static post scaled (0.2, 2.2, 0.2) under a "Door frame" entity), the eight-link ball chain from a world point, and the edge-case keys.
   - **It drops:** the spare joint and its P key (solve order is proven bit for bit by a headless test and is invisible by eye); the spike's frame drawing (now decision 4); and its `FrameFromWorld` bug, which treated any invalid `bodyB` as the world.
   - **It adds:** a trigger child on the door that overlaps its post and logs `TriggerEnter` with the post. This is ticket 25's check that triggers ignore the joint's no-collide rule.
   - **Keys:**

     | Key | Action |
     |---|---|
     | 1 | Push the door |
     | 2 | Cycle the door motor (off, +velocity, −velocity, position 0), logging whether the door was asleep |
     | 3 | Destroy link 3 |
     | 4 | Teleport the door alone |
     | 5 | Teleport the door and its frame |
     | 6 | Scale link 5 |
     | 7 | Add or remove a lump on link 6 |
     | 8 | Reshape link 6 |
     | 9 | Toggle `collideConnected` on the chain |
     | 0 | Log every joint's state |
     | B | Toggle a 15 N `breakForce` on chain joint 1 |
     | R | Repair disabled joints |
     | L | Toggle the door's limits |
7. **"Joints (C#)" shows the four kinds the C++ scene lacks, through the surface ticket 28 requires.** C++ places only the marker. C# builds everything in one system, linking to entities it created in that same system, with frames from `Joint.FrameFromWorld`. The scene has:
   - **Breakable bridge:** planks hinged end to end between two static posts. The middle hinge has a `breakForce`. A break is read through `EventReader<JointBroken>` and logged.
   - **Kind swap:** the bridge's middle joint swaps between hinge and **fixed** by assigning `Kind`, so the bridge visibly stiffens.
   - **Motorised lift:** a **slider** with limits, its motor driven through the `Kind.Slider` view.
   - **Spring rope:** a **distance** joint with a `JointSpring`, holding a weight that bobs from a world point. Its frames come from `FrameFromWorld`'s world overload.
   - **Limb:** a **swing-twist** arm with cone limits and an orientation motor.

   It does not repeat the C++ scene's edge-case keys, which C# reaches unchanged. Where both scenes have a key for the same job, it is the same key (B breaks, R repairs, 0 logs). The keys avoid WASD, the F-keys and J:

   | Key | Action |
   |---|---|
   | B | Drop a weight on the bridge. C# makes it at runtime with `Entity.Create` |
   | R | Repair: write `Enabled = true` on disabled joints |
   | K | Swap the bridge's middle joint between hinge and fixed |
   | 1 | Cycle the lift's motor (off, up, down, position) |
   | 2 | Toggle the limb's orientation motor between rest and a raised pose |
   | 0 | Log every joint's `GetJointState` |
8. **Hand checks** (the spec's list for the engine developer):
   - **Switching:**
     - F4 steps Default → Joints (C++) → Joints (C#) → Default, logging each scene's key help on entry.
     - F3 still works as before.
     - The F2 mode carries across a switch: the lines drawn match the logged mode.
     - A key pressed in one scene does nothing in another.
     - `FENRIR_DEMO_SCENE` with `FENRIR_DEMO_KEYS=1` runs each joints scene through its keys with no errors, in Debug and Release.
   - **Joints (C++):**
     - **Door:** it swings, stops at its limits, and its name shows its angle. With limits on, a velocity motor holds the door at its limit, not past it. This checks whichever fix the spec picks for ticket 27's lesson 2.
     - **Waking:** a motor edit on a sleeping door wakes it.
     - **Trigger child:** the door's trigger child reports its post.
     - **Chain:** it hangs with its frames together and shows its load in names. Links two apart still collide.
     - **`collideConnected`:** turning it on (9) visibly pushes overlapping links apart.
     - **Destroying a link (3):** one warning per joint naming it. The links below fall, and the inactive joints draw grey.
     - **Teleports:** the door teleported alone (4) is pulled back without a jolt. The door and frame together (5) line up at once.
     - **Shape changes (6, 7, 8):** the anchors stay put.
     - **Breaking:** B then a swing breaks joint 1 with one `JointBroken`, and it draws dimmed. R repairs it, and it breaks again if still loaded.
   - **Joints (C#):**
     - **Build:** C# builds the scene with no warning and no inactive joint.
     - **Bridge:** B drops a weight, the bridge breaks with one logged `JointBroken`, and R repairs it.
     - **Kind swap:** K swaps the middle joint to fixed and back, and the bridge visibly stiffens.
     - **Lift:** 1 drives it between its limits and to a position.
     - **Rope:** it bobs from its world point.
     - **Limb:** it stays inside its cone, and 2 raises and lowers it.
     - **Readouts:** the names show each joint's reading live.
   - **With the `Joints` lines on:** every joint's frames coincide at rest.
9. **A separate demo app is fog, not part of the joints spec.** Splitting the DemoApp would mean copying its renderer and editor today. It goes in the map's "Not yet specified" with a trigger.

**No ADR:** this is demo code and a debug-line category, both easy to reverse. **Glossary:** no new domain terms. The scene switcher, family keys and demo markers are DemoApp vocabulary, not engine language.
