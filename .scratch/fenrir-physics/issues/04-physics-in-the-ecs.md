# How is physics modelled in the ECS?

Type: grilling
Status: open
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
