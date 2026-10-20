# Which settings does each vehicle kind expose?

Type: grilling
Status: open
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
