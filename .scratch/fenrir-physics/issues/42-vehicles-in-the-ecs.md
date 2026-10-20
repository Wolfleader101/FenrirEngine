# How is a vehicle modelled in the ECS?

Type: grilling
Status: open
Blocked by: 40, 41

## Question

How does game code make and drive a vehicle, given ADR-0011 (plain-data components that physics diffs; anything switched during play is a field edit), ADR-0013 (joints on joint entities with explicit local frames) and ADR-0014 (character bodies driven by a field physics applies in its own phase)? The spec covers wheeled cars, motorcycles and tracked vehicles, with the full drivetrain exposed as plain data. Findings: [What does Jolt offer for vehicles?](40-what-jolt-offers-for-vehicles.md) and [How do other engines expose vehicles?](41-how-other-engines-expose-vehicles.md).

- **The component:** a vehicle component on the entity of a dynamic `RigidBody` (the chassis), or on its own entity naming the body; what it holds, and what makes it inactive (no `RigidBody`, a non-dynamic motion type, no collider).
- **The kind:** wheeled, motorcycle and tracked as a tagged union in `CollisionShape`'s and `JointKind`'s pattern (a third engine union, which is the `[TaggedUnion]` generator's stated trigger), separate components, or one component with optional parts.
- **Wheels:** an array inside the vehicle component, or one wheel entity each (a child of the chassis whose `Transform` gives its position, and whose pose physics writes back for drawing); how many wheels, and how a tracked vehicle's wheels belong to a track.
- **Driver input:** throttle, steering, brake and handbrake (and a tracked vehicle's per-track input) as fields game code writes in `Tick`, physics applies in `PostTick`; normalised or physical; whether physics writes anything back into them.
- **Switching:** `bool enabled`; whether a disabled vehicle is a plain rigid body.
- **Vocabulary:** the glossary term (vehicle, and whatever a wheel and a track are called) and words to avoid (constraint, controller).

Probably an ADR (where the wheels live is hard to reverse).

Facts from "What does Jolt offer for vehicles?": `VehicleConstraint` is a one-body constraint and a step listener, needing a dynamic body; wheels are not bodies and nothing collides with them. Wheel positions and axes are in body space from the body origin (not the centre of mass). The controller kind fixes the wheel-settings kind, and wheel count, wheel kind and the vehicle's up and forward axes need a rebuild; the drivetrain is live. Input is wheeled and motorcycle forward, right, brake, handbrake; tracked forward, left and right ratios (non-zero), brake. Input does not wake the body.

From "How do other engines expose vehicles?": every engine is a chassis body plus a cast per wheel. Wheels are an array on the vehicle (Unreal, bevy_rapier, PhysX) or an entity or node each (Unity, Godot, and Unity's ECS package, which splits wheel settings on wheel entities from state on the vehicle and raises wheel added and removed events). Input is normalised pedals and steering (throttle and brake in [0, 1], steer in [−1, 1]: Unreal, PhysX, Unity ECS) or physical per-wheel values (Unity `WheelCollider`, Godot, Rapier), always written before the step. bevy_rapier keeps settings, input and output in one component on the chassis.
