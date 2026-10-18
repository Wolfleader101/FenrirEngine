# What do the character demo scenes show?

Type: grilling
Status: open
Blocked by: 34, 35, 37

## Question

The character spec ships two DemoApp scenes on F5 through `DemoScenes` (examples/DemoApp/src/DemoScenes.hpp): "Character (C++)" and "Character (C#)", the C# one built only from C# as `scripts/Physics/Joints.cs` is. Builds on [What does game code read back from a character, and which events?](34-character-state-and-events.md), [How does a character interact with other bodies?](35-characters-and-other-bodies.md) and [Spike: a character walking a small level through the proposed model](37-spike-character-walking-a-level.md).

- **Camera:** the DemoApp's camera is a free-fly camera (F1, WASD). A character scene needs a follow camera and keys that drive the character instead; both are demo code, not engine code. How the keys are shared with the free-fly camera.
- **Contents:** what each scene shows (slopes, stairs, a platform, pushable boxes, a crouch tunnel, triggers), and how the C# scene proves the C# surface rather than repeating the C++ one.
- **Readouts:** ground state shown live (in an entity's `Name`, as the joints scenes do; there is no text overlay).
- **Debug lines:** whether characters get a debug-line category (shape, ground normal, velocity).
- **The spike's scene:** whether it becomes the C++ scene.
- **Hand checks:** what the engine developer verifies by hand, and the `FENRIR_DEMO_KEYS` smoke runs.
