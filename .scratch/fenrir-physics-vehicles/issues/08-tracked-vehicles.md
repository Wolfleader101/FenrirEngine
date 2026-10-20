# 08: Tracked vehicles

**What to build:** A tracked vehicle is driven by each track's speed ratio and pivots on the spot (spec: "FenrirPhysics: the components" (`VehicleInput` track ratios, `VehicleTrackSide`), "settings and defaults" (kinds: `TrackedVehicle`, `VehicleTrack`; drives out of the box), "state" (track speeds), "the step in `PostTick`" (determinism), "the Jolt backend" (input, tracks)).

- **`TrackedVehicle`** joins `VehicleKind` with the spec's engine and transmission defaults and `VehicleTrack left, right` (`inertia`, `angularDamping`, `maxBrakeTorque`, `differentialRatio`, `longitudinalFriction`, `lateralFriction`), all live.
- **Wheels:** each `VehicleWheel::track` puts it on the left or right track; tracked vehicles ignore the tyre, brake and steer fields and ignore differentials. Jolt's driven wheel on each track is the rearmost wheel of that side.
- **Input:** `forward`, `leftTrack`, `rightTrack` (each [−1, 1], default 1) and `brake`; `steer` and `handbrake` ignored. A 0 ratio holds the track still, replaced privately by a tiny one because Jolt refuses 0.
- **Grip:** a track's `longitudinalFriction`/`lateralFriction` combine with the ground's material as tyres do (ticket 10 proves the combine).
- **State:** `leftTrackSpeed` and `rightTrackSpeed` (m/s); tracked wheels have no slip.
- **Determinism, all kinds:** a car, a motorcycle and a tank over bumps and crates stay bit-identical over 600 ticks in two runs, single- and multi-threaded (the spike's 16 200 poses).
- **DemoApp (C++):** the spike's 4000 kg tank with nine wheels a side; Tab cycles car / motorcycle / tank; A/D pivot it when stopped (the demo turns steer into track ratios). The readout adds its two track speeds.

**Blocked by:** 05 (Teleports, rebuilds and wheels lost mid-drive); 07 (Motorcycles).

**Status:** ready-for-agent

- [ ] `FenrirPhysicsTests`: the tank drives forward; on `leftTrack = −1, rightTrack = 1` it pivots, with opposite track speeds read back; a 0 ratio holds its track still; `steer`, `handbrake`, the tyre fields and differentials are ignored.
- [ ] A `VehicleTrack` edit is live and wakes the chassis; changing kind to and from tracked keeps RPM, gear and spin.
- [ ] The car, motorcycle and tank are bit-identical over 600 ticks in two runs, single- and multi-threaded.
- [ ] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] The tank pivots on the spot; Tab cycles all three vehicles (checked by hand).
