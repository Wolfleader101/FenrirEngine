# 14: DemoApp gamepad driving

**What to build:** The engine developer drives with a gamepad as well as the keyboard, so throttle and steering are analog when checking handling. Demo code only; the engine gains nothing (spec: "DemoApp: the two F6 scenes, the chase camera and the gamepad" (gamepad)).

- **`DemoGamepad { int Connected; Vec2 LeftStick; Vec2 RightStick; float LeftTrigger; float RightTrigger; uint32 Buttons; }`** (`Demo.DemoGamepad`, registered in the demo's scripting extension when scripting is on), written each frame by the DemoApp from the first connected GLFW gamepad onto an entity in the active scene: `DemoCamera`'s pattern turned around, C++ owning the device and C# reading it (ticket 16).
- **Vehicles (C++)** reads it; keyboard or pad both work, the pad's value winning beyond a deadzone of about 0.15, analog values going straight into `VehicleInput`. The mapping is the spec's table: right trigger `forward`; left trigger `brake`, then reverse once stopped; left stick X `steer` (walk on foot); A handbrake (jump on foot); bumpers shift down and up; Y get out / get in; Back rights the vehicle; right stick orbits the chase camera, springing back behind on release.
- Unplugging mid-drive falls back to the keyboard with no stuck input; the other scenes ignore the pad. Smoke runs can't press the pad.

**Blocked by:** 01 (Vehicle tracer bullet).

**Status:** ready-for-agent

- [ ] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors with no pad connected, in Debug and Release.
- [ ] With a pad connected: analog throttle, brake and steer drive; the bumpers shift in Manual; the right stick orbits and springs back; Y and Back work once tickets 12's getting in and out and flip reset are in (checked by hand).
- [ ] Unplugging mid-drive falls back to the keyboard with no stuck input (checked by hand).
