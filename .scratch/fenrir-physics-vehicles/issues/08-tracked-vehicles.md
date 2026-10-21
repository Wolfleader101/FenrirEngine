# 08: Tracked vehicles

**What to build:** A tracked vehicle is driven by each track's speed ratio and pivots on the spot (spec: "FenrirPhysics: the components" (`VehicleInput` track ratios, `VehicleTrackSide`), "settings and defaults" (kinds: `TrackedVehicle`, `VehicleTrack`; drives out of the box), "state" (track speeds), "the step in `PostTick`" (determinism), "the Jolt backend" (input, tracks)).

- **`TrackedVehicle`** joins `VehicleKind` with the spec's engine and transmission defaults and `VehicleTrack left, right` (`inertia`, `angularDamping`, `maxBrakeTorque`, `differentialRatio`, `longitudinalFriction`, `lateralFriction`), all live.
- **Wheels:** each `VehicleWheel::track` puts it on the left or right track; tracked vehicles ignore the tyre, brake and steer fields and ignore differentials. Jolt's driven wheel on each track is the rearmost wheel of that side.
- **Input:** `forward`, `leftTrack`, `rightTrack` (each [−1, 1], default 1) and `brake`; `steer` and `handbrake` ignored. A 0 ratio holds the track still, replaced privately by a tiny one because Jolt refuses 0.
- **Grip:** a track's `longitudinalFriction`/`lateralFriction` combine with the ground's material as tyres do (ticket 10 proves the combine).
- **State:** `leftTrackSpeed` and `rightTrackSpeed` (m/s); tracked wheels have no slip.
- **Determinism, all kinds:** a car, a motorcycle and a tank over bumps and crates stay bit-identical over 600 ticks in two runs, single- and multi-threaded (the spike's 16 200 poses).
- **DemoApp (C++):** a **Tank (C++)** scene of its own (`src/TankScene.hpp`), as ticket 07 gave the motorcycle its Motorcycle (C++) scene: registered in `main.cpp`, listed in the DemoApp README, and placed in the F6 family through `DemoScenes`. It holds the spike's 4000 kg tank with nine wheels a side; A/D pivot it when stopped (the demo turns steer into track ratios). The readout adds its two track speeds. It does not Tab-cycle in the car's Vehicles (C++) scene, and `VehicleScene.hpp` is edited as little as possible (ticket 09 edits it at the same time).

**Blocked by:** 05 (Teleports, rebuilds and wheels lost mid-drive); 07 (Motorcycles).

**Status:** resolved

- [x] `FenrirPhysicsTests`: the tank drives forward; on `leftTrack = −1, rightTrack = 1` it pivots, with opposite track speeds read back; a 0 ratio holds its track still; `steer`, `handbrake`, the tyre fields and differentials are ignored.
- [x] A `VehicleTrack` edit is live and wakes the chassis; changing kind to and from tracked keeps RPM, gear and spin.
- [x] The car, motorcycle and tank are bit-identical over 600 ticks in two runs, single- and multi-threaded.
- [x] `FENRIR_DEMO_SCENE="Tank (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] In Tank (C++), the tank pivots on the spot on A/D when stopped, and the readout shows both track speeds (checked by hand).

**Resolved by:** PR #62 (branch claude/interesting-brattain-03be97), merged into dotnet-scripting. The proof is `FenrirPhysicsTests`' `[tracked]` cases (`Tests/VehicleTracks.cpp`), the three-vehicle determinism case in `Tests/Vehicles.cpp`, and the `FENRIR_DEMO_SCENE="Tank (C++)" FENRIR_DEMO_KEYS=1` smoke run. That run drives the tank round its loop on the autopilot, pivoting on the spot at each end, taps 3 twice on the way (the tracks' grip), then stops and taps T, C, Space and 5. The last box (watching the tank by hand) is left for a person. Findings and choices:
- **The DemoApp tank has a scene of its own,** `Tank (C++)` (`src/TankScene.hpp`), third in the F6 family after `Vehicles (C++)` and `Motorcycle (C++)`, as the user decided. `VehicleScene.hpp` is unchanged: the scene reuses its `VehicleCamera`. Its keys: 3 (both tracks' grip, 4 ⇄ 1, a live edit), 5 (interpolation), T (back to the start), P (autopilot, pivoting at each end), C, L, Space (brake), and the driving keys. It has a row of 20 kg crates to push aside.
- **Pivoting needs the throttle.** Jolt drives the tracks only with `forward` above 0, so the demo's pivot is `forward = |steer|` with the inner track at −1. A `forward` of 0 on opposite ratios does nothing.
- **The spike's end wheels never touch flat ground:** they sit 0.2 m higher and are rigid, so the default tank has 14 of its 18 wheels on the ground at rest.
- **A tracked vehicle's engine is locked to its tracks** from its first step: Jolt sets its RPM from the fastest track, with no clutch slip. So a car turned tracked loses its clutch slip on the first tick (about 5% of RPM at part throttle). Gear and wheel spin carry over exactly, and the tests match the differential ratios so RPM carries too.
- Every wheel of a tracked vehicle sits on one track, by `VehicleWheel::track`; Jolt needs that. Each track's driven wheel is its rearmost (the first of those level), and a track with no wheels reads 0 speed and does nothing. A wheel edit that changes its track or moves it reassigns both tracks live.
- Tracked wheels ignore the wheel's own inertia and angular damping as well as its tyre, brake and steer fields: the track's `inertia` and `angularDamping` stand for its wheels. The `TrackedVehicle` doc comment says so.
- Out-of-range tracks: a negative inertia, brake torque or friction is treated as 0 with one warning per vehicle; inertia and `differentialRatio` are kept above 0 and a negative damping is treated as 0, silently (the backend divides by the first two).
- A 0 track ratio, and anything closer to 0 than 0.001, is given to Jolt as 0.001, the spike's value. A sleeping tracked vehicle has its tracks' spin stopped with its wheels'.
