# What does Jolt offer for vehicles?

Type: research
Status: resolved
Blocked by: none

## Question

What do JoltPhysics v5.6.0's vehicles offer to, and require of, FenrirPhysics, in the areas where those facts constrain the vehicles spec? Answer from Jolt's own source, docs and samples (the submodule at `packages/FenrirPhysics/libs/JoltPhysics`, `Jolt/Physics/Vehicle/` and `Samples/Tests/Vehicle/`), pinned to v5.6.0:

- **The constraint:** what `VehicleConstraint` is (a constraint on one body, a step listener, or both); how it is added to and removed from a `PhysicsSystem`; whether it needs anything beyond a dynamic body (mass, centre of mass, inertia, shape); what happens to it when its body sleeps, is removed, changes shape or motion type, or is teleported.
- **The three controllers:** `WheeledVehicleController`, `MotorcycleController` (and its lean) and `TrackedVehicleController`; what each adds, and which settings each shares with the others.
- **Wheels:** `WheelSettings` and its wheeled and tracked variants: position, suspension direction, steering axis, up and forward, radius, width, suspension length, spring and damping, max steer angle, brake and handbrake torque, tyre friction curves; which are in body-local space; which can change on a live vehicle and which need it rebuilt.
- **Drivetrain:** `VehicleEngine` (torque curve, RPM range, inertia), `VehicleTransmission` (auto and manual, gear ratios, shift points, clutch), `VehicleDifferential`s, `VehicleAntiRollBar`s, and the tracked drivetrain; which are live.
- **Driver input:** how throttle, steering, brake and handbrake (and a tracked vehicle's left and right) are set, and when they must be set relative to the step.
- **Ground contact:** the `VehicleCollisionTester` kinds (ray, sphere, cylinder), the object layer and filters they take, how they map onto FenrirPhysics's private object layers and `CollisionFilter`'s layer and mask; what a wheel reports about its contact (body, sub-shape, point, normal, suspension length, slip, longitudinal and lateral friction); how per-surface friction is set (`TireMaxImpulseCallback`, friction from the ground's material).
- **Wheels in the world:** how a wheel's world pose (spin, steer, suspension) is read for drawing (`GetWheelWorldTransform`), and when it is valid.
- **Other bodies:** what a wheel's contact does to the body under it (pushing, standing on a dynamic or kinematic body, being driven over); the max pitch and roll limits; whether the vehicle body itself collides as any body.
- **Threading and timing:** where in `PhysicsSystem::Update` a vehicle runs (before or during the step, on which threads), what the host may call on a vehicle and when, and the collision-step count.
- **Determinism:** what the result depends on (order of vehicles, contact order, any process-global state).
- **Debug drawing:** what `VehicleConstraint::DrawConstraint` shows, since FenrirPhysics draws its own debug lines and has Jolt's debug renderer off.
- **What the samples do:** the car, motorcycle and tank samples' default settings and the input code driving them, as a starting point for FenrirPhysics's defaults.

Put the findings on a `research/jolt-vehicles` branch, in `.scratch/fenrir-physics/research/jolt-vehicles.md`, and link them here.

## Answer

Findings: branch `research/jolt-vehicles`, `.scratch/fenrir-physics/research/jolt-vehicles.md` (commit c891ac5), read against Jolt v5.6.0 (`e77f175`), every claim cited to file and line.

- **The constraint:**
  - `VehicleConstraint` is both a one-body constraint and a step listener: the host adds it with `AddConstraint` and `AddStepListener`, and removes both before destroying the body; without the listener it never acts.
  - It needs a dynamic body, reading its mass and inertia every step, and a collision tester set before the first step (there is no default).
  - It disables itself while its body is out of the broad phase and ignores shape changes. Setting input does not wake the body; the host wakes it.
  - Registering any vehicle stops `PhysicsSystem::Update` from skipping a world where everything sleeps (the step-listener doc says otherwise; the code wins).
- **Controllers:** wheeled, motorcycle (derived from wheeled, adds a lean spring, marked "still in development") and tracked (steers by left and right track speed ratios). The controller kind fixes the wheel-settings kind; changing either needs a rebuild.
- **Wheels:**
  - All wheel vectors are in body space, from the body origin, not the centre of mass.
  - `WheelSettings` has no setters; Jolt reads the caller's settings by reference each step, so editing works but is undocumented, and settings may be shared between vehicles.
  - Wheel count, wheel kind and the vehicle's up and forward axes need a rebuild; steer, spin and rotation angle are live state.
- **Drivetrain:** engine, transmission, differentials, limited-slip ratios, anti-roll bars, tracks and the motorcycle's lean gains are live; the motorcycle's max lean angle needs a rebuild. A manual gearbox is `VehicleTransmission::Set(gear, clutch)`. The torque curve's X axis is `RPM / maxRPM`, not the min-to-max range its doc describes.
- **Input:** wheeled and motorcycle take forward, right, brake and handbrake; tracked takes forward, left ratio, right ratio and brake (ratios non-zero). Plain stores read on every collision step of the next `Update`, so input is set before the step. Steering jumps straight to the input; positive right steers right; brake-before-reverse is the samples' code, not Jolt's.
- **Ground contact:**
  - Three probes (ray, sphere, cylinder), each taking one object layer (default two-sided simulation filters); broad-phase, object-layer and body filters can be overridden and are held as raw pointers. FenrirPhysics's layer registry fits either the body's own layer or its existing query filters.
  - Probes skip triggers and raise no contact events.
  - Per-surface friction comes from the ground body's friction or a combine callback given the sub-shape; wheeled and motorcycle also take a tyre max-impulse callback.
- **Wheel poses:** `GetWheelWorldTransform` combines the body's current pose with wheel state from the start of the last collision step: exact before `Update`, one collision step stale after. Wheels are not bodies, so their poses need their own write-back.
- **Other bodies:** wheels push and wake dynamic bodies under them and ride moving ground through tyre friction; nothing collides with a wheel, only the hull. The max pitch/roll limit is a live cone around world up. Resetting a gravity override sets the body's gravity factor to 1, not its old value.
- **Threading:** `OnStep` runs inside the step (after FenrirPhysics's step 3b), once per collision step, on Jolt job threads, in parallel across vehicles, with all bodies locked; host callbacks, filters and the friction combine run there and must not use the locking interfaces. Probe frequency can be lowered per vehicle, separately awake and asleep.
- **Determinism:** on one machine and build, results depend on body IDs (which phase the probes), constraint priority and index, `dt` variation and exact ties between probe hits; no process-global state. Vehicle state is in `SaveState` except the steer angle and the motorcycle's lean integral.
- **Debug drawing:** with Jolt's debug renderer off, `DrawConstraint` and the controllers' drawing are not compiled; everything they draw is readable through public getters (suspension, wheel basis, contacts, RPM, gear, slips).
- **Samples' defaults:** car 1500 kg, front-wheel drive, wheel radius 0.3 m, suspension 0.3–0.5 m at 1.5 Hz, steer 30°, roll limit 60°, cylinder probe (with the longitudinal tyre cap ×10 to keep tuning made under an old bug); motorcycle 240 kg, caster 30°, 150 Nm at 1000–10000 RPM, six gears, steering ramped at 4/s; tank 4000 kg, nine wheels a side, ray probe, inner track at 0.6 to turn, pivot turns below 1 m/s.
- **Other surprises:** the cylinder probe has no slope limit; ray and sphere probes use a fixed world up given at construction; `MotorcycleController::GetSettings` always reports the default lean damping; `VehicleEngineSettings`'s binary save leaves out inertia and damping.
