# What Jolt offers for vehicles

Research for [40: What does Jolt offer for vehicles?](../issues/40-what-jolt-offers-for-vehicles.md).

**Pinned source:** JoltPhysics **v5.6.0**, commit `e77f175595e64cb44218cc9d9d56fc365ad0e36a` (tag `v5.6.0`), the submodule at `packages/FenrirPhysics/libs/JoltPhysics`. Every link points at that tag. Sources are Jolt's own headers and `.cpp` files under `Jolt/Physics/Vehicle/`, the parts of `Jolt/Physics/PhysicsSystem.cpp`, `PhysicsStepListener.h` and `Constraints/` that run vehicles, `Docs/Architecture.md`, `Docs/ReleaseNotes.md`, `Samples/Tests/Vehicle/` and `UnitTests/Physics/WheeledVehicleTests.cpp`. FenrirPhysics facts come from `packages/FenrirPhysics/` at `6e0b649` (the head that carries character ticket 11).

Link shorthand: `Vehicle/` is `Jolt/Physics/Vehicle/`, `Tests/` is `Samples/Tests/Vehicle/`. Markdown line links use `?plain=1`. Statements marked *(inference)* are read off the code paths, not stated by Jolt.

---

## 1. The constraint

**What it is: both a constraint and a step listener.** `class VehicleConstraint : public Constraint, public PhysicsStepListener` ([VehicleConstraint.h L65](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L65)). It is a one-body `Constraint` (not a `TwoBodyConstraint`): it holds one `Body *mBody` and solves against `Body::sFixedToWorld` and whatever each wheel touches ([VehicleConstraint.h L218](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L218); [VehicleConstraint.cpp L416, L508-L521](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L416)). The wheels are not bodies and have no collision shape; they are probes plus constraint parts ([Wheel.h L140-L143](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/Wheel.h#L140-L143)).

- **The listener half** (`OnStep`) runs the wheel probes, anti-roll bars and the controller's drivetrain once per collision step ([VehicleConstraint.cpp L152-L326](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L152-L326)).
- **The constraint half** solves per wheel: a suspension spring, a hard stop at minimum length, longitudinal and lateral friction, plus one angular part for the pitch/roll limit ([VehicleConstraint.cpp L425-L631](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L425-L631)).

**Adding and removing.** The header says "Don't forget to register the constraint as a StepListener" ([VehicleConstraint.h L44](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L44)). Every sample does both `AddConstraint` and `AddStepListener` ([VehicleConstraintTest.cpp L170-L171](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleConstraintTest.cpp#L170-L171); [MotorcycleTest.cpp L130-L131](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/MotorcycleTest.cpp#L130-L131); [TankTest.cpp L118-L119](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/TankTest.cpp#L118-L119)), and removes the step listener in its destructor ([VehicleConstraintTest.cpp L25-L28](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleConstraintTest.cpp#L25-L28)).
- The constraint list holds `Ref`s; the step-listener list holds raw pointers, and `RemoveStepListener` swaps the last listener into the removed slot ([PhysicsSystem.cpp L115-L131](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.cpp#L115-L131)). So the listener must be removed before the last `Ref` goes *(inference)*.
- Without the listener, `mIsActive` is never set, so `IsActive()` stays false and the constraint is never solved *(inference: [VehicleConstraint.h L186, L225](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L186); [VehicleConstraint.cpp L187](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L187))*.

**Construction** (`VehicleConstraint(Body &, const VehicleConstraintSettings &)`, [VehicleConstraint.cpp L78-L104](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L78-L104)): asserts `mUp` and `mForward` are normalized and that there is at least one wheel. It builds the controller from `mController->ConstructController(*this)`, one runtime `Wheel` per `WheelSettings` through the controller, and seeds its step counter from `Hash64(body ID index)`. There is no collision tester by default: the host must call `SetVehicleCollisionTester` ([VehicleConstraint.h L80](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L80)), and `OnStep` dereferences it unchecked ([VehicleConstraint.cpp L223, L236](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L223)).

**What it needs from the body.**
- The samples all use one `Dynamic` body with its mass overridden (`CalculateInertia`, 1500 kg car, 240 kg bike, 4000 kg tank) and its centre of mass pushed down with `OffsetCenterOfMassShape` ([VehicleConstraintTest.cpp L47-L52](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleConstraintTest.cpp#L47-L52); [MotorcycleTest.cpp L62-L67](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/MotorcycleTest.cpp#L62-L67); [TankTest.cpp L71-L79](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/TankTest.cpp#L71-L79)). The docs suggest that shape for "a vehicle to improve its handling" ([Architecture.md L189](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L189)).
- Mass and inertia are read every step: the suspension's effective mass comes from `GetInverseMass()` and `GetLocalSpaceInverseInertia()` ([VehicleConstraint.cpp L446-L451](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L446-L451)), and the gravity override divides by the inverse mass ([L169-L171](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L169-L171)). A static body has no `MotionProperties`, so it would crash there; a kinematic one has zero inverse mass and inertia, giving an infinite effective mass *(inference)*. Jolt checks neither.
- The shape matters only as the body's own collision; wheels never use it.

**Sleep.**
- `OnStep` runs whether the body sleeps or not. `mIsActive` is true if the body is awake **or** any wheel touches an awake body ([VehicleConstraint.cpp L186-L187, L252](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L186-L187)). `BuildIslands` then wakes the vehicle and any dynamic body under its wheels ([L328-L379](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L328-L379)).
- While asleep, wheels are probed every `mNumStepsBetweenCollisionTestInactive` steps (default 1), whose "only purpose" is to wake the vehicle when something moves into its wheels ([VehicleConstraint.h L177-L183](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L177-L183)).
- It keeps the body awake (`ResetSleepTimer`) while the controller refuses sleep or any wheel spins faster than 10 degrees/s, but only if the body allows sleeping ([VehicleConstraint.cpp L309-L322](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L309-L322); [ReleaseNotes.md L159](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/ReleaseNotes.md?plain=1#L159)). Both drivetrains refuse sleep while there is forward input, the gearbox is shifting or the engine is above 1.01 x min RPM ([WheeledVehicleController.cpp L234-L239](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L234-L239); [VehicleEngine.h L83](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleEngine.h#L83); [VehicleTransmission.h L73](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleTransmission.h#L73)).
- **Setting input does not wake the body.** Every sample calls `ActivateBody` itself when there is input ([VehicleConstraintTest.cpp L226-L228](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleConstraintTest.cpp#L226-L228); [TankTest.cpp L267-L268](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/TankTest.cpp#L267-L268)). The unit test checks a braked car goes to sleep ([WheeledVehicleTests.cpp L194-L196](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/UnitTests/Physics/WheeledVehicleTests.cpp#L194-L196)).
- A registered step listener stops `PhysicsSystem::Update` from taking its "nothing awake" early-out ([PhysicsSystem.cpp L191](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.cpp#L191)), even though the listener header says `OnStep` "is not called if there aren't any active bodies" ([PhysicsStepListener.h L33](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsStepListener.h#L33)). The code wins: a world with one vehicle always runs the full step pipeline.

**Body removed.** "`VehicleConstraint`s are automatically disabled when the vehicle body is not in the `PhysicsSystem`" ([ReleaseNotes.md L33](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/ReleaseNotes.md?plain=1#L33)): `OnStep` returns at once and `IsActive()` is false if the body is not in the broad phase ([VehicleConstraint.cpp L156-L158](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L156-L158); [VehicleConstraint.h L186](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L186)). The wheel state is kept, so re-adding the body resumes the vehicle *(inference)*. **Destroying** the body leaves `mBody` dangling; the constraint and listener must go first *(inference)*. FenrirPhysics already removes constraints before bodies for this reason ([JoltBackend.cpp L1439-L1446](../../../packages/FenrirPhysics/src/Backend/Jolt/JoltBackend.cpp)).

**Shape change.** `NotifyShapeChanged` does nothing ([VehicleConstraint.h L187](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L187)). Wheel positions are in body space, "not COM space" ([VehicleConstraint.h L154](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L154); used through `GetWorldTransform()` at [VehicleConstraint.cpp L192, L201](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L192)). So a new shape or centre of mass moves nothing the vehicle knows about, and the new mass properties are picked up at the next step *(inference)*.

**Motion type change.** Nothing in the vehicle reacts. See "what it needs from the body" for static and kinematic.

**Teleport.** Nothing is cached in world space across steps except the last contact (position, normal, axle plane). A full probe overwrites them every step by default ([VehicleConstraint.cpp L227-L249](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L227-L249)). With more than one step between probes, the in-between steps extrapolate the old contact plane from the new pose ([L204-L225](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L204-L225)), which is wrong for a step after a teleport *(inference)*. The pre-step callback "is the last moment that the position/orientation of the vehicle can be changed" ([VehicleConstraint.h L96](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L96)). Wheel spin and engine RPM carry over a teleport unless reset (`Wheel::SetAngularVelocity`, `VehicleEngine::SetCurrentRPM`).

## 2. The three controllers

The controller is chosen by the type of `VehicleConstraintSettings::mController` and built once ([VehicleController.h L24-L37](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleController.h#L24-L37)). It decides the wheel type: `ConstructWheel` asserts the settings are `WheelSettingsWV` or `WheelSettingsTV` ([WheeledVehicleController.h L166](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.h#L166); [TrackedVehicleController.h L140](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/TrackedVehicleController.h#L140)). Each controller supplies `PreCollide`, `PostCollide`, `SolveLongitudinalAndLateralConstraints`, `AllowSleep`, save/restore and `Draw` ([VehicleController.h L56-L82](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleController.h#L56-L82)).

| | `WheeledVehicleController` | `MotorcycleController` | `TrackedVehicleController` |
|---|---|---|---|
| Base | `VehicleController` | **derives from wheeled** (settings too) | `VehicleController` |
| Wheels | `WheelSettingsWV` | `WheelSettingsWV` | `WheelSettingsTV` |
| Engine, transmission | yes | yes (inherited) | yes |
| Power split | `VehicleDifferentialSettings` list + limited-slip ratio between them | same | two `VehicleTrack`s (left, right) |
| Steering | wheels turn: `steer = -right * mMaxSteerAngle` | wheels turn, limited by speed so the lean stays under the max | no steer angle; left/right track speed ratios |
| Tyre model | slip curves + `TireMaxImpulseCallback` | same + lean impulse | constant friction per wheel, no callback |
| Input | forward, right, brake, handbrake | same (bike sample passes `false` for handbrake) | forward, left ratio, right ratio, brake |

Sources: [WheeledVehicleController.h L72-L203](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.h#L72-L203); [MotorcycleController.h L10-L117](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/MotorcycleController.h#L10-L117); [TrackedVehicleController.h L55-L167](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/TrackedVehicleController.h#L55-L167); steering at [WheeledVehicleController.cpp L257-L263](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L257-L263).

**Shared by all three:** the whole `VehicleConstraintSettings` (up, forward, max pitch/roll, wheels' base `WheelSettings`, anti-roll bars), the collision tester, friction combine, step callbacks, gravity override and probe rate. `VehicleEngineSettings` and `VehicleTransmissionSettings` are shared by type between wheeled and tracked, but the tracked settings constructor sets different defaults (section 4).

**Motorcycle lean** ([MotorcycleController.cpp L91-L261](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/MotorcycleController.cpp#L91-L261)). The class "is still in development and may need a lot of tweaks/hacks to work properly" ([MotorcycleController.h L11](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/MotorcycleController.h#L11)).
- **Target lean** is the direction of the total ground impulse on the wheels (normal plus lateral), smoothed by `mLeanSmoothingFactor` (frame-rate dependent), with the forward part removed and clamped to `mMaxLeanAngle` (L100-L129).
- **Steering limit:** each steerable wheel's angle is capped at `asin(wheelBase * tan(maxLean) * |g| / (v^2 * cos(caster)))`, unless `EnableLeanSteeringLimit(false)` (L141-L184).
- **Lean spring:** when every wheel touches with a positive suspension impulse, a PID-like angular impulse about forward (`mLeanSpringConstant`, `mLeanSpringDamping`, `mLeanSpringIntegrationCoefficient`) is applied straight to the body **inside the velocity solve** (`AddAngularImpulse`, then a cancelling linear `AddImpulse`). Otherwise the integral decays (L190-L258).
- `EnableLeanController(false)` lets the bike fall over ([MotorcycleController.h L55-L56](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/MotorcycleController.h#L55-L56)).
- Defaults: max lean 45 degrees, spring 5000, damping 1000, integration 0, decay 4, smoothing 0.8 ([MotorcycleController.h L23-L40](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/MotorcycleController.h#L23-L40)).

**Tracked drive** ([TrackedVehicleController.cpp L171-L420](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/TrackedVehicleController.cpp#L171-L420)). Each track's driven wheel carries the angular velocity; every other wheel on the track spins at `track speed * driven radius / own radius` (L60-L63). Engine torque goes through the gearbox to both tracks scaled by their ratio, then `SyncLeftRightTracks` forces the left:right speed ratio, conserving momentum by inertia (L188-L208, L264-L291). Brake torque on a track is spread over its touching wheels by radius (L293-L341). A negative ratio on one side gives a pivot turn. `SetDriverInput`, `SetLeftRatio` and `SetRightRatio` assert the ratios are non-zero ([TrackedVehicleController.h L91, L98, L102](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/TrackedVehicleController.h#L91)).

## 3. Wheels

**`WheelSettings` (all controllers)** ([Wheel.h L29-L41](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/Wheel.h#L29-L41)). **All vectors are in the body's local space** (body origin, not centre of mass).

| Setting | Default | Meaning |
|---|---|---|
| `mPosition` | 0 | Suspension attachment point (the "hard point") |
| `mSuspensionDirection` | (0, -1, 0) | Suspension travel, "should point down" |
| `mSteeringAxis` | (0, 1, 0) | Axis the wheel steers about, "should point up" |
| `mWheelUp`, `mWheelForward` | +Y, +Z | Neutral-steer wheel frame; up gives camber, forward gives toe; forward need not be perpendicular to up |
| `mSuspensionMinLength`, `mSuspensionMaxLength` | 0.3, 0.5 m | Max raised and max droop length from the hard point |
| `mSuspensionPreloadLength` | 0 | Natural length is max + preload; "more bouncy" |
| `mSuspensionSpring` | `FrequencyAndDamping`, 1.5 Hz, 0.5 | `SpringSettings`; stiffness/damping or mass-normalized modes also accepted ([VehicleConstraint.cpp L443-L472](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L443-L472)) |
| `mRadius`, `mWidth` | 0.3, 0.1 m | Wheel size; the ray and sphere probes use only the radius |
| `mEnableSuspensionForcePoint`, `mSuspensionForcePoint` | false, 0 | Apply tyre forces at a fixed body point instead of the contact point: more stable, less accurate against dynamic bodies |

Construction asserts: unit-length vectors, `0 <= min <= max`, preload >= 0, frequency > 0, damping >= 0, radius > 0, width >= 0 ([Wheel.cpp L66-L81](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/Wheel.cpp#L66-L81)). Note the frequency assert applies even in stiffness mode.

**Suspension behaviour.**
- The spring pushes only, never pulls (`SolveVelocityConstraint(..., 0, FLT_MAX)`).
- Below min length a hard velocity stop and a position correction keep the axle above the contact plane ([VehicleConstraint.cpp L513-L517, L574-L580, L599-L623](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L513-L517)).
- In frequency mode, stiffness is scaled by an effective mass taken from the body's mass and inertia at the force point. So a heavier body keeps the same ride frequency ([L444-L459](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L444-L459)).
- An angled contact divides stiffness and damping by `max(0.1, cos)` ([L474-L503](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L474-L503)).
- A wheel with `max == min` has no spring at all, only the hard stop (L440, L510-L511). The tank's end wheels use this ([TankTest.cpp L104](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/TankTest.cpp#L104)).

**`WheelSettingsWV` (wheeled and motorcycle)** ([WheeledVehicleController.h L31-L37](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.h#L31-L37); curves at [WheeledVehicleController.cpp L45-L56](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L45-L56)):

| Setting | Default | Meaning |
|---|---|---|
| `mInertia` | 0.9 kg m^2 | Wheel's moment of inertia (20 kg, 0.3 m) |
| `mAngularDamping` | 0.2 | Spin damping, `dw/dt = -c w` |
| `mMaxSteerAngle` | 70 degrees | Asserted <= 90 degrees ([L89](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L89)) |
| `mLongitudinalFriction` | (0, 0), (0.06, 1.2), (0.2, 1.0) | `LinearCurve`: friction vs slip ratio |
| `mLateralFriction` | (0, 0), (3, 1.2), (20, 1.0) | Friction vs slip angle **in degrees** |
| `mMaxBrakeTorque` | 1500 Nm | Brake pedal at 1 |
| `mMaxHandBrakeTorque` | 4000 Nm | Handbrake at 1; samples set 0 on the front wheels |

Curves clamp to their end values outside their range ([LinearCurve.cpp L25-L39](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Core/LinearCurve.cpp#L25-L39)).

**`WheelSettingsTV` (tracked):** a constant `mLongitudinalFriction` 4.0 and `mLateralFriction` 2.0 ([TrackedVehicleController.h L27-L28](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/TrackedVehicleController.h#L27-L28)). Inertia, damping and brakes live on the track.

**Live or rebuild.**
- **Wheel settings have no setters.** The runtime `Wheel` keeps a `RefConst` to the caller's settings object (no copy) and exposes it only as `const` ([Wheel.h L55, L123](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/Wheel.h#L55); [Wheel.cpp L66-L68](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/Wheel.cpp#L66-L68)). It reads every field each step, so mutating a settings object the host still holds would take effect. But that is undocumented, the construction asserts would not re-run, and one settings object may be shared by many vehicles: the stress test shares one set across 225 cars ([VehicleStressTest.cpp L101](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleStressTest.cpp#L101)) *(inference)*. The supported path is a rebuild; the car sample's settings menu restarts the test ([VehicleConstraintTest.cpp L347](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleConstraintTest.cpp#L347)).
- **Wheel count, wheel kind, controller kind, `mUp` and `mForward`** have no setters ([VehicleConstraint.h L119-L123](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L119-L123)): rebuild.
- **Wheel state is live:** `SetAngularVelocity`, `SetRotationAngle`, `SetSteerAngle` ([Wheel.h L60-L73](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/Wheel.h#L60-L73)). The wheeled and motorcycle controllers overwrite the steer angle from the input every step (section 5).

## 4. Drivetrain

**`VehicleEngineSettings`** ([VehicleEngine.h L34-L39](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleEngine.h#L34-L39); curve at [VehicleEngine.cpp L23-L29](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleEngine.cpp#L23-L29)):
- `mMaxTorque` 500 Nm, `mMinRPM` 1000, `mMaxRPM` 6000, `mInertia` 0.5 kg m^2, `mAngularDamping` 0.2.
- `mNormalizedTorque` is a curve (0, 0.8), (0.66, 1.0), (1, 0.8). Its doc says X is "0 = mMinRPM, 1 = mMaxRPM", but the code evaluates it at `currentRPM / mMaxRPM` ([VehicleEngine.h L37, L63](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleEngine.h#L37)). So X = 0 means 0 RPM, not min RPM.
- RPM is clamped to [min, max] ([L50](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleEngine.h#L50)). The engine never stalls.

**`VehicleTransmissionSettings`** ([VehicleTransmission.h L33-L41](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleTransmission.h#L33-L41)):
- `mMode` `Auto` or `Manual`.
- `mGearRatios` {2.66, 1.78, 1.3, 1.0, 0.74}, `mReverseGearRatios` {-2.90}. Forward ratios must be > 0, reverse < 0.
- Auto only: `mSwitchTime` 0.5 s, `mClutchReleaseTime` 0.3 s, `mSwitchLatency` 0.5 s, `mShiftUpRPM` 4000, `mShiftDownRPM` 2000.
- `mClutchStrength` 10.

**Auto mode** ([VehicleTransmission.cpp L51-L129](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleTransmission.cpp#L51-L129)):
- From neutral, or when the input's sign opposes the gear, it jumps to 1st or reverse 1. It shifts up above `mShiftUpRPM` when allowed and down below `mShiftDownRPM`.
- During a shift the clutch is 0 for `mSwitchTime`, then ramps to 1 over `mClutchReleaseTime`, then no shift for `mSwitchLatency`.
- The wheeled controller allows an upshift only when no driven wheel slips (slip > 0.1 or airborne) and RPM is rising ([WheeledVehicleController.cpp L609-L620](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L609-L620)).

**Manual mode:** `Update` does nothing. The host sets gear and clutch with `VehicleTransmission::Set(gear, clutchFriction)`, gear -1 = reverse, 0 = neutral ([VehicleTransmission.h L48-L51](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleTransmission.h#L48-L51)). The enum's comment names a `SetTransmissionInput` that does not exist ([L18](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleTransmission.h#L18)).

**The wheeled model** ([WheeledVehicleController.cpp L266-L656](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L266-L656)):
- Engine and driven wheels are coupled through the clutch (`torque = strength * clutchFriction * (w_engine - avg wheel speed at clutch)`) and solved together by **implicit Euler with Gaussian elimination**, because "the system is very stiff". Brakes then lock or slow each wheel.
- The ground's reaction torque is estimated from last step's longitudinal impulse scaled by `dt / previous dt` (L502-L556). So a variable `dt` affects the result *(inference)*.

**`VehicleDifferentialSettings`** ([VehicleDifferential.h L31-L36](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleDifferential.h#L31-L36)):
- `mLeftWheel` and `mRightWheel` are wheel indices, -1 for none. The bike drives only `mRightWheel = 1`.
- `mDifferentialRatio` 3.42, `mLeftRightSplit` 0.5.
- `mLimitedSlipRatio` 1.4 (`FLT_MAX` = open).
- `mEngineTorqueRatio` 1, and the ratios of all differentials must sum to 1 (asserted, [WheeledVehicleController.cpp L359-L365](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L359-L365)).
- Between differentials, `mDifferentialLimitedSlipRatio` (default 1.4) shifts torque to the slowest one ([WheeledVehicleController.h L85](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.h#L85)). The limited-slip logic is at [VehicleDifferential.cpp L42-L79](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleDifferential.cpp#L42-L79).
- **A wheel in no differential is undriven**; it only brakes and rolls.

**`VehicleAntiRollBar`** ([VehicleAntiRollBar.h L26-L28](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleAntiRollBar.h#L26-L28)): left and right wheel indices (default 0, 1) and `mStiffness` 1000 N/m. Each step, if both wheels touch, it adds `(right length - left length) * stiffness * dt` as opposite extra suspension impulses ([VehicleConstraint.cpp L279-L300](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L279-L300)).

**Tracks** (`VehicleTrackSettings`, [VehicleTrack.h L35-L40](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleTrack.h#L35-L40)):
- `mDrivenWheel`, `mWheels` (indices, including the driven one), `mInertia` 10, `mAngularDamping` 0.5, `mMaxBrakeTorque` 15000 Nm, `mDifferentialRatio` 6.
- Exactly two tracks, `ETrackSide::Left` and `Right`.
- The tracked settings constructor overrides the engine and gearbox defaults "around... the M1 Abrams", with mass and torque scaled down 10x "to keep the simulation sane": 500-4000 RPM, 500 Nm, shift 1000/3500, gears {4, 3, 2, 1}, reverse {-4, -3} ([TrackedVehicleController.h L57-L58](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/TrackedVehicleController.h#L57-L58); [TrackedVehicleController.cpp L97-L108](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/TrackedVehicleController.cpp#L97-L108)).
- Unlike the wheeled one, the tracked engine is driven by the fastest track's speed rather than solved implicitly (L223-L258).

**Live or rebuild.** The controllers **copy** engine, transmission, differential and track settings into runtime objects at construction ([WheeledVehicleController.cpp L178-L215](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L178-L215); [TrackedVehicleController.cpp L130-L162](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/TrackedVehicleController.cpp#L130-L162)), and expose them as writable references.

| What | How | Live? |
|---|---|---|
| Engine (torque, RPM range, curve, inertia, damping) | `GetEngine()` | yes, "next time step" ([WheeledVehicleController.h L126-L127](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.h#L126-L127)) |
| Transmission (mode, ratios, shift points, clutch) | `GetTransmission()` | yes ([L132-L133](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.h#L132-L133)); gear and clutch through `Set` in manual |
| Differentials | `GetDifferentials()`, `SetDifferentialLimitedSlipRatio` | yes ([L138-L143](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.h#L138-L143)) |
| Anti-roll bars | `VehicleConstraint::GetAntiRollBars()` | yes ([VehicleConstraint.h L166-L168](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L166-L168)) |
| Tracks | `GetTracks()` | yes ([TrackedVehicleController.h L124-L125](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/TrackedVehicleController.h#L124-L125)) |
| Tyre impulse rule | `SetTireMaxImpulseCallback` | yes |
| Motorcycle lean spring, damping, integration, decay, smoothing, both enables | setters | yes ([MotorcycleController.h L55-L84](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/MotorcycleController.h#L55-L84)) |
| Motorcycle `mMaxLeanAngle` | none | rebuild |

The car sample writes engine torque, clutch strength and limited-slip ratios every tick before the step ([VehicleConstraintTest.cpp L232-L240](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleConstraintTest.cpp#L232-L240)). The construction asserts (positive ratios, `shiftUp < maxRPM` in auto, LSD ratio > 1) do not re-run on live writes.

## 5. Driver input

| Controller | Call | Ranges |
|---|---|---|
| Wheeled, motorcycle | `SetDriverInput(forward, right, brake, handBrake)`, or `SetForwardInput` / `SetRightInput` / `SetBrakeInput` / `SetHandBrakeInput` | forward -1..1 in auto (sign picks direction), 0..1 in manual; right -1..1 (**+1 = right**); brake and handbrake 0..1 |
| Tracked | `SetDriverInput(forward, leftRatio, rightRatio, brake)`, or the single setters | forward as above; ratios -1..1, **non-zero** (asserted); brake 0..1 |

Sources: [WheeledVehicleController.h L100-L121](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.h#L100-L121); [TrackedVehicleController.h L86-L107](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/TrackedVehicleController.h#L86-L107).

- **Steer sign:** `Wheel::mSteerAngle` is "positive is to the left", so the controller sets `-right * maxSteer` ([Wheel.h L69](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/Wheel.h#L69); [WheeledVehicleController.cpp L262](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L262)). Steering is **instant**: the angle jumps to the input each step. The motorcycle sample ramps its input at 4 per second itself ([MotorcycleTest.cpp L166-L176](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/MotorcycleTest.cpp#L166-L176)).
- **Plain stores.** The inputs are plain member stores; they are read in `PreCollide`/`PostCollide`/the solve of every collision step of the next `PhysicsSystem::Update` ([VehicleConstraint.cpp L184, L303, L584](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L184)). So input is set **before** `Update` (every sample does it in `PrePhysicsUpdate`), and one value holds for all collision steps of that update.
- **Inside the step**, the pre-step callback "can be used to allow higher-level code to e.g. control steering" ([VehicleConstraint.h L95-L99](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L95-L99)). It runs before `PreCollide` on each collision step ([VehicleConstraint.cpp L160-L162](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L160-L162)).
- **No reverse logic in Jolt.** Pressing back while rolling forward selects reverse at once in auto mode. The samples brake until the speed is under 0.1 m/s, then accept the new direction ([VehicleConstraintTest.cpp L185-L202](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleConstraintTest.cpp#L185-L202)).
- **Input is part of the saved state** (`SaveState` writes all four), so a restored world resumes with the saved input ([WheeledVehicleController.cpp L827-L849](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L827-L849)).

## 6. Ground contact

**The three testers** ([VehicleCollisionTester.h L84-L144](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleCollisionTester.h#L84-L144); [VehicleCollisionTester.cpp](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleCollisionTester.cpp)):

| Tester | Query | Length | Slope filter | Notes |
|---|---|---|---|---|
| `VehicleCollisionTesterRay(layer, up = +Y, maxSlope = 80 degrees)` | `CastRay` from the hard point along the suspension | max length + radius | yes: normal must be within `maxSlope` of a fixed **world** `up` | thinnest; ignores width ([L19-L103](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleCollisionTester.cpp#L19-L103)) |
| `VehicleCollisionTesterCastSphere(layer, radius, up, maxSlope)` | `CastShape` of a sphere | max length + wheel radius - sphere radius | yes | `mUseShrunkenShapeAndConvexRadius`, `mReturnDeepestPoint` ([L125-L216](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleCollisionTester.cpp#L125-L216)) |
| `VehicleCollisionTesterCastCylinder(layer, convexRadiusFraction = 0.1)` | `CastShape` of a wheel-sized cylinder, oriented by the wheel's current steer and spin | max length | **none** | the only one using width; can hit walls ([L241-L329](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleCollisionTester.cpp#L241-L329)) |

- All three keep the **closest** non-sensor hit (strictly smaller early-out fraction), skip sensors, and return body, sub-shape, contact point, normal and suspension length ([L46-L74, L159-L188, L279-L303](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleCollisionTester.cpp#L46-L74)).
- Each has a cheap `PredictContactProperties` that reprojects onto the last contact plane, used between full probes ([L105-L123, L218-L239, L331-L374](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleCollisionTester.cpp#L105-L123)).
- The ray and sphere testers' `up` is a constructor argument, not the vehicle's up or gravity *(inference: [VehicleCollisionTester.h L93, L115](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleCollisionTester.h#L93))*. With a gravity override or a non-Y world up, it must be set to match.
- All three use **`GetNarrowPhaseQueryNoLock()`** and `GetBodyLockInterfaceNoLock()`, because "when this runs all bodies are locked" ([L52-L53, L92](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleCollisionTester.cpp#L52-L53)).
- A tester is ref-counted and may be shared by vehicles; the samples keep one per mode ([VehicleConstraintTest.h L73](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleConstraintTest.h#L73)). It is live: `SetVehicleCollisionTester` takes effect at the next step, and the car sample sets it every tick ([VehicleConstraintTest.cpp L246](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleConstraintTest.cpp#L246)).

**Filters** ([VehicleCollisionTester.h L26-L45, L77-L80](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleCollisionTester.h#L26-L45); built at [VehicleCollisionTester.cpp L21-L28](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleCollisionTester.cpp#L21-L28)):
- **Defaults.** The tester takes one `ObjectLayer`. By default it queries as a body in that layer would collide: `GetDefaultBroadPhaseLayerFilter(layer)` and `GetDefaultLayerFilter(layer)`, plus `IgnoreSingleBodyFilter(vehicle body)`.
- **Overrides.** `SetBroadPhaseLayerFilter`, `SetObjectLayerFilter` and `SetBodyFilter` replace each default. They are stored as **raw pointers**, so the host keeps them alive. A custom body filter **replaces** the self-ignore, so it must skip the vehicle body itself.
- **No shape filter, no `GroupFilter`.** Collision groups are simulation-only; queries use `BodyFilter` instead ([Architecture.md L650-L655](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L650-L655)).
- The filters run on Jolt job threads during the step (section 9).

**Against FenrirPhysics's layers** ([JoltBackend.cpp L76-L255, L2624-L2675](../../../packages/FenrirPhysics/src/Backend/Jolt/JoltBackend.cpp)):
- **Three broad-phase trees,** `Static`, `Moving`, `Trigger`. `Moving` pairs with every tree, triggers pair with static ([L76-L99](../../../packages/FenrirPhysics/src/Backend/Jolt/JoltBackend.cpp)).
- **One object layer per `LayerKey{broadPhase, layers, mask}`.** `ObjectPairs` is the two-sided layer-and-mask rule ([L102-L113, L236-L256](../../../packages/FenrirPhysics/src/Backend/Jolt/JoltBackend.cpp)). The registry "changed only between steps... the filters read it during a step" (L111-L113), which holds for vehicle probes too.
- **What follows, as options rather than choices:**
  - **(a) Default filters.** Pass the vehicle body's own acquired layer: wheels then see exactly what the body collides with, by the two-sided rule. A `Moving` key admits the `Trigger` tree too, but the testers drop sensor hits themselves.
  - **(b) The query filters.** Set FenrirPhysics's `QueryBroadPhaseFilter`/`QueryLayerFilter`/`QueryBodyFilter` as the three overrides for the one-sided query rule. `QueryBodyFilter` skips `ignoreEntity` by body user data, which is the entity ([JoltBackend.cpp L1489, L2661-L2675](../../../packages/FenrirPhysics/src/Backend/Jolt/JoltBackend.cpp)), so it can also stand in for the self-ignore.
- **Wheels never touch triggers.** Wheel contacts are query hits, not body contacts: they raise no `ContactListener` events, sensor or otherwise *(inference)*.

**What a wheel reports** ([Wheel.h L57-L112](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/Wheel.h#L57-L112); `WheelWV` adds slip and friction at [WheeledVehicleController.h L61-L65](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.h#L61-L65)):

| Getter | Meaning |
|---|---|
| `HasContact()`, `GetContactBodyID()`, `GetContactSubShapeID()` | Ground body and sub-shape (ID only; the pointer "is not valid outside of the simulation step", [VehicleConstraint.cpp L238](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L238)) |
| `GetContactPosition()`, `GetContactNormal()`, `GetContactLongitudinal()`, `GetContactLateral()` | World space, from the probe; "by the time you call this the vehicle has moved". All **assert `HasContact()`** |
| `GetContactPointVelocity()` | Ground velocity at the contact, cached at probe time |
| `GetSuspensionLength()`, `HasHitHardPoint()` | Current length from the hard point; whether the min-length stop is active |
| `GetSuspensionLambda()`, `GetLongitudinalLambda()`, `GetLateralLambda()` | Total impulses (N s) of the last solve |
| `GetAngularVelocity()`, `GetRotationAngle()`, `GetSteerAngle()` | Spin (rad/s, + = forward), angle [0, 2 pi), steer (+ = left) |
| `WheelWV::mLongitudinalSlip`, `mLateralSlip` | Slip ratio; slip angle in **radians** (the curve takes degrees) |
| `WheelWV::mCombinedLongitudinalFriction`, `mCombinedLateralFriction`, `mBrakeImpulse` | Tyre x ground friction after the combine callback |

These are public members, not getters. `WheelTV` has only the combined frictions, brake impulse and track index ([TrackedVehicleController.h L49-L52](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/TrackedVehicleController.h#L49-L52)).

**Per-surface friction.** Two hooks:
1. **`SetCombineFriction(fn(wheelIndex, &longitudinal, &lateral, groundBody, subShapeID))`** turns the tyre's curve value into a combined friction. The default is `sqrt(tyre * groundBody.GetFriction())` for each axis ([VehicleConstraint.h L83-L90, L238-L244](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L83-L90)). Wheeled wheels call it in `PostCollide`, tracked wheels in `Update` ([WheeledVehicleController.cpp L129-L133](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L129-L133); [TrackedVehicleController.cpp L78-L83](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/TrackedVehicleController.cpp#L78-L83)). The sub-shape ID lets it read a per-triangle `PhysicsMaterial` *(inference)*. **Jolt never reads the ground's material on its own; only `Body::GetFriction()`.**
2. **`SetTireMaxImpulseCallback(fn(wheelIndex, &maxLong, &maxLat, suspensionImpulse, longFriction, latFriction, longSlip, latSlip, dt))`**, wheeled and motorcycle only. It sets the friction impulse caps, by default `friction * suspension impulse` ([WheeledVehicleController.h L148-L151, L191-L196](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.h#L148-L151)). It is called in the velocity solve, every iteration ([WheeledVehicleController.cpp L672-L676](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L672-L676)).
   - **The car sample multiplies the longitudinal cap by 10**, to keep handling tuned under an older bug that let `mNumVelocitySteps` times too much impulse through ([VehicleConstraintTest.cpp L160-L168](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleConstraintTest.cpp#L160-L168); [APIChanges.md L66](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/APIChanges.md?plain=1#L66)).

Both are `std::function`s called on Jolt job threads with all bodies locked (section 9).

## 7. Wheels in the world

- **`GetWheelWorldTransform(wheelIndex, wheelRight, wheelUp)`** returns `body world transform * GetWheelLocalTransform(...)` ([VehicleConstraint.cpp L147-L150](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L147-L150)).
- **The local transform** ([L127-L145](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L127-L145)) is built from:
  - the steered wheel basis (rotate `mWheelUp`/`mWheelForward` about `mSteeringAxis` by the steer angle, [L116-L125](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L116-L125));
  - the position `mPosition + mSuspensionDirection * suspensionLength`;
  - the spin `RotationX(angle)`.
- **The two arguments** say which axes of the wheel **model** are its axle (`right`) and up. The doc's advice is to flip `right` for left and right wheels sharing one mesh ([VehicleConstraint.h L154-L164](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L154-L164)). The samples draw a Y-axis cylinder with `(sAxisY, sAxisX)` ([VehicleConstraintTest.cpp L264-L269](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleConstraintTest.cpp#L264-L269)).
- **When it is valid:** any time outside the step. It mixes the body's **current** transform with the wheel state (steer, spin, suspension length) from the **last** `OnStep`. That state is written at the start of each collision step, before the solve and integration ([VehicleConstraint.cpp L195-L241](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L195-L241)).
  - Read **before** `Update`, it pairs the pre-step body pose with the suspension found for it. The samples draw there "since we draw the bodies too in the state before the step" ([VehicleConstraintTest.cpp L263](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleConstraintTest.cpp#L263)).
  - Read **after** `Update`, the body has moved one collision step past the probe, so suspension length and steer lag by one collision step *(inference)*. A FenrirPhysics write-back after the step would see this lag.
- **Spin direction:** for wheeled vehicles, `mAngle` advances in `PostCollide` *before* the drivetrain changes the spin, and it is wrapped to [0, 2 pi) ([WheeledVehicleController.cpp L102-L105, L275-L280](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L102-L105)). An interpolated drawing must unwrap it *(inference)*.
- **Pose only.** There is no per-wheel transform for physics: wheels have no bodies, so FenrirPhysics's write-back of body poses does not cover them.
- The `VehicleConstraint` doc notes wheels "jump up and down quite quickly" over rubble and suggests smoothing the visual wheel ([VehicleConstraint.h L63-L64](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L63-L64)).

## 8. Other bodies

**What a wheel does to the ground body.**
- Every wheel part is a two-body `AxisConstraintPart` between the vehicle body and the contact body ([VehicleConstraint.cpp L508-L521](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L508-L521)). So a **dynamic** body under a wheel receives the opposite suspension, drive, brake and side impulses, and is pushed and loaded.
- `BuildIslands` wakes such bodies and links them into the vehicle's island ([L328-L379](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L328-L379)). Static and kinematic ground take no impulse.
- **Standing on a moving (kinematic or dynamic) body:** the contact point's velocity is cached at probe time and all slip and friction are relative to it ([VehicleConstraint.cpp L245-L246](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L245-L246); [WheeledVehicleController.cpp L112, L679](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L112)), so a platform carries the vehicle through tyre friction *(inference)*. For a kinematic platform, the velocity is whatever `MoveKinematic` set before the step. FenrirPhysics sets it in its pre-step sync (see the characters research, section 6).
- **Force point.** `mEnableSuspensionForcePoint` moves where forces act from the contact point to a fixed body point; the doc trades "more accurate simulation when interacting with dynamic objects" against stability ([Wheel.h L41](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/Wheel.h#L41)).
- **Light objects.** Over light objects the body dips, an "artifact of the iterative solver". Jolt suggests three fixes: more velocity steps (also per vehicle via `mNumVelocityStepsOverride`), less mass difference, or filtering rubble out of the probes and pushing it with "fake wheels (keyframed bodies)" ([VehicleConstraint.h L46-L62](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L46-L62)).

**Being driven over / hitting wheels.** Wheels have no shape, so another body passes through a wheel and meets only the vehicle body's own shape. The inactive probe exists to wake a sleeping vehicle when a body moves "into the wheels but not touching the body" ([VehicleConstraint.h L180-L181](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L180-L181)). A wheel on another vehicle's body is an ordinary dynamic contact.

**The vehicle body itself** is an ordinary dynamic body in its object layer. It collides, raises contact events and is pushed like any other. The tank sample uses a `GroupFilter` so the hull, turret and barrel do not collide with each other, and hinges for the turret ([TankTest.cpp L66-L79, L121-L163](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/TankTest.cpp#L66-L79)). The default probe ignores only the hull, not other bodies of a multi-body vehicle.

**Pitch and roll limit.**
- `mMaxPitchRollAngle` (default pi = off) keeps the vehicle's up inside a cone around world up, which is minus gravity or the override ([VehicleConstraint.h L33, L75-L77, L125-L126](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L33); [VehicleConstraint.cpp L164-L181](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L164-L181)).
- It is a one-sided angular constraint against the world, solved in velocity and position ([L400-L423, L586-L588, L625-L628](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L400-L423)). It acts even in the air *(inference)*.
- Live via `SetMaxPitchRollAngle`. All samples use 60 degrees.

**Gravity override.** `OverrideGravity(g)` sets the body's gravity factor to 0 and adds `g * mass` as a force each step while awake; `ResetGravityOverride()` restores the factor to **1**, not to its previous value ([VehicleConstraint.h L113-L117](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L113-L117); [VehicleConstraint.cpp L164-L172](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L164-L172)). A FenrirPhysics gravity factor other than 1 would be lost on reset *(inference)*.

## 9. Threading and timing

**Where it runs in `PhysicsSystem::Update`.**
- `Update` locks every body, then sets up `collisionSteps` steps. In each step the **step-listener jobs run first**; apply-gravity and determine-active-constraints wait on them ([PhysicsSystem.cpp L236, L355, L388-L399](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.cpp#L388-L399)).
- So `OnStep` runs **during** `Update`, once per collision step, before that step's collision detection and solve ("received inCollisionSteps times for every PhysicsSystem::Update(...) call", [PhysicsStepListener.h L28](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsStepListener.h#L28)). `inContext.mDeltaTime` is the collision-step `dt`, and `mIsFirstStep`/`mIsLastStep` mark the ends ([PhysicsStepListener.h L12-L19](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsStepListener.h#L12-L19)).
- The constraint half then runs in the island solver with all other constraints.

**On which threads.**
- Listeners are split into batches of `mStepListenersBatchSize` (8) and spread over up to `max_concurrency` jobs ([PhysicsSystem.cpp L243, L683-L710](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.cpp#L683-L710); [PhysicsSettings.h L38-L42](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSettings.h#L38-L42)). So **vehicles run in parallel on Jolt's job threads**, which for FenrirPhysics are the App's worker threads through `PoolJobs` ([JoltBackend.cpp L775-L790](../../../packages/FenrirPhysics/src/Backend/Jolt/JoltBackend.cpp)).
- `OnStep` runs "while all body and constraint mutexes are locked. You can read/write bodies and constraints but not add/remove them." Parallel listeners must avoid races themselves ([PhysicsStepListener.h L29-L32](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsStepListener.h#L29-L32)). The debug access grant there is: velocities read/write, positions read-only, activation allowed but no deactivation ([PhysicsSystem.cpp L685-L690](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.cpp#L685-L690)).
- A vehicle's `OnStep` writes only its own body (gravity force, sleep timer) and its own wheels, and reads others through the no-lock interfaces *(inference, from section 1 and 6)*.
- **The three step callbacks, the combine-friction function and the tester's filters** run on those job threads, inside the step, with bodies locked. Host code there must not use the locking `BodyInterface` or `GetNarrowPhaseQuery()` *(inference)*.
  - `PreStep`: before the probes; the last chance to move the vehicle.
  - `PostCollide`: after the probes; may change velocity.
  - `PostStep`: after the drivetrain; e.g. air control.

  Sources: [VehicleConstraint.h L92-L111](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L92-L111).
- **The tyre impulse callback and the motorcycle lean** run in the velocity solve. The vehicle is assigned to the non-parallel split of a large island ([VehicleConstraint.cpp L381-L384](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L381-L384)), so its solve is single-threaded within its island.

**What the host may call, and when.**
- Between `Update` calls: everything (input, settings references, tester, callbacks, wheel state, `GetWheelWorldTransform`).
- During `Update`: nothing from outside the callbacks. Jolt says this of constraints in general, not of vehicles *(inference)*.
- `AddStepListener`/`RemoveStepListener` take `mStepListenersMutex`, which `Update` holds for its whole run ([PhysicsSystem.cpp L117, L125, L235, L675](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.cpp#L235)), so adding a vehicle during a step blocks until it ends.

**Collision steps and probe rate.**
- With `collisionSteps = N`, each vehicle probes, runs its drivetrain and solves N times per `Update`, with one set of driver input.
- `SetNumStepsBetweenCollisionTestActive/Inactive(n)` thins the probes: 0 = never, 1 = every step. Wheels are tested round-robin, phased by the vehicle's body ID ([VehicleConstraint.h L170-L183](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L170-L183); [VehicleConstraint.cpp L189-L226](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L189-L226)). The stress test uses 2 when active and 0 when inactive for 225 cars ([VehicleStressTest.cpp L102-L103](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleStressTest.cpp#L102-L103)).
- **Against FenrirPhysics's PostTick:**
  - Sequence: sync, push edits, joints, pushes, move characters (3b), `m_backend.Step(dt, CollisionSteps(dt))`, write back, joint measures, events, poses, debug lines ([PhysicsWorld.cpp L220-L237](../../../packages/FenrirPhysics/src/PhysicsWorld.cpp)).
  - `CollisionSteps` is `ceil(dt * 60)` ([L142-L146](../../../packages/FenrirPhysics/src/PhysicsWorld.cpp)).
  - A vehicle's `OnStep` would run inside `m_backend.Step`, after 3b, so characters move against the vehicle's pre-step pose. Input would have to be pushed by the time `Step` is called.

## 10. Determinism

What the result depends on:
- **The body ID.** It seeds the probe round-robin (`mCurrentStep = Hash64(body ID index)`, [VehicleConstraint.cpp L102-L103](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L102-L103)). This matters only when probing less than every step.
- **Probe hit order.** Each tester keeps a hit only if its fraction is strictly smaller than the best so far. Ties go to whichever hit arrives first, and narrow-phase hit order is not deterministic ([VehicleCollisionTester.cpp L49, L163, L283](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleCollisionTester.cpp#L49); [Architecture.md L804-L805](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L804-L805)). An exact tie (a wheel on a seam between two bodies or triangles) can pick a different ground body or sub-shape between runs *(inference)*.
- **Vehicle order.** Listener order changes on removal (swap with last, [PhysicsSystem.cpp L127-L130](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.cpp#L127-L130)), and listeners run concurrently. But each `OnStep` writes only its own vehicle, so the order should not change results *(inference)*.
- **Solve order.** In the solve, vehicles are ordered with all constraints by priority, then constraint index ([ConstraintManager.cpp L103-L115](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/ConstraintManager.cpp#L103-L115)). The index changes as constraints are removed. FenrirPhysics already sets distinct priorities on joints for this reason ([JoltBackend.cpp L1644-L1646](../../../packages/FenrirPhysics/src/Backend/Jolt/JoltBackend.cpp)), and a vehicle's `mConstraintPriority` is a `ConstraintSettings` field like a joint's ([Constraint.h L82](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/Constraint.h#L82)).
- **Variable `dt`.** The wheeled drivetrain scales last step's impulses by `dt / previous dt` ([WheeledVehicleController.cpp L503](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L503)), and the motorcycle's lean smoothing is "frame rate dependent" ([MotorcycleController.h L38-L40](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/MotorcycleController.h#L38-L40)).
- **No process-global state** in the vehicle code. The only statics are the `JPH_TRACE_VEHICLE_STATS` tracer, which is commented out ([WheeledVehicleController.cpp L18, L245-L255](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L18)). The motorcycle logs to `JPH_DET_LOG` for determinism checks ([MotorcycleController.cpp L139](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/MotorcycleController.cpp#L139)).
- **State save and restore.** A vehicle is a constraint, so `PhysicsSystem::SaveState` includes it ([PhysicsSystem.cpp L2918](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.cpp#L2918); [ConstraintManager.cpp L220-L238](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/ConstraintManager.cpp#L220-L238)).
  - Saved: driver input, engine RPM, gear and clutch timers, per wheel spin, angle, contact body ID, position, normal, lateral, suspension length and solver impulses, the pitch/roll axis and the step counter ([VehicleConstraint.cpp L646-L699](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L646-L699)).
  - Not saved: steer angle (the wheeled controllers recompute it from the input each step), the motorcycle's integrated lean angle (only `mTargetLean` is saved, [MotorcycleController.cpp L263-L275](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/MotorcycleController.cpp#L263-L275)), and all settings.
- **FenrirPhysics's scope.** FenrirPhysics builds Jolt with `CROSS_PLATFORM_DETERMINISTIC OFF` ([libs/CMakeLists.txt L22](../../../packages/FenrirPhysics/libs/CMakeLists.txt)), so the bar is the same machine and build. None of the above needs more than that.

## 11. Debug drawing

`VehicleConstraint::DrawConstraint` only calls `mController->Draw` ([VehicleConstraint.cpp L635-L642](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.cpp#L635-L642)). All of it, `SetRPMMeter` included, exists only under `JPH_DEBUG_RENDERER` ([VehicleConstraint.h L195-L198](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L195-L198); [WheeledVehicleController.h L153-L156](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.h#L153-L156)). FenrirPhysics sets `DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF` ([libs/CMakeLists.txt L19](../../../packages/FenrirPhysics/libs/CMakeLists.txt)), so none of it is compiled ([Jolt.cmake L914-L918](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Jolt.cmake#L914-L918)).

What the wheeled controller draws ([WheeledVehicleController.cpp L743-L823](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/WheeledVehicleController.cpp#L743-L823)):
- **Whole vehicle:**
  - an RPM gauge: pies for below min, below shift-down, shift band and above shift-up, plus a needle, at `mRPMMeterPosition` (local +1 Y) ([VehicleEngine.cpp L66-L80](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleEngine.cpp#L66-L80));
  - a yellow line for wheel speed at the clutch;
  - a text block with the inputs, gear, clutch, RPM and km/h.
- **Per wheel:**
  - the suspension: red line from hard point to min length, green from min to max;
  - a marker at the current length, red when below min;
  - the wheel basis: forward red, up green, right blue, steering axis yellow;
  - a wireframe cylinder at the wheel pose with its spin, red when at the hard stop.
- **Per contact:** normal yellow, longitudinal red, lateral blue, and text with spin, suspension length, slips and frictions. Without contact, red text with the spin.

The tracked controller draws the same per-wheel set plus per-track speed text ([TrackedVehicleController.cpp L428-L506](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/TrackedVehicleController.cpp#L428-L506)). The motorcycle adds arrows for current up (yellow) and target lean (red) from the centre of mass ([MotorcycleController.cpp L279-L289](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/MotorcycleController.cpp#L279-L289)). `DrawConstraintLimits` draws nothing. Every input to these lines is public through the getters in sections 3, 6 and 7, so the lines can be rebuilt outside Jolt *(inference)*.

## 12. What the samples do

**Car** (`VehicleConstraintTest`, [.h L40-L69](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleConstraintTest.h#L40-L69); [.cpp L30-L270](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleConstraintTest.cpp#L30-L270)):
- **Body:** box 1.8 x 0.4 x 4 m, centre of mass moved 0.2 m down, 1500 kg, `Layers::MOVING`.
- **Wheels:** four at `(+-0.9, -0.18, +-1.4)`, radius 0.3, width 0.1. Suspension 0.3-0.5 m, 1.5 Hz, damping 0.5. Front steer 30 degrees and no handbrake; rear no steer.
- **Drivetrain:** one differential on wheels 0 and 1, the **front** (front-wheel drive); 4WD splits 0.5/0.5. Limited slip 1.4 on all differentials, anti-roll bars on both axles at the default 1000.
- **Engine and tyres:** 500 Nm, clutch 10, the x10 longitudinal tyre cap.
- **Limits and probe:** max roll 60 degrees, cylinder tester by default, draw size 0.1.
- **Input:** arrow keys give forward and right of +-1. Down while moving forward brakes until under 0.1 m/s, then reverses. Z is the handbrake and cancels throttle. Any input wakes the body.
- The settings menu exposes caster, king pin, camber, toe and suspension angles, all 0 by default.

**Motorcycle** (`MotorcycleTest`, [.cpp L30-L227](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/MotorcycleTest.cpp#L30-L227)):
- **Body:** box 0.4 x 0.6 x 0.8 m, centre of mass 0.3 m down, 240 kg.
- **Wheels:** two, radius 0.31, width 0.05, at z +-0.75. Suspension 0.3-0.5 m; front 1.5 Hz, rear 2 Hz. Brakes 500 front, 250 rear.
- **Front geometry:** steer 30 degrees, **caster 30 degrees** (suspension tilted, steering axis = -suspension).
- **Drivetrain:** engine 150 Nm, 1000-10000 RPM, shift 2000/8000, six gears {2.27, 1.63, 1.3, 1.09, 0.96, 0.88}, reverse -4, clutch 2. One differential driving only the rear (`left = -1, right = 1`), ratio 1.93 x 40/16.
- **Probe and limits:** cylinder tester with convex radius fraction 1.0 (a rounded tyre), max pitch/roll 60 degrees.
- **Input:** Z brakes. Steering is ramped at 4/s. Braking is scaled by `(1 - sin(lean))^2` "as we'll spin out". Handbrake is always 0.

**Tank** (`TankTest`, [.cpp L33-L306](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/TankTest.cpp#L33-L306)):
- **Body:** hull 3.4 x 1 x 6.4 m, centre of mass 0.5 m down, 4000 kg.
- **Wheels:** nine per side, radius 0.3, width 0.1. Suspension 0.3-0.5 m at 1 Hz; the first and last wheel are fixed at 0.3 (no spring). The last wheel of each track is the driven one.
- **Drivetrain:** `TrackedVehicleControllerSettings` defaults; ray tester; max pitch/roll 60 degrees.
- **Turret and barrel:** separate bodies on motorized hinges, kept from colliding with the hull by a `GroupFilter`.
- **Input:** Up/Down forward, RShift brake. Turning sets the inner track's ratio to 0.6. Under 1 m/s with no throttle, turning becomes a pivot turn (that side's ratio -1, forward 1). The tank is woken every tick.

**Stress** ([VehicleStressTest.cpp L26-L161](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleStressTest.cpp#L26-L161)): 225 cars sharing one settings object, ray probes every second step when awake and none when asleep, the default (uncentred) centre of mass.

`VehicleSixDOFTest` shows the other route, a car built from real wheel bodies on `SixDOFConstraint`s ([VehicleSixDOFTest.h L18](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Vehicle/VehicleSixDOFTest.h#L18)).

---

## What this means for FenrirPhysics

Facts the vehicles spec will rest on. These are not the decisions themselves.

**The constraint:**
- A Jolt vehicle is one dynamic body plus a `VehicleConstraint` that must be added both as a constraint and as a step listener, and removed from both before the body is destroyed.
- It needs a collision tester before the first step, and the body's mass and inertia (read live each step).
- It disables itself while the body is out of the broad phase and ignores shape changes.
- It does not wake the body on input; the host does.
- Any registered vehicle keeps `PhysicsSystem::Update` from skipping an all-asleep world.

**Controllers:**
- Wheeled, motorcycle (a wheeled subclass with a lean spring, marked "still in development") and tracked.
- The kind fixes the wheel-settings kind. Neither can change without a rebuild.

**Wheels:**
- All geometry is body-local (body origin, not centre of mass).
- Jolt gives no setters for `WheelSettings`. It reads the caller's object by reference each step, which works but is undocumented and shared between vehicles.
- Wheel count, kind, `mUp` and `mForward` are construction-only.

**Drivetrain:**
- Engine, transmission, differentials, anti-roll bars, tracks, tyre callback, friction combine, tester, max pitch/roll and probe rate are all **live** through writable references or setters.
- The motorcycle's max lean is construction-only.
- Manual gearboxes are driven with `VehicleTransmission::Set`.
- The engine curve's X axis is `RPM / maxRPM`, despite its doc.

**Input:**
- Four plain setters (forward, right, brake, handbrake), or tracked (forward, left ratio, right ratio, brake).
- They are read on every collision step of the next `Update`.
- Steering is instant, positive right input steers right, and Jolt has no brake-before-reverse logic.

**Ground contact:**
- One of three probe kinds, each a query with an `ObjectLayer` (default two-sided filters) or explicit broad-phase/object/body filter overrides held by raw pointer.
- FenrirPhysics's layer registry can supply either (section 6).
- Probes skip sensors and raise no contact events.
- Per-surface friction comes only through `Body::GetFriction()` or the combine callback (which gets the sub-shape).

**Wheel poses:**
- `GetWheelWorldTransform` combines the body's current pose with wheel state from the start of the last collision step.
- It is exact before `Update` and one collision step stale after it.
- Wheels are not bodies, so their poses need their own write-back path.

**Other bodies:**
- Wheels push and wake dynamic bodies under them and ride moving ground through tyre friction.
- Nothing collides with a wheel; only the hull collides.
- Max pitch/roll is a live world-relative cone.
- The gravity override resets the body's gravity factor to 1.

**Threading:**
- `OnStep` runs inside `m_backend.Step`, after FenrirPhysics's 3b, once per collision step, on Jolt job threads, in parallel across vehicles, with every body locked.
- Callbacks, filters and the combine function run there too and must use no-lock access.

**Determinism:**
- Within one build the result depends on body IDs (probe phasing), constraint priority and index, `dt` variation, and exact ties between probe hits.
- There is no process-global state.
- Vehicle state is inside `PhysicsSystem::SaveState`, except steer angle and the motorcycle's lean integral.

**Debug drawing:**
- With `JPH_DEBUG_RENDERER` off, Jolt's vehicle drawing is not compiled.
- Every quantity it draws is readable through public getters and members.

**Surprises worth carrying forward:**
- Registering a vehicle disables `Update`'s all-asleep early-out, contrary to the step-listener doc.
- The engine torque curve's X axis is `RPM / maxRPM`, not the documented `(RPM - min) / (max - min)`.
- The car sample's handling depends on a x10 longitudinal tyre cap kept for an old bug.
- The cylinder probe has no slope limit; the ray and sphere probes use a fixed world up given at construction.
- `MotorcycleController::GetSettings` copies `mLeanSpringDamping` from the new settings object onto itself, so it always reports the default 1000 ([MotorcycleController.cpp L299](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/MotorcycleController.cpp#L299)).
- `VehicleEngineSettings`'s binary save omits `mInertia` and `mAngularDamping` ([VehicleEngine.cpp L31-L45](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleEngine.cpp#L31-L45)).
