---
status: accepted
---

# Vehicles are a component on their chassis, with one entity per wheel

A **vehicle** is a `Vehicle` component on the entity of a dynamic `RigidBody`, its **chassis**. Each wheel is a `VehicleWheel` component on its own **wheel entity**, which names the vehicle and is posed by physics for drawing. The decision follows ADR-0011's rules: an entity holds at most one component of each type, components are plain data that physics diffs, and anything switched during play should be a field edit. It also follows ADR-0014's, which says game code drives physics through fields that physics applies in its own phase. The survey of Unity, Unreal, Godot, bevy_rapier, Avian, O3DE and PhysX is on branch `research/engine-vehicle-models`. The facts about Jolt are on `research/jolt-vehicles`.

## The model

```cpp
struct Vehicle
{
    VehicleKind kind;   // tagged union: WheeledVehicle, MotorcycleVehicle, TrackedVehicle
    VehicleInput input; // forward [-1, 1], steer [-1, 1] (+ right), brake [0, 1], handbrake [0, 1]
    bool enabled = true;
};

struct VehicleWheel
{
    Entity vehicle;      // the chassis entity
    Math::Vec3 position; // chassis-local, from the chassis origin
    VehicleTrack track;  // left or right; read only by tracked vehicles
    // wheel settings decided by later tickets
};
```

- **The chassis is the body.** `Vehicle` sits beside the entity's `RigidBody` and `Collider`, and the chassis is the only part of a vehicle that collides. A body is at most one vehicle, so one component per type is enough.
- **Axes are a convention:** chassis-local +Y is up and −Z is forward, matching the engine's Y-up, right-handed conventions and the camera's default front. A model authored facing another way is turned on a child mesh entity.
- **The kind is a tagged union,** as `CollisionShape` is in `Collider` and `JointKind` is in `Joint`. Each alternative holds that kind's drivetrain. Changing kind is an assignment, and physics rebuilds the backend vehicle privately.
- **Driver input is normalised and persistent.** Game code writes `input` in `Tick`, and physics applies it in `PostTick`. A tracked vehicle's alternative adds left and right track ratios and ignores `steer` and `handbrake`: turning a steering value into track speeds is game code's job. Physics never writes `input`. A value stays until game code changes it, so input is not a one-shot request.
- **Wheels live on wheel entities.**
  - The `vehicle` link is explicit, never "my parent", so a wheel entity can sit anywhere in the hierarchy (usually under the chassis).
  - `position` is local to the chassis origin, not the centre of mass, and follows the chassis's world scale, as joint frames do (ADR-0013). Wheel dimensions are absolute metres and are not scaled.
  - The wheel entity's `Transform` is output only. Physics writes the wheel's pose (spin, steer, suspension travel) into it for drawing, and never reads a mount point from it. Game code writing it has no effect.
  - Wheel order, which is the backend's wheel index, is entity order. Differentials and anti-roll bars name wheel entities.
  - Adding, removing or relinking a wheel is a structural change that rebuilds each vehicle involved.
- **Inactive, as for joints and characters:** the component is kept, there is no backend vehicle, one warning is logged, and the vehicle comes back once the setup is valid.
  - A vehicle is inactive without a dynamic `RigidBody` with a usable collider, or beside a `CharacterBody`.
  - A wheel is inactive when it names something that is not a vehicle, sits on the chassis entity, or shares its entity with a `RigidBody`, `CharacterBody` or `Collider` (triggers included).
  - A vehicle with no wheels is valid but inert.
- **Switching:** `enabled = false` leaves a plain rigid body resting on its chassis, and its wheel entities stay where they last were. There is no per-wheel `enabled`. Destroying or unlinking a wheel entity takes the wheel off.

## Considered options

- **Wheels as an array inside `Vehicle`** (Unreal's `WheelSetups`, bevy_rapier, PhysX). Rejected for three reasons:
  - Components must be blittable for C# (ADR-0007), so a growable array is out, and a fixed capacity is awkward (Jolt's tank sample has 18 wheels).
  - Each wheel needs an entity anyway, so that its pose can be drawn.
  - Unity's ECS package splits wheels onto their own entities for the same reasons.
- **A vehicle entity naming its body,** like a joint entity (ADR-0013). Rejected: the backend's vehicle holds exactly one body, so ADR-0013's reason (a body takes part in many joints) does not apply, and a second vehicle naming the same body would need a rule.
- **One component type per kind** (`WheeledVehicle`, `TrackedVehicle`). Rejected for the reason ADR-0011 gives for colliders: changing kind becomes a structural change, and an entity could hold two contradictory kinds.
- **Physical per-wheel input** (Unity's `WheelCollider`, Godot, Rapier: torques and steer angles per wheel). Rejected: the spec exposes a full drivetrain, which turns pedals into torque, and normalised pedals are what Unreal, PhysX, Unity's ECS package and the backend take.
- **The wheel entity's `Transform` as its mount point** (Godot, Unity). Rejected: physics overwrites it every tick to pose the wheel, and ADR-0013 already rejects frames captured from a pose.
- **Up and forward axis fields.** Not now: changing them means rebuilding the vehicle, and nothing in the spec needs them. They can be added later, defaulting to the convention.

## Consequences

- Physics keeps a private index from each vehicle to its wheel entities, in entity order, and rebuilds the backend vehicle when that set changes.
- `VehicleKind` is the engine's third tagged union. "What does C# get for vehicles?" decides whether that is the trigger for the deferred `[TaggedUnion]` generator.
- A wheel entity's `Transform` is written by physics, like a dynamic body's, so the diff must not take that write for an edit.
