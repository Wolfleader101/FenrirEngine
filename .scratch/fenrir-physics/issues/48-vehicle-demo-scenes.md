# What do the vehicle demo scenes show?

Type: grilling
Status: open
Blocked by: 44, 45, 46

## Question

The vehicles spec ships two DemoApp scenes on F6 through `DemoScenes` (examples/DemoApp/src/DemoScenes.hpp): "Vehicles (C++)" and "Vehicles (C#)", the C# one built only from C# as `scripts/Physics/Joints.cs` and the character C# scene are. Builds on [How does a vehicle step each tick, and what does game code read back?](44-how-a-vehicle-steps-and-reports.md), [How does a vehicle meet other bodies, joints and characters?](45-vehicles-and-other-bodies.md) and [Spike: a car, a motorcycle and a tank driving a small track through the proposed model](46-spike-vehicles-on-a-track.md), and follows "What do the character demo scenes show?".

- **Camera and keys:** reusing the character scenes' `DemoCamera` follow camera, and how driving keys share the keyboard with the free-fly camera (F1, WASD).
- **Contents:** which kinds each scene shows (car, motorcycle, tank), the track (ramps, bumps, a low-grip surface, loose bodies, a trailer), and how the C# scene proves the C# surface rather than repeating the C++ one.
- **Readouts:** speed, RPM, gear and wheel contact in entity `Name`s, as the earlier scenes do (there is no text overlay).
- **Debug lines:** a `DebugDraw::Vehicles` category (wheel casts, suspension, contact points and normals, slip).
- **Wheels:** wheel meshes drawn from written-back wheel poses.
- **The spike's scene:** whether it becomes the C++ scene.
- **Smoke runs:** an autopilot for `FENRIR_DEMO_KEYS`, and `FENRIR_DEMO_SCENE`.
- **Hand checks:** what the engine developer verifies by hand.
