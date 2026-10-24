# Which settings does each vehicle kind expose?

Type: grilling
Status: resolved
Blocked by: 40, 42

## Question

The vehicles spec exposes the full drivetrain as plain data with defaults that drive out of the box. What does each kind (wheeled, motorcycle, tracked) expose, in which units, and which fields are live? Builds on [What does Jolt offer for vehicles?](40-what-jolt-offers-for-vehicles.md) and [How is a vehicle modelled in the ECS?](42-vehicles-in-the-ecs.md), and follows "Which joint kinds are there, and what does each expose?" (SI units and radians, shared setting structs, every field a live edit, solver tuning refused).

- **Wheels:** radius, width, suspension (length, spring, damping, preload), steering (max angle, which wheels steer), brake and handbrake torque, tyre friction (longitudinal and lateral curves, or a simpler grip value), and the cast shape that finds the ground (ray, sphere, cylinder).
- **Engine and transmission:** torque curve, RPM range, inertia; automatic and manual gearbox, ratios, shift points, clutch; how game code shifts gears.
- **Differentials and anti-roll bars:** how they name the wheels they join, given the answer on where wheels live.
- **Motorcycle:** lean (angle, spring, smoothing), and keeping upright at rest.
- **Tracked:** tracks, their wheels, the tracked drivetrain.
- **Shared structs** across kinds (a `VehicleEngine`, a `VehicleTransmission`), mirroring `JointSpring`, `JointLimits` and `JointMotor`.
- **Live or rebuild:** which edits Jolt takes on a live vehicle, and which FenrirPhysics hides by rebuilding it privately.
- **Defaults:** the values a newly added vehicle of each kind drives with (Jolt's samples are a starting point).
- **Limits:** max pitch and roll, and anything else the vehicle enforces on its body.
- **From "How is a vehicle modelled in the ECS?" (ADR-0015):** settings go in `VehicleKind`'s alternatives (drivetrain) and on `VehicleWheel` (one wheel type for every kind, fields a kind doesn't use ignored); differentials and anti-roll bars name wheel entities; per-wheel axes (suspension direction, steering axis) default to the chassis convention (+Y up, −Z forward); decide exactly which point `VehicleWheel::position` names (suspension top or wheel centre at rest); wheel dimensions are absolute metres, never scaled.
- **Refused as tuning:** what stays private (ADR-0010: no backend tuning settings).

Facts from "What does Jolt offer for vehicles?": engine, transmission, differentials, limited-slip ratios, anti-roll bars, tracks and the motorcycle's lean gains are live; the motorcycle's max lean angle needs a rebuild, and Jolt marks the motorcycle "still in development". `WheelSettings` has no setters (Jolt reads the settings by reference each step, which is undocumented). A manual gearbox is set as a gear and a clutch. The torque curve's X axis is `RPM / maxRPM`. Probes are ray, sphere or cylinder (the cylinder has no slope limit). The max pitch/roll limit is a live cone around world up. Probe frequency can be lowered per vehicle. Samples: car 1500 kg, front-wheel drive, 0.3 m wheels, suspension 0.3–0.5 m at 1.5 Hz, 30° steer, cylinder probe, with a ×10 longitudinal tyre cap kept from an old bug; motorcycle 240 kg, 30° caster, 150 Nm at 1000–10000 RPM, six gears; tank 4000 kg, nine wheels a side, ray probe.

From "How do other engines expose vehicles?": only PhysX, Unreal and Unity's ECS package expose a drivetrain; Unity ECS has the clearest drive-out-of-the-box defaults (engine starts on awake, automatic six-speed). No engine keeps a motorcycle upright, only PhysX has a tank drive, and many-wheeled tanks without tracks slide. Tuning being hard is a common complaint in Unity and Unreal.

## Answer

No ADR. Every call is either additive to change later (curves, gear lists, axis angles, kingpin can all be added beside what is here) or follows a precedent already recorded (ADR-0010 for refusing tuning, ADR-0015 for wheels and links, the joints rules for units, live edits and out-of-range values). Glossary: **drivetrain**, **differential**, **anti-roll bar**, **suspension travel**; **driver input** gains the manual gear and clutch.

- **No arrays in any vehicle component.** Every list Jolt takes (torque curve, tyre curves, gear ratios, differentials, anti-roll bars) becomes named shape fields or its own component, so FenrirScripting needs no fixed-buffer mapping. A free-form curve can come later as a cooked handle like `CollisionMesh`, if anyone needs one.
- **Units:** SI and radians, as for joints (metres, newton-metres, kg m², N/m, Hz and damping ratio). The one exception is engine speeds (min, max, peak torque, shift points), which are in RPM. Tyre slip angles are radians, converted privately to Jolt's degrees.
- **`VehicleWheel::position`** is the wheel centre with the suspension fully extended: where the wheel hangs with the chassis lifted, and where the wheel entity sits before the first step. Physics puts Jolt's hard point `suspension.travel` above it, along the suspension direction, with min length 0. A model authored at ride height sinks onto its springs by the sag.
- **Per-wheel axes** are angles, all defaulting to 0 (the chassis convention: suspension straight down, steering about +Y, wheel facing −Z):
  - `caster` tilts the suspension and steering axis back (the motorcycle's front fork);
  - `camber` tilts the wheel's top in or out;
  - `toe` turns the wheel's front in or out.

  Each one is signed to mean the same on both sides, mirrored by the sign of `position.x`; at x = 0 nothing is mirrored. Kingpin and sideways suspension tilt are left out, addable later.
- **`VehicleWheel`** (beside `vehicle`, `position` and `track` from ADR-0015), with defaults from Jolt:
  - `radius = 0.3`, `width = 0.1`, `inertia = 0.9`, `angularDamping = 0.2`; `caster`, `camber`, `toe` = 0.
  - `VehicleSuspension suspension { float travel = 0.2; JointSpring spring = {1.5, 0.5}; float preload = 0; }`. `travel` is how far the wheel can rise from full extension. `travel` 0 or a spring frequency of 0 makes the suspension rigid (Jolt's min == max). The spring reuses `JointSpring` rather than adding a twin type; stiffness mode is not offered.
  - `maxSteerAngle = 0`: a wheel steers only when given an angle. There is no "front wheels steer" default.
  - `maxBrakeTorque = 1500`, `maxHandbrakeTorque = 4000`.
  - `VehicleTyre longitudinal = {0.06, 1.2, 0.2, 1.0}` and `lateral = {0.052, 1.2, 0.35, 1.0}`, each `{ peakSlip, peakFriction, slideSlip, slideFriction }`: friction rises from 0 to its peak, falls to the slide value, and stays there. Longitudinal slip is a ratio, lateral slip an angle in radians. Physics builds Jolt's curves from them. Jolt's car-sample ×10 longitudinal cap is not carried over; if the spike finds the defaults weak, the default friction goes up instead.
  - Tracked vehicles ignore the tyre, brake and steer fields.
- **Drivetrain structs**, shared by every kind:
  - `VehicleEngine { maxTorque = 500; minRPM = 1000; maxRPM = 6000; peakTorqueRPM = 4000; torqueAtMinRPM = 0.8; torqueAtMaxRPM = 0.8; inertia = 0.5; angularDamping = 0.2; }`. The two fractions are of `maxTorque`; physics builds Jolt's `RPM / maxRPM` curve from them. The engine never stalls.
  - `VehicleTransmission { mode = Automatic; gearCount = 5; firstGear = 2.66; topGear = 0.74; reverseGear = 2.90; shiftUpRPM = 4000; shiftDownRPM = 2000; switchTime = 0.5; clutchReleaseTime = 0.3; switchLatency = 0.5; clutchStrength = 10; }`. Forward ratios are spaced geometrically from first to top; there is one reverse gear, given as a positive magnitude. The shift and switch fields are read only in Automatic.
- **Kinds:**
  - `WheeledVehicle { VehicleEngine engine; VehicleTransmission transmission; float limitedSlipRatio = 1.4; }`. The limited-slip ratio is the one *between* differentials.
  - `MotorcycleVehicle { engine; transmission; limitedSlipRatio; MotorcycleLean lean; }`, starting from Jolt's motorcycle defaults: 150 Nm, 1000–10000 RPM, six gears from 2.27 to 0.88, reverse 4, shift 2000/8000, clutch strength 2.
    - `MotorcycleLean { bool enabled = true; float maxAngle = π/4; bool limitSteering = true; }`. Disabled, the bike falls over. `limitSteering` caps steering by speed so the lean stays under `maxAngle`.
    - Jolt's lean spring, damping, integration, decay and frame-rate-dependent smoothing are raw controller gains: private at Jolt's defaults, refused as tuning.
    - Staying upright at rest is the lean spring's job; the spike checks it. The spec says the backend marks motorcycles as still in development.
  - `TrackedVehicle { engine; transmission; VehicleTrack left, right; }`, starting from Jolt's tracked defaults (500–4000 RPM, four gears, reverse 4, shift 1000/3500).
    - `VehicleTrack { inertia = 10; angularDamping = 0.5; maxBrakeTorque = 15000; differentialRatio = 6; longitudinalFriction = 4; lateralFriction = 2; }`. Grip is per track, because one wheel type can't hold kind-dependent defaults.
    - Jolt's driven wheel (whose radius sets track speed) is chosen privately: the rearmost wheel on each track.
- **Differentials and anti-roll bars** are their own components, on any entity (an axle entity holding one of each, a wheel entity, anything):
  - `VehicleDifferential { Entity leftWheel, rightWheel; float ratio = 3.42; float leftRightSplit = 0.5; float limitedSlipRatio = 1.4; float torqueShare = 1; }`. An empty wheel is "none", so the motorcycle's rear-only drive is one differential with one wheel. Each vehicle's torque shares are normalised privately, so they never have to sum to 1. A wheel in no differential only rolls and brakes.
  - `VehicleAntiRollBar { Entity leftWheel, rightWheel; float stiffness = 1000; }` (N/m).
  - Both wheels must be active wheels of the same vehicle; otherwise the component is inactive (kept, one warning, back once valid), as joints are. Tracked vehicles ignore differentials.
  - **Drives out of the box:** a wheeled or motorcycle vehicle with no differential drives every wheel equally (privately one single-wheel differential per wheel). Adding any differential replaces that default. There are no anti-roll bars by default.
- **Gears:** `VehicleInput` gains `int gear` (0 neutral, 1…n forward, −1 reverse) and `float clutch`, a pedal in [0, 1] where 0 is engaged, so zero-initialised input drives. Both are read only in Manual, applied at once (Jolt's switch time is Automatic's), with the clutch left to game code. An out-of-range gear is clamped. Physics never writes them; the current gear is read back through "How does a vehicle step each tick, and what does game code read back?".
- **Track input:** `leftTrack` and `rightTrack` in `VehicleInput` default to 1, so a fresh tracked vehicle drives straight. Jolt refuses 0, so physics privately replaces 0 with a tiny ratio: that track holds still.
- **Ground probe:** `VehicleProbe probe` on `Vehicle` (`Ray`, `Sphere`, `Cylinder`), live, shared by every kind. Wheeled and motorcycle default to `Cylinder`, tracked to `Ray`, from each kind's defaults. The cylinder's rounding is private (0.1 for wheeled, 1.0 for a motorcycle's round tyre). Ray and sphere use the world's −gravity as up with a fixed 80° slope limit.
- **Tilt limit:** `float maxTilt = π` on `Vehicle`, in radians: the live cone around world up past which the chassis can't pitch or roll. The default π means off. Flipped vehicles stay with "How does a vehicle meet other bodies, joints and characters?".
- **Live edits:** every settings field is a live edit applied on the next tick, as for joints. Privately, the drivetrain, anti-roll bars, tracks, probe and tilt limit go through Jolt's writable references. Wheel settings are each vehicle's own unshared copy, patched in place and validated by physics, because Jolt's construction asserts don't re-run. The only private rebuild is changing `MotorcycleLean::maxAngle`, which carries over wheel spin, engine RPM and gear. The spike verifies the in-place wheel patching.
- **Out-of-range values,** one warning per vehicle, wheel or component, as for joints:
  - negative torques, inertias, frictions, stiffnesses, travel and widths are treated as 0, and radius is raised to a small minimum;
  - `maxSteerAngle`, `caster`, `camber` and `toe` are clamped below π/2;
  - `minRPM < maxRPM` is enforced, `peakTorqueRPM` and the shift points are clamped into the range, and shift-down is kept below shift-up;
  - `gearCount ≥ 1`, and limited-slip ratios at or below 1 are raised to just above it.

  Driver input is clamped to its ranges silently, as joint position targets are.
- **Refused as tuning (ADR-0010):** probe frequency, the suspension force point, the cylinder's rounding, the tyre max-impulse callback, the lean gains, the per-vehicle gravity override (gravity is per world), draw sizes and the spring stiffness mode. The friction combine with the ground is "How does a vehicle step each tick, and what does game code read back?"'s (per-surface grip).
- **Handed on:**
  - "How does a vehicle step each tick, and what does game code read back?": the current gear and RPM in the read-back; how a wheel's tyre friction (or a track's) combines with the ground's `PhysicsMaterial`.
  - "How does a vehicle meet other bodies, joints and characters?": flipped vehicles with `maxTilt` off by default; differential and anti-roll-bar components whose wheels go away mid-drive.
  - "Spike: a car, a motorcycle and a tank driving a small track through the proposed model": in-place wheel patching, a motorcycle standing at rest, the default tyres driving without the ×10 cap, and the all-wheel-drive default.
  - "What does C# get for vehicles?": no arrays to map; two more components (`VehicleDifferential`, `VehicleAntiRollBar`).
