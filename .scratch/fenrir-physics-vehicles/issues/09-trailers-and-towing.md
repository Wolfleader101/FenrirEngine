# 09: Trailers and towing

**What to build:** A trailer, wagon or hand cart is a vehicle with no engine, towed behind any vehicle by an ordinary joint (spec: "FenrirPhysics: settings and defaults" (kinds: `TrailerVehicle`), "trailers, getting in and out, flipping (recipes)" (towing), "the Jolt backend" (a trailer is the wheeled controller with no differentials)).

- **`TrailerVehicle {}`** joins `VehicleKind`: no engine and no gearbox. It reads `brake`, `handbrake` and `steer` (for a steered dolly) and ignores `forward`, gear and clutch, and ignores differentials. Its `state` engine and gear fields stay 0. Changing kind to and from trailer is a rebuild with ticket 05's carry-over.
- **Towing needs no vehicle-specific joint rules:** a trailer is any dynamic body joined to the chassis by an ordinary `Joint`, usually `ball` at the hitch; with `collideConnected = false` the hitch doesn't fight the chassis, and the joint links the islands so they wake together. A wheeled vehicle given no input also tows, in neutral (spike lesson 9). Trailer brakes are game code copying the tractor's `brake` and `handbrake`; there is no automatic link.
- **Docs:** the towing recipe and the stated limit (a trailer much heavier than its tractor jitters at the hitch) go in `Vehicle`'s doc comment.
- **DemoApp (C++):** the car tows the spike's 300 kg `TrailerVehicle` on a ball `Joint`; the trailer's `brake` and `handbrake` are copied from the car's every tick. A trailer's readout shows only speed and wheels on ground. The autopilot's ramp leg takes the trailer over it.

**Blocked by:** 05 (Teleports, rebuilds and wheels lost mid-drive).

**Status:** ready-for-agent

- [ ] `FenrirPhysicsTests`: a trailer reads `brake` and `steer` and ignores `forward`; its engine and gear state stay 0; it ignores a differential naming its wheels.
- [ ] A trailer on a ball joint is towed by the car and wakes with it; a wheeled vehicle with no input tows in neutral.
- [ ] Changing kind wheeled ⇄ trailer reaches physics and keeps surviving spin.
- [ ] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] The trailer is towed over the ramp and brakes with the car (checked by hand).
