---
status: accepted
---

# Physics components are plain data, and physics finds edits by diffing

FenrirPhysics is configured with components, like the rest of the engine (ADR-0007, ADR-0010). Two parts of their design will surprise a reader who knows Unity or Godot, and both follow from the same rules of this ECS: an entity holds at most one component of each type, and a structural change (adding or removing a component) is restricted. A C# system's structural change waits until the system returns, and a native function may make none (ADR-0009). So anything game code switches during play should be a field edit, never a structural change.

## Shape and body kinds are values, not component types

- **One `RigidBody` with a motion type** (`Static`, `Kinematic`, `Dynamic`), not Godot's `StaticBody` / `KinematicBody` / `RigidBody`. Freezing a ragdoll or releasing a kinematic platform is a field edit, and an entity cannot hold two contradictory body kinds.
- **One `Collider { CollisionShape shape; }`**, where `CollisionShape` is a plain-data tagged union (box, sphere, capsule, convex hull, mesh), not Unity's `BoxCollider` / `SphereCollider` components. Changing a collider's kind is an assignment (`collider.shape = SphereShape{1.0f}`); an entity cannot hold two colliders of different kinds; physics and code that resolves compound bodies look for one component type. The shape is read and written through a checked typed view (`shape.As<BoxShape>().halfExtents`, in C# `Shape.Box.HalfExtents`), which is the price of the kind being type-safe.

Considered and rejected for colliders:

- **Per-shape components** (`BoxCollider`): the most direct field access, and they work in C# today, but changing kind is a structural change and two kinds on one entity are representable.
- **A handle to a shared shape resource** (Godot's `Shape3D`, Avian's `SharedShape`): editing a size needs a physics call, and every primitive becomes a resource to release. Sharing is kept anyway: identical shapes share one backend shape through a private cache keyed by value. Only convex hull and mesh data, which must be cooked, sit behind a FenrirPhysics-owned handle.
- **A flat struct holding every kind's fields:** meaningless fields on every collider, and nothing to stop editing a box's radius.

C# components cannot hold a tagged union today, so scripting needs that capability before C# can reach `Collider` (tracked as ".scratch/fenrir-physics/issues/16-csharp-tagged-union-components.md"). It is a general capability: light kinds and camera projections have the same shape.

*Amended 2026-10-16:* that capability now exists. A component may hold a tagged union, written in C# by hand in one pattern (FenrirScripting's README, "Tagged unions"), and C# reads and writes `Collider` and its `CollisionShape` that way.

*Amended 2026-10-20:* the pattern is no longer written by hand. FenrirScripting's `[TaggedUnion]` generator writes it from the tag and the kind fields, and `CollisionShape` and `JointKind` use it.

## Edits are found by diffing, not by hooks

FenrirECS will report component adds and removes (a neutral API, per ADR-0008), and physics creates and destroys bodies from those, in a batch before the step. Field edits are not reported. Each tick, physics compares each body's components against its private copy of what it last applied or wrote back (for a shape, the tag and the active kind's fields) and pushes the differences to the backend.

*Amended 2026-10-16:* FenrirECS now reports them: `EntityList::Watch` returns a `ComponentWatch` that records every add and remove of one component type, which physics drains once per tick. It drains each watch through a tracked component set (`TrackedComponents`): the watch, a scan of what was there when the set was made, and the entities that have the type as of its last drain.

We chose this over an edit hook (`on_update`, a dirty flag, or explicit "mark changed" calls) because C# systems write components through `Ref<T>` straight into ECS storage, which no hook sees, and because explicit calls are easy to forget. The cost is one plain-data comparison per body per tick, which is small beside the step itself; bevy_rapier uses the same technique for transforms.

## Consequences

- Writing a dynamic body's `Transform` or velocity is a teleport or a velocity change, detected by the diff. Physics writes those fields back after each step and updates its copy, so its own writes are not mistaken for edits.
- Changing a shape's dimensions or scale rebuilds the backend shape: fine occasionally, costly every tick.
- The diff order, like the add and remove order, is part of the determinism contract: physics visits bodies in a reproducible order.
