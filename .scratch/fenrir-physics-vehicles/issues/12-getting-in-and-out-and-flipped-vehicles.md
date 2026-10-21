# 12: Getting in and out, and flipped vehicles

**What to build:** The recipes for a character getting in and out of a vehicle and for righting a flipped vehicle, written into the docs and shown working in the C++ scene, with no seat component, flip helper or collision groups (spec: "trailers, getting in and out, flipping (recipes)", "ground, grip and other bodies" (general collision groups: not needed), "DemoApp: the two F6 scenes" (getting in and out, flipped)).

- **Docs on `Vehicle`:** getting in (set `CharacterBody::enabled = false`, parent the character under the chassis or a seat child at a seat-local `Transform`; it follows the chassis's interpolated pose); getting out (unparent, place beside the door, clear with `Depenetrate` or `MoveCast`, re-enable); flipped (`state.wheelsOnGround == 0` with the chassis's up turned away for a while, then a `Transform` write upright or a torque; a `maxTilt` of about 60° stops rollovers). Engine code gains nothing.
- **DemoApp (C++), demo code following the recipe, in the Vehicles (C++) scene:** the scene starts with the driver character seated in the car; F gets out (unparented beside the driver's door, cleared with `Depenetrate`, re-enabled; the chase camera follows it, and it walks with a small copy of the character scene's steering: WASD relative to the camera, Shift to run, Space to jump, no crouch); F within 3 m of the car gets in and drives it. The car is the scene's only drivable vehicle (the motorcycle and the tank have scenes of their own, and ticket 09's trailer has no driver), so there is no Tab to move the driver. While nobody is driving, the car gets handbrake 1, which the trailer copies. T teleports the character when on foot.
- **Flipped:** a vehicle is marked "flipped" in its readout once `wheelsOnGround` has been 0 for 2 s with its up more than 60° from world up. R rights the driven vehicle (a `Transform` write upright, lifted 1 m, velocities zeroed); K kicks it over with a roll torque.
- **Autopilot:** P gets the driver back into the car first. The smoke run presses K, R, F, F and T once each, in that order, after its key taps.

**Blocked by:** 03 (Wheel settings, probe and tilt limit); 10 (Ground, grip and other bodies).

**Status:** resolved

- [x] `FenrirPhysicsTests`: a disabled character parented under a chassis follows it and is not seen by the chassis, the probes or queries; re-enabled beside the chassis after `Depenetrate` it overlaps nothing.
- [x] The doc comments carry both recipes and the tilt note.
- [x] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs the autopilot and keys, including K, R, F, F and T, with no errors, in Debug and Release.
- [ ] F gets out beside the door, not inside anything, and the character walks; F within 3 m of the car gets in and drives it; K flips the car, "flipped" shows, and R rights it; with 7 on, K can't roll it past 60° (checked by hand).

**Resolved by:** PR #66 (branch claude/distracted-gould-56a3c2), into dotnet-scripting. The proof is `FenrirPhysicsTests`' `[drivers]` case (`Tests/VehicleDrivers.cpp`), the recipes in the `Vehicle` doc comment, and the `FENRIR_DEMO_SCENE="Vehicles (C++)" FENRIR_DEMO_KEYS=1` smoke run, which now presses K, R, F, F and T after its key taps. The last box is left for a person. Findings and choices:
- **The DemoApp bullets were rewritten before building:** the motorcycle and the tank moved to scenes of their own (tickets 07 and 08), so the car is the Vehicles (C++) scene's only drivable vehicle (09's trailer has no driver). F gets in within 3 m of the car, there is no Tab, and the hand check is "F near the car" rather than the tank.
- **Within 3 m** is measured from the driver's centre to the car's chassis box, 0 inside it.
- **On foot the chase camera keeps its heading** behind the driver rather than swinging round to a facing (a character has none), and Q/E turn it at 90°/s, as the character scene's camera does. The pad's left stick walks too.
- **The test seats the driver over the front-left wheel,** its capsule reaching through that wheel's probe, so a probe that saw a disabled character would show; the chassis's bounds staying the box's shows the capsule is not in the compound.
- **K is an angular impulse of 2400 N m s about the forward axis with 4500 N s of lift:** enough to land the car on its roof. With 7's 60° limit on it ends propped on two wheels at the limit, since its centre of mass is then past the wheels, until R rights it.
- R rights the trailer with the car, upright behind it on the same heading, so the hitch's frames line up.
