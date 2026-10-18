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

From "What does game code read back from a character, and which events?": the readouts can show `state.ground`, `groundBody`, `movedVelocity`, `carriedVelocity`, `onWall` and `onCeiling`; landing is derived by comparing with last tick, so a scene showing a landing counter proves the derivation. Characters send the existing contact events (with `ContactEvents`) and trigger events, so a trigger the character walks through and a `ContactEvents` character are the checks for them. A debug-line category could draw `groundNormal` and `movedVelocity`.

From "Spike: a character walking a small level through the proposed model" (37): the spike's "Characters (spike)" scene (branch `prototype/fenrir-physics-character-spike`, `CharacterScene.hpp`) is a candidate for the C++ scene:

- **Level:** a 30° ramp, a 55° slope, ten 0.3 m stairs to a 3 m ledge, a lift beside the ledge (1.5 m/s), a sideways shuttle (2 m/s), five 1 kg boxes, and a low ceiling to jump into.
- **Camera:** a follow camera placed after the camera controller in `Update`, so it wins. WASD walks while it follows, and C frees the camera, giving WASD back to it, while the arrows still walk. Q/E orbit it.
- **Readouts:** the character's `Name` shows its ground state, `velocity`, carried velocity and ground changes per second. Each ground change is logged with its tick, which is how the stair-descent flicker and the lift-turn hop were found. Logging the ground body ("on Lift") was the most useful readout.
- **Autopilot:** an autopilot route (P) gives `FENRIR_DEMO_KEYS` something to walk, since smoke keys can only tap.
- **Hand checks:** on a 60 Hz display, interpolation cannot be seen by eye. Leave it to the headless check, or drop the frame cap for the demo.
- **Debug lines:** the scene had none. The character's look is two cubes over a capsule collider, and the colliders category does not draw characters. A character category drawing the capsule, the ground normal and `velocity` would show what the look hides.
- **Game code:** the scene's Tick sets horizontal speed from the keys, which stops `stopOnSlope = false` from sliding. The demo should add input to the written-back velocity instead.

From "How does a character interact with other bodies?": hand checks for pushing and being pushed, beyond the spike's 1 kg boxes:
- boxes of several masses (1, 20, 100 kg), showing that `pushForce` (100 N) moves the light ones and not the heaviest, and a seesaw or raft the character's `mass` tips;
- a kinematic door or paddle that shoves the character at its own speed, and a ball thrown at a standing character that bounces off without moving it, with a `ContactEvents` readout proving the bounce is reported (the inner body's step contacts);
- two characters (one autopilot) blocking each other, one standing on the other and riding it;
- a box resting on the character's head, which is not carried (a stated limit).
A character debug-line category could draw the capsule its inner body presents to bodies and queries.
