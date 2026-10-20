# How does a vehicle step each tick, and what does game code read back?

Type: grilling
Status: open
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
- **Inactive scenes:** the world pauses; confirm nothing new.

Facts from "What does Jolt offer for vehicles?": a vehicle's `OnStep` runs inside the step (after the character move, step 3b), once per collision step, on Jolt job threads, in parallel across vehicles with all bodies locked; any host callback (filters, the friction combine, the tyre max-impulse callback) runs there and must not lock. Input is read on every collision step of the next update, so it is set before the step. `GetWheelWorldTransform` is exact before the update and one collision step stale after it. Wheel contact reports body, sub-shape, point, normal, suspension length and slip. Probes skip triggers and raise no contact events. Friction comes from the ground body's friction or a combine callback given the sub-shape. Input does not wake the body, and any registered vehicle stops a sleeping world from skipping its update. Determinism depends on body IDs, constraint priority and index, `dt` and exact probe ties, with no process-global state.

From "How do other engines expose vehicles?": the ECS designs write state back each tick (bevy_rapier's `VehicleWheel.state` with the ground entity; Unity ECS's contact, compression, slip, gear and RPM); the others query. Godot and Unity ECS pose wheels themselves; the rest offer a pose to copy. Only PhysX maps ground material to grip. Parked creep and low-speed jitter is the top complaint (remedies: an anti-creep anchor, sticky-tyre constraints, aggressive sleep).
