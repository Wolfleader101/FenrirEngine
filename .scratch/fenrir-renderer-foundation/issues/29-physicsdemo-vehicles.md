# 29: PhysicsDemo: the vehicle scenes

**What to build:** PhysicsDemo gains the last four legacy scenes, ported faithfully: Vehicles (C++), Motorcycle (C++), Tank (C++) and Vehicles (C#), so every physics scene the DemoApp showed lives on (spec: "Demo apps" (PhysicsDemo scenes, the faithful port); ticket 23).

- **Scenes,** after ticket 28's six: Vehicles (C++), Motorcycle (C++), Tank (C++), Vehicles (C#), each its own source file.
- **Faithful port,** with ticket 28's list of changes: chase cameras as `Camera` entities (C frees the camera and snaps back, as now), the gamepad from `Gamepads`, debug lines through FenrirDebugDraw, `DemoMesh` as `MeshRenderer`, readouts and autopilots unchanged.
- **README:** the scenes and keys, and physics ticket 48's hand-check list (vehicles) moved in from the legacy README.

**Blocked by:** 28 (PhysicsDemo: Playground, Joints and Character, in C++ and C#).

**Touches:** `examples/PhysicsDemo/`

**Status:** ready-for-agent

- [ ] `FENRIR_DEMO_SMOKE` with `FENRIR_DEMO_KEYS=1` runs all ten PhysicsDemo scenes on null and GL with no errors, running each vehicle autopilot and printing the legacy log lines.
- [ ] Each vehicle scene's bodies, keys, readouts and log lines match the legacy scene (checked against the legacy sources).
- [ ] By hand, the moved vehicles hand-check list passes: the car, motorcycle and tank drive, the chase camera follows and frees, the readouts are live (checked by hand).
