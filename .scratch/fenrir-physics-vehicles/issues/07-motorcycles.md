# 07: Motorcycles

**What to build:** A motorcycle stands at rest and leans into turns, with a switch to let it fall (spec: "FenrirPhysics: settings and defaults" (kinds: `MotorcycleVehicle`, `MotorcycleLean`; live edits), "state" (`leanAngle`), "the step in `PostTick`" (rebuilds), "the Jolt backend" (motorcycle lean damping)).

- **`MotorcycleVehicle`** joins `VehicleKind` with the spec's engine, transmission and `limitedSlipRatio` defaults and `MotorcycleLean { enabled, maxAngle, limitSteering }`. Its doc comment says the backend marks motorcycles as still in development, and that a motorcycle drives both wheels unless given a rear-only `VehicleDifferential`.
- **Backend:** the motorcycle controller. **The lean damping cap** (spike lesson 2): Jolt's lean damping capped privately at half the chassis's roll inertia ÷ the collision-step time, Jolt's spring of 5000 kept. Lean gains stay private.
- **Live:** `lean.enabled` and `limitSteering` live; `maxAngle` a private rebuild carrying over RPM, gear and spin through ticket 05's carry-over.
- **Changing kind** (wheeled ⇄ motorcycle) is an assignment: a rebuild with the same carry-over.
- **State:** `leanAngle` (rad, + right), 0 for other kinds.
- **DemoApp (C++):** the spike's 240 kg motorcycle with a rear-only differential; Tab switches car ⇄ motorcycle (the driven vehicle; ticket 08 adds the tank to the cycle); 3 changes the max lean (a rebuild keeping RPM and gear); 6 toggles the lean controller. The readout adds its lean.

**Blocked by:** 04 (Differentials and anti-roll bars); 05 (Teleports, rebuilds and wheels lost mid-drive).

**Status:** resolved

- [x] `FenrirPhysicsTests`: the default motorcycle stands at rest; under full steer at 60 km/h it leans about 23°; `limitSteering` caps steer by speed so the lean stays under `maxAngle`; with `lean.enabled = false` it falls over.
- [x] A light bike (the sample's box, roll inertia 10.4 kg m²) landing from a jump stays stable at 60 Hz (the damping cap).
- [x] Changing `maxAngle` and changing kind wheeled ⇄ motorcycle keep RPM, gear and surviving spin.
- [x] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release (the motorcycle's run is `"Motorcycle (C++)"`).
- [ ] The motorcycle stands at rest and leans into turns; with 6 off it falls over; 3 changes the max lean without losing RPM or gear (checked by hand).

**Resolved by:** PR #60 (branch claude/intelligent-satoshi-988dd7), stacked on #59 (ticket 11, on #58, #57 and #56). The proof is `FenrirPhysicsTests`' `[motorcycles]` cases (`Tests/VehicleMotorcycles.cpp`), the `VehicleKindWrongViewAsserts` test, and the `FENRIR_DEMO_SCENE="Motorcycle (C++)" FENRIR_DEMO_KEYS=1` smoke run. That run rides the bike round its loop on the autopilot, taps 3 twice on the way, then stops, taps 6 twice (it falls over) and T. The last box (watching the bike by hand) is left for a person. Findings and choices:
- **The DemoApp motorcycle has a scene of its own,** `Motorcycle (C++)` (`src/MotorcycleScene.hpp`), second in the F6 family after `Vehicles (C++)`, instead of sitting beside the car behind Tab. The user asked for this so its smoke run doesn't wait for the car's 5-minute lap. Its keys: 3 (max lean, 45° ⇄ 20°, a rebuild), 6 (lean controller), T (stand it up at the start), 5 (interpolation), P (autopilot round a loop with a hairpin at each end), C, L, and the driving keys. Tab is unused, so ticket 08's "Tab cycles car, motorcycle and tank" needs rethinking against this.
- Jolt's sample bike leans 22.7° steady at 60 km/h under full steer. `limitSteering` holds its fork to about 4° there. With it off, the fork turns the full 30° and the bike weaves.
- The lean-damping cap is applied in every `World::Step`, before Jolt's update, from that step's collision-step time and the chassis's roll inertia. That covers any tick rate and any mass edit without a rebuild. Without the cap, the sample bike diverges even at rest (47 rad/s); with it, it lands from a 4 m drop and stands up again.
- A motorcycle's cylinder probe is fully rounded (a convex-radius fraction of 1, as in Jolt's sample), so a leaning wheel rolls on its edge. Like the lean gains, this stays private.
- `maxAngle` is clamped silently to between 0 and just under a quarter turn, as the tilt limit is, because Jolt takes its tangent.
- A kind change and a `maxAngle` change take step 2's rebuild path, using ticket 05's carry-over; `lean.enabled` and `limitSteering` go through `UpdateVehicle` as live edits.
