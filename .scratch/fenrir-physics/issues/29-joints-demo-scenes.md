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
