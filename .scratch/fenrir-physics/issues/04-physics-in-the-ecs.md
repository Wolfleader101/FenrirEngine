# How is physics modelled in the ECS?

Type: grilling
Status: resolved
Blocked by: 01, 03

## Question

What components does FenrirPhysics add, and how does their state relate to the physics world?

- Which components: a body component (motion type static/dynamic/kinematic, mass, damping), collider components (shape, material: friction, restitution), sensors/triggers?
- Compound shapes: several colliders on one entity, colliders on child entities, or both?
- Which side owns the truth? The ECS is authoritative and the physics world mirrors it, or the physics world is authoritative for dynamic bodies and writes back? Per motion type.
- How are changes noticed (a component edited by a system or a C# script): change tracking, explicit API calls, or re-sync every tick?
- `Transform` is local to its parent through `Relationship`, but the backend simulates in world space. What happens for a body with a parent, a parent that moves, and a dynamic body whose parent is also a body?
- How is `Transform::scale` treated (uniform, non-uniform, ignored)?
- How do entity destruction and component removal tear down bodies?

## Notes from "Where does the backend seam go?"

The user's preferences for the component layout, to weigh against what suits the ECS best (the user defers to that):

- **Bodies:** separate `RigidBody` / `StaticBody` / `KinematicBody` components (Godot-like), or one `RigidBody` with a motion type (Unity-like). Either is acceptable. Also open: is a collider without a body static (Unity), or does static need a body component?
- **Colliders:** per-shape components (`BoxCollider`, `SphereCollider`, ...; the user likes the names), or one collider component referencing a separate collision-shape resource (Godot's `CollisionShape3D` + `Shape3D`). Either is acceptable. A cooked shape (convex hull, mesh) must be referenced by a FenrirPhysics-owned handle, never a backend type.
- **Triggers:** an `isTrigger` flag on a collider, or a separate area/trigger-volume component. Either is acceptable.
- `SoftBody` and `CharacterBody` are the names for those feature families' components.

## Answer

**Components** (all plain data):

- **`RigidBody`**: one component with `MotionType { Static, Kinematic, Dynamic }`, mass, damping, gravity scale, and linear and angular velocity as read/write fields. Switching motion type is a field edit, never a structural change. Three components (Godot-style) were rejected: switching would be structural, which C# defers and native functions cannot do, and `RigidBody` + `StaticBody` on one entity would be representable. A collider with no `RigidBody` on itself or an ancestor is static; `RigidBody { Static }` means the same, and is what freezing a body produces.
- **`Collider { CollisionShape shape; }`**, where `CollisionShape` is a plain-data tagged union of `BoxShape { halfExtents }`, `SphereShape { radius }`, `CapsuleShape { halfHeight, radius }`, `ConvexHullShape { hull }` and `MeshShape { mesh }`. It is assigned by value (`collider.shape = BoxShape{...}`) and read and written through a checked typed view (`shape.Is<BoxShape>()`, `shape.As<BoxShape>().halfExtents`; in C#, `Shape.Box.HalfExtents`), with no physics call. Identical shapes share one backend shape through a private cache keyed by value. Considered and rejected:
  - per-shape components (`BoxCollider`): switching kind is structural, and two kinds on one entity are representable;
  - a handle to a shared shape resource (Godot, Avian): editing needs a physics call, and primitives become resources to release;
  - a flat struct holding every kind's fields: meaningless fields.
- **Convex hull and mesh data** is cooked by FenrirPhysics from vertex and index data the caller supplies (a `CreateCollisionMesh`-style call), referenced by a FenrirPhysics-owned handle, and shared. Loading it from a model or asset is the caller's job; FenrirPhysics never depends on the renderer. An asset pipeline for collision data is out of scope.
- **`PhysicsMaterial`** (friction, restitution) and **`CollisionFilter`** (layer and mask): optional components with defaults.
- **`Trigger`**: a tag component on a collider's entity.
- **Compounds**: one collider per entity. A collider on an entity without its own `RigidBody` belongs to the nearest ancestor that has one.

**Ownership:**

- Static and kinematic bodies: the ECS. Kinematic bodies are driven to their `Transform` each step (pushing dynamic bodies), not teleported.
- Dynamic bodies: physics owns pose and velocity and writes them back after each step. Game code writing a dynamic body's `Transform` or velocity is a teleport or a velocity change.
- Forces and impulses: physics calls (`AddForce(entity, ...)`), queued and applied before the next step.

**Noticing changes:**

- Adds and removes: FenrirECS gains a neutral notification API (ADR-0008's rule). Physics creates and destroys bodies in batches before the step, in a deterministic order. See "How does FenrirECS report component adds and removes?".
- Field edits: each tick, physics compares each body's components against its private copy of what it last applied or wrote back (for a shape, the tag and the active kind) and pushes the differences. This also sees C# `Ref<T>` writes.

**Hierarchy and world space:** the engine defines one correct world transform (see "What world transform does the engine define?"), and the renderer moves to it too. A dynamic body under a parent is simulated independently, and physics writes back `local = parentWorld⁻¹ × world`. Kinematic and static bodies follow their world transform.

**Scale:** world scale is applied to the collision shape: non-uniform where the backend supports it for that shape, clamped to the nearest valid scale otherwise (a documented behaviour difference, ADR-0010). Changing scale rebuilds the shape.

**Teardown:** removing `RigidBody` makes its colliders static. A body left with no collider stays in the ECS, inactive in physics, with a warning. A destroyed entity's body is removed in the batch before the next step; queued events still name the stale `Entity`, whose version detects it.
