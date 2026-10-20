# How does a vehicle step each tick, and what does game code read back?

Type: grilling
Status: resolved
Blocked by: 40, 42

## Question

Where does a vehicle sit in `PostTick`'s sequence (drain watches, diff, joints and pushes, move characters (3b), step, write back, events, debug lines), and what does it report afterwards? Builds on [What does Jolt offer for vehicles?](40-what-jolt-offers-for-vehicles.md) and [How is a vehicle modelled in the ECS?](42-vehicles-in-the-ecs.md), and follows "How does a character move each tick?" and "What does game code read back from a character, and which events?".

- **Order:** when driver input is applied, and whether the vehicle runs inside the step (Jolt's step listener) or beside it; what that means for threading, the collision-step count and determinism (order between vehicles).
- **State:** speed, engine RPM, current gear, and per wheel: grounded, ground body and collider, contact point and normal, suspension compression, slip, spin and steer angle. Written back into the component (as `CharacterState` is) or asked for through `PhysicsWorld` (as `JointState` is).
- **Wheel visuals:** each wheel's pose (spin, steer, suspension travel) written to a wheel entity's `Transform` for drawing; how that write is kept from being taken for an edit; interpolation alongside the chassis's `RigidBody::interpolate`.
- **Per-surface grip:** tyre friction taken from the ground collider's `PhysicsMaterial` (ice, grass, tarmac); whether vehicles need their own friction value beside it.
- **Events:** whether wheel contacts send `ContactBegin`/`ContactEnd` or only the chassis does; any vehicle events (none, by the characters' precedent).
- **Sleep:** a parked vehicle sleeping, and input waking it.
- **Teleports and edits** detected by the diff (moving the chassis, changing a wheel), and a disabled or inactive vehicle's state.
- **From "How is a vehicle modelled in the ECS?" (ADR-0015):** wheels are `VehicleWheel` components on wheel entities whose `Transform` physics writes (output only), so per-wheel state can live on the wheel entity or on `Vehicle`; `VehicleInput` is never written by physics, so waking on an input change is physics noticing the diff; a disabled vehicle's wheel entities stay where they last were.
- **Inactive scenes:** the world pauses; confirm nothing new.

Facts from "What does Jolt offer for vehicles?": a vehicle's `OnStep` runs inside the step (after the character move, step 3b), once per collision step, on Jolt job threads, in parallel across vehicles with all bodies locked; any host callback (filters, the friction combine, the tyre max-impulse callback) runs there and must not lock. Input is read on every collision step of the next update, so it is set before the step. `GetWheelWorldTransform` is exact before the update and one collision step stale after it. Wheel contact reports body, sub-shape, point, normal, suspension length and slip. Probes skip triggers and raise no contact events. Friction comes from the ground body's friction or a combine callback given the sub-shape. Input does not wake the body, and any registered vehicle stops a sleeping world from skipping its update. Determinism depends on body IDs, constraint priority and index, `dt` and exact probe ties, with no process-global state.

From "How do other engines expose vehicles?": the ECS designs write state back each tick (bevy_rapier's `VehicleWheel.state` with the ground entity; Unity ECS's contact, compression, slip, gear and RPM); the others query. Godot and Unity ECS pose wheels themselves; the rest offer a pose to copy. Only PhysX maps ground material to grip. Parked creep and low-speed jitter is the top complaint (remedies: an anti-creep anchor, sticky-tyre constraints, aggressive sleep).

## Answer

No ADR: every call can be reversed by adding fields or events, and all of it follows ADR-0015, the characters' write-back precedent ("What does game code read back from a character, and which events?") and the interpolation rules ("Is there transform interpolation, and who owns it?"). Glossary: **compression** and **slip** added.

- **Where it runs:** pushed in `PostTick` step 2, run inside step 4.
  - Driver input and every vehicle edit are pushed in step 2, after the bodies' edits (so a chassis made or remade in steps 1–2 already exists), beside joint sync.
  - The vehicle runs inside step 4 as the backend's step listener, after characters move (3b), once per collision step, on job threads, in parallel across vehicles. Characters therefore meet a vehicle's pre-step pose, as they do any body's.
  - Probes run on every collision step. Probe rate stays refused as tuning ("Which settings does each vehicle kind expose?").
  - **Determinism:** each vehicle gets a private constraint priority from its chassis entity order, as joints do. Each `OnStep` writes only its own vehicle, so the parallel order doesn't matter.
  - The only host callback is the friction combine. It reads a private table (body and sub-shape to collider friction) that is built before the step, and it never locks.
- **State is written back, not queried** (the characters' precedent): a nested `VehicleState state` inside `Vehicle` and a `VehicleWheelState state` inside `VehicleWheel`, overwritten at write-back (step 5). Game code's edits are ignored and overwritten, and physics keeps no diff copy.
  - Rejected: a `PhysicsWorld` query in the joints' style. A HUD reads speed, RPM and gear every tick, and skid marks read every wheel every tick, which from C# would be a native call per wheel per tick. Per-wheel state also can't live on `Vehicle`, because no vehicle component holds an array.
  - The state describes the last collision step's probes against the chassis's post-step pose. That gives one collision step of lag in contact, compression and steer, which the backend's own wheel pose has too. The doc comments say so.
  - A ground entity destroyed before game code reads it is detected by its version, as for characters and events.
- **Shape:**

  ```cpp
  struct VehicleState // written by physics at write-back; game code's edits are overwritten
  {
      float forwardSpeed = 0.0f;   // m/s along chassis -Z, signed
      float engineRpm = 0.0f;
      int gear = 0;                // engaged gear: -1 reverse, 0 neutral, 1..n (input.gear's numbering)
      float clutch = 0.0f;         // applied clutch, [0, 1], 0 engaged
      bool shifting = false;
      int wheelsOnGround = 0;
      float leanAngle = 0.0f;      // motorcycle only; rad, + right
      float leftTrackSpeed = 0.0f; // tracked only; m/s
      float rightTrackSpeed = 0.0f;
  };

  struct VehicleWheelState // written by physics at write-back; game code's edits are overwritten
  {
      bool onGround = false;
      Entity groundBody;                            // on ground only
      Entity groundCollider;                        // the touched sub-shape's collider entity
      Math::Vec3 contactPoint = Math::Vec3(0.0f);   // world space
      Math::Vec3 contactNormal = Math::Vec3(0.0f);  // world space, unit
      Math::Vec3 groundVelocity = Math::Vec3(0.0f); // the ground's velocity at the contact
      float compression = 0.0f;      // m risen from full extension, 0..suspension.travel
      float suspensionForce = 0.0f;  // N
      float longitudinalSlip = 0.0f; // ratio, in VehicleTyre's terms
      float lateralSlip = 0.0f;      // rad
      float angularVelocity = 0.0f;  // rad/s, + rolls forward
      float steerAngle = 0.0f;       // rad, + right (as input.steer)
  };
  ```

  - In Automatic, `gear` and `clutch` are the gearbox's own choices. In Manual they echo the clamped input.
  - Fields a kind doesn't have stay 0 (tracked wheels have no slip). Ground fields are empty or zero off the ground.
  - Left out: tyre forces, combined friction, and a "skidding" bool. Game code compares the slips with `VehicleTyre`'s `peakSlip` and `slideSlip`.
- **Wheel poses:** before the first step, the wheel entity sits at `position` ("Which settings does each vehicle kind expose?").
  - From step 5 on, after the chassis's write-back (parents before children), physics writes each wheel entity's world position and rotation through `SetWorldTransform`. The pose is the chassis pose × the wheel's chassis-local pose: `position` raised by `compression`, then caster, camber and toe, then steer, then spin about the axle.
  - With every angle 0, the wheel entity's rotation equals the chassis's, with its X as the axle, so left and right wheels can share a symmetric mesh. Scale is never written.
  - Interpolation follows the chassis's `RigidBody::interpolate`, with no flag of its own. Compression, steer and spin (angle unwrapped) are blended in chassis space between the last two steps, then composed with the chassis's blended pose in `AfterTicks`. `BeforeTicks` restores the true pose.
  - Game code's writes to a wheel's `Transform` are overwritten, so there is no edit detection for wheels.
- **Per-surface grip:** the tyre's curve value (or a track's `longitudinalFriction`/`lateralFriction`) combines with the `friction` of the `PhysicsMaterial` on the collider the wheel touches, found by sub-shape. The rule is the geometric mean, `sqrt(tyre × ground)`.
  - Body contacts already use this rule, so one `friction` value means the same to bodies and tyres. The price is that ice must be set very low: 0.05 under a 1.2 tyre still gives 0.24.
  - Rejected: the product, because the default 0.5 material would halve every tyre.
  - There is no surface table and no vehicle-level friction. The chassis's own `PhysicsMaterial` is only the hull's.
- **Events:** wheels send no `ContactBegin`/`ContactEnd` and never set off triggers, because their probes are queries. Landing, skidding or a surface change are changes in `state` that game code notices. The chassis sends its usual body events. There are no vehicle events, by the characters' precedent; any can be added later.
- **Sleep and waking:** in step 2, any of these wakes the chassis: a change to `input` (including `gear` and `clutch`), an edit to `Vehicle`, `VehicleWheel`, `VehicleDifferential` or `VehicleAntiRollBar`, or a rebuild.
  - The backend then keeps the chassis awake while forward input is held or the engine is above idle.
  - A sleeping vehicle keeps its last state, with spin 0.
  - There is no anti-creep helper. Parking is the handbrake plus sleep, which the spike checks.
- **Teleports, rebuilds, disabling, pausing:**
  - A chassis teleport is the existing body teleport. Wheel spin and engine RPM carry over, as a body's velocity does, and wheel poses and their interpolation snap.
  - Every private rebuild (a kind change, a wheel-set change, `MotorcycleLean::maxAngle`) carries over what still maps: engine RPM, gear, and each surviving wheel's spin. New wheels start still, the chassis keeps its velocity, and wheel poses snap.
  - **Disabled or inactive:** `state` on the vehicle and its wheels resets to defaults, and wheel `Transform`s stay where they were (ADR-0015). Re-enabling builds the vehicle fresh, with nothing carried over.
  - **Paused world:** nothing new. State is kept, and wheels aren't posed.
- **Handed on:**
  - "Spike: a car, a motorcycle and a tank driving a small track through the proposed model": checks that a handbraked car on a slope stays put and sleeps, that ice and grass feel right under the geometric mean, that wheels interpolate smoothly, that the state fields read sensibly, and that rebuilds keep RPM, gear and spin.
  - "What does C# get for vehicles?": both `state` structs are read-only in C#, through the `ref readonly` accessor that "What does C# get for characters?" added.
  - "What do the vehicle demo scenes show?": wheel meshes are drawn from the written-back poses, and readouts come from `state`.
