# How do collision layers and filtering work?

Type: grilling
Status: resolved
Blocked by: 01, 03

## Question

What does game code see for deciding what collides with what, and how does it map onto the backend?

- A named-layer collision matrix (Unity-style), category/mask bits (Box2D-style), or both?
- How many layers, declared how (at app setup, data-driven), and can they change at run time?
- How does FenrirPhysics derive Jolt's object layers and broad-phase layers from it, without exposing them?
- Per-body ignore rules (e.g. a character ignoring its own ragdoll) — in the foundation or left to the feature families?

## Answer

- **Layer and mask bits, Godot-style.** `CollisionFilter { std::uint32_t layers; std::uint32_t mask; }`. A collider may be in several layers. Two colliders collide when `(A.layers & B.mask) && (B.layers & A.mask)` (symmetric, as the backend requires; the same rule as Rapier and Avian). Default: layer 0, colliding with everything. Rejected: Unity's single layer plus a global matrix, which needs extra global configuration and cannot express one object ignoring a layer.
- **The filter belongs to the body** (Godot). It is read from the body's entity (the `RigidBody` entity, or a standalone static collider's entity) and applies to all the body's solid colliders. A `CollisionFilter` on a solid child collider is ignored with a warning. Per-collider filtering for solids was rejected: it needs per-sub-shape filtering in Jolt, slower and harder to keep identical on another backend.
- **Triggers are their own backend bodies** (Jolt's sensor flag is per body), so a `Trigger` carries its own filter even as a child of a moving body, which it follows.
- **32 layers.** `PhysicsConfig` may name them (`{"Default", "Player", ...}`) and give each a default mask, for lookup (`CollisionLayers::Bit("Player")`), logs, debug data and a future editor; code may use plain constants. The set of names is fixed at `Install`.
- **Changes at run time** are field edits, applied on the next tick through the diff (ADR-0011).
- **Mapping to Jolt is private:** each distinct `(layers, mask)` in use gets its own Jolt `ObjectLayer`, and FenrirPhysics evaluates the rule itself, which avoids the 16-layer limit of Jolt's ready-made mask filter. Broad-phase layers: static, moving, trigger.
- **Per-body ignore rules** (collision groups: a character and its own ragdoll, bodies joined by a joint) are left to the constraints, ragdoll and character feature families.
- **Queries** take the same `mask` bits to choose what they hit ("What does the query API look like?").
