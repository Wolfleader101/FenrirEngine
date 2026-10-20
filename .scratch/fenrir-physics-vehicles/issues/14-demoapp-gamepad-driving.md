# 14: DemoApp gamepad driving

**What to build:** The engine developer drives with a gamepad as well as the keyboard, so throttle and steering are analog when checking handling. Demo code only; the engine gains nothing (spec: "DemoApp: the two F6 scenes, the chase camera and the gamepad" (gamepad)).

- **`DemoGamepad { int Connected; Vec2 LeftStick; Vec2 RightStick; float LeftTrigger; float RightTrigger; uint32 Buttons; }`** (`Demo.DemoGamepad`, registered in the demo's scripting extension when scripting is on), written each frame by the DemoApp from the first connected GLFW gamepad onto an entity in the active scene: `DemoCamera`'s pattern turned around, C++ owning the device and C# reading it (ticket 16).
- **Vehicles (C++)** reads it; keyboard or pad both work, the pad's value winning beyond a deadzone of about 0.15, analog values going straight into `VehicleInput`. The mapping is the spec's table: right trigger `forward`; left trigger `brake`, then reverse once stopped; left stick X `steer` (walk on foot); A handbrake (jump on foot); bumpers shift down and up; Y get out / get in; Back rights the vehicle; right stick orbits the chase camera, springing back behind on release.
- Unplugging mid-drive falls back to the keyboard with no stuck input; the other scenes ignore the pad. Smoke runs can't press the pad.

**Blocked by:** 01 (Vehicle tracer bullet).

**Status:** resolved

- [x] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors with no pad connected, in Debug and Release.
- [ ] With a pad connected: analog throttle, brake and steer drive; the bumpers shift in Manual; the right stick orbits and springs back; Y and Back work once tickets 12's getting in and out and flip reset are in (checked by hand).
- [ ] Unplugging mid-drive falls back to the keyboard with no stuck input (checked by hand).

**Resolved by:** branch claude/vehicles-gamepad-ed872c (not yet merged into dotnet-scripting). The proof is the `FENRIR_DEMO_SCENE="Vehicles (C++)" FENRIR_DEMO_KEYS=1` smoke run with no pad connected, in Debug and Release; the two pad boxes are left for a person. Choices the ticket left open:
- `DemoGamepads` (`examples/DemoApp/src/DemoGamepad.hpp`) runs at `BeforeTicks` and writes into every `DemoGamepad` in the active scene, so a scene that wants the pad puts one on an entity (Vehicles (C++) has a "Gamepad" entity) and the other scenes never see it. The triggers are turned into [0, 1] from GLFW's [-1, 1]; `Buttons` bit n is `GLFW_GAMEPAD_BUTTON_n`; with nothing connected every field is zero, which is what releases a held input on unplugging.
- The engine's C# contract has no `Vector2`, so the demo maps `glm::vec2` to its own `DemoScripts.Vector2` (`scripts/DemoGamepad.cs`, beside `DemoGamepad` and a `GamepadButton` enum for ticket 16).
- Pad buttons press the keys they stand for while held, kept apart from the keyboard's held keys: the spec's A Space, left / right bumper Q / E (02's shifting, in Manual), Y F and Back R; and, on the buttons the spec leaves free, the scene's other keys: X M (the gearbox), B Shift (the clutch), the D-pad 1 (up), 2 (right), 7 (down) and 4 (left), and Start 9. Y and Back do nothing until ticket 12 gives F and R their actions (getting in and out, righting), and then work with no change here. Walking on foot with the left stick is 12's.
- The right stick orbits the chase camera up to 180° round and 20° up or down, and it eases back behind at the camera's own catch-up rate once the stick is released. Leaving the scene clears its `DemoGamepad`, so nothing is stuck on return.
