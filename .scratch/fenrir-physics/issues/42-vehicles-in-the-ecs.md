# How is a vehicle modelled in the ECS?

Type: grilling
Status: resolved
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

## Answer

ADR-0015, "Vehicles are a component on their chassis, with one entity per wheel" (`docs/adr/0015-vehicles-are-a-component-on-their-chassis-with-wheel-entities.md`). Vocabulary is in `GLOSSARY.md`: vehicle, chassis, wheel (wheel entity), track, vehicle kind, driver input.

- **The component:** `Vehicle` sits on the entity of a dynamic `RigidBody`, and that body is the chassis, the only part of the vehicle that collides. There is no separate vehicle entity, because a body is at most one vehicle.
- **Axes:** fixed by convention to chassis-local +Y up and −Z forward (the camera's default front), with no fields. Fields can be added later, defaulting to the convention.
- **The kind:** a `VehicleKind` tagged union on `Vehicle` (wheeled, motorcycle, tracked), with each kind's drivetrain in its alternative. Changing kind rebuilds the vehicle privately. Whether this third union triggers the `[TaggedUnion]` generator stays with "What does C# get for vehicles?".
- **Wheels:** one `VehicleWheel` per wheel entity.
  - It holds an explicit `vehicle` link (never "my parent"), a `position` local to the chassis origin that follows the chassis's world scale (as joint frames do), and a left-or-right `track` that only tracked vehicles read. One wheel type serves every kind; fields a kind doesn't use are ignored.
  - Wheel dimensions are absolute metres, never scaled.
  - Wheel order is entity order, which is the backend's index. Differentials and anti-roll bars name wheel entities.
  - Adding, removing or relinking a wheel rebuilds each vehicle involved.
  - Physics writes the wheel entity's `Transform` for drawing, and never reads a mount point from it. Game code's writes to it have no effect.
  - There is no per-wheel `enabled`; destroying or unlinking the wheel entity takes the wheel off.
  - Arrays were rejected because a component can't hold a growable array and still be blittable for C#, and a fixed cap doesn't suit Jolt's 18-wheel tank sample.
- **Driver input:** `VehicleInput input` on `Vehicle`, normalised:
  - `forward` in [−1, 1], where negative reverses
  - `steer` in [−1, 1], where + is right
  - `brake` and `handbrake` in [0, 1]

  The tracked alternative adds `leftTrack` and `rightTrack` ratios and ignores `steer` and `handbrake`; turning a steer value into track ratios is game code's job. Game code writes input in `Tick` and physics applies it in `PostTick`. Physics never writes input, and it persists until changed (it is not a one-shot request).
- **Inactive** (component kept, no backend vehicle, one warning, back once valid):
  - A vehicle is inactive without a dynamic `RigidBody` that has a usable collider, or beside a `CharacterBody`.
  - A wheel is inactive when it names something that isn't a vehicle, sits on the chassis entity, or shares its entity with a `RigidBody`, `CharacterBody` or any `Collider` (triggers included).
  - A vehicle with no wheels is valid but inert.
- **Switching:** `bool enabled = true` on `Vehicle`. Disabled, the vehicle is a plain rigid body resting on its chassis, and its wheel entities stay where they last were. Parking is the handbrake's job.
- **Handed on:**
  - "Which settings does each vehicle kind expose?": all settings, gear requests, per-wheel axes (defaulting to the convention), and exactly which point `position` names.
  - "How does a vehicle step each tick, and what does game code read back?": per-wheel state and where it is written, waking on input, and wheel-pose interpolation.
  - "How does a vehicle meet other bodies, joints and characters?": edge cases during a drive.
