# 09: Trailers and towing

**What to build:** A trailer, wagon or hand cart is a vehicle with no engine, towed behind any vehicle by an ordinary joint (spec: "FenrirPhysics: settings and defaults" (kinds: `TrailerVehicle`), "trailers, getting in and out, flipping (recipes)" (towing), "the Jolt backend" (a trailer is the wheeled controller with no differentials)).

- **`TrailerVehicle {}`** joins `VehicleKind`: no engine and no gearbox. It reads `brake`, `handbrake` and `steer` (for a steered dolly) and ignores `forward`, gear and clutch, and ignores differentials. Its `state` engine and gear fields stay 0. Changing kind to and from trailer is a rebuild with ticket 05's carry-over.
- **Towing needs no vehicle-specific joint rules:** a trailer is any dynamic body joined to the chassis by an ordinary `Joint`, usually `ball` at the hitch; with `collideConnected = false` the hitch doesn't fight the chassis, and the joint links the islands so they wake together. A wheeled vehicle given no input also tows, in neutral (spike lesson 9). Trailer brakes are game code copying the tractor's `brake` and `handbrake`; there is no automatic link.
- **Docs:** the towing recipe and the stated limit (a trailer much heavier than its tractor jitters at the hitch) go in `Vehicle`'s doc comment.
- **DemoApp (C++):** the car tows the spike's 300 kg `TrailerVehicle` on a ball `Joint`; the trailer's `brake` and `handbrake` are copied from the car's every tick. A trailer's readout shows only speed and wheels on ground. The autopilot's ramp leg takes the trailer over it.

**Blocked by:** 05 (Teleports, rebuilds and wheels lost mid-drive).

**Status:** resolved

- [x] `FenrirPhysicsTests`: a trailer reads `brake` and `steer` and ignores `forward`; its engine and gear state stay 0; it ignores a differential naming its wheels.
- [x] A trailer on a ball joint is towed by the car and wakes with it; a wheeled vehicle with no input tows in neutral.
- [x] Changing kind wheeled ⇄ trailer reaches physics and keeps surviving spin.
- [x] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] The trailer is towed over the ramp and brakes with the car (checked by hand).

**Resolved by:** PR #PRNUM (branch claude/optimistic-austin-69234f), into dotnet-scripting. The proof is `FenrirPhysicsTests`' `[trailers]` cases (`Tests/VehicleTrailers.cpp`) and the `FENRIR_DEMO_SCENE="Vehicles (C++)" FENRIR_DEMO_KEYS=1` smoke run, whose autopilot lap now tows the trailer over the ramp and the rest of the track. The last box (watching the trailer by hand) is left for a person. Findings and choices:
- **A trailer is the wheeled controller held in neutral, not one with no differentials.** Jolt asserts (in builds with asserts) that its driven differentials' torque shares sum to 1, so a controller with no differential driving a wheel trips it every step. The trailer keeps the default one-per-wheel differentials, ignores game code's, and is held in Manual neutral; at a gear ratio of 0 Jolt's clutch carries nothing, so no wheel is driven. Its state's engine, gear, clutch and shifting fields are read as 0.
- `VehicleType::Trailer` follows `Motorcycle` for now; ticket 08 adds `Tracked` before it, to the spec's order.
- A trailer turned into another kind starts at idle in neutral (it had no engine speed or gear to carry), keeping its wheels' spin; the other way, the wheels' spin is kept and the drivetrain dropped.
- The DemoApp trailer keeps the default handbrake torque, so copying the car's `handbrake` holds it on the parking slope too. T puts it back behind the car; its name shows only its speed and wheels on ground, and L logs it.
