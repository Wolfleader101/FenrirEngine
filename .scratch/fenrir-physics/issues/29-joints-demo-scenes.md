# What do the joints demo scenes show, and how do several scenes switch?

Type: grilling
Status: open
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
