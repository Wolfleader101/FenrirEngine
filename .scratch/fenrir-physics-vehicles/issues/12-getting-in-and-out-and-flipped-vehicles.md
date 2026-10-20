# 12: Getting in and out, and flipped vehicles

**What to build:** The recipes for a character getting in and out of a vehicle and for righting a flipped vehicle, written into the docs and shown working in the C++ scene, with no seat component, flip helper or collision groups (spec: "trailers, getting in and out, flipping (recipes)", "ground, grip and other bodies" (general collision groups: not needed), "DemoApp: the two F6 scenes" (getting in and out, flipped)).

- **Docs on `Vehicle`:** getting in (set `CharacterBody::enabled = false`, parent the character under the chassis or a seat child at a seat-local `Transform`; it follows the chassis's interpolated pose); getting out (unparent, place beside the door, clear with `Depenetrate` or `MoveCast`, re-enable); flipped (`state.wheelsOnGround == 0` with the chassis's up turned away for a while, then a `Transform` write upright or a torque; a `maxTilt` of about 60° stops rollovers). Engine code gains nothing.
- **DemoApp (C++), demo code following the recipe:** the scene starts with the driver character seated in the car; F gets out (unparented beside the driver's door, cleared with `Depenetrate`, re-enabled; the chase camera follows it, and it walks with a small copy of the character scene's steering: WASD relative to the camera, Shift to run, Space to jump, no crouch); F within 3 m of any vehicle gets in and makes it the driven one; Tab moves the driver. While nobody is driving, every vehicle gets handbrake 1. T teleports the character when on foot.
- **Flipped:** a vehicle is marked "flipped" in its readout once `wheelsOnGround` has been 0 for 2 s with its up more than 60° from world up. R rights the driven vehicle (a `Transform` write upright, lifted 1 m, velocities zeroed); K kicks it over with a roll torque.
- **Autopilot:** P gets the driver back into the car first. The smoke run presses K, R, F, F and T once each, in that order, after its key taps.

**Blocked by:** 03 (Wheel settings, probe and tilt limit); 10 (Ground, grip and other bodies).

**Status:** ready-for-agent

- [ ] `FenrirPhysicsTests`: a disabled character parented under a chassis follows it and is not seen by the chassis, the probes or queries; re-enabled beside the chassis after `Depenetrate` it overlaps nothing.
- [ ] The doc comments carry both recipes and the tilt note.
- [ ] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs the autopilot and keys, including K, R, F, F and T, with no errors, in Debug and Release.
- [ ] F gets out beside the door, not inside anything, and the character walks; F near the tank gets in and drives it; K flips the car, "flipped" shows, and R rights it; with 7 on, K can't roll it past 60° (checked by hand).
