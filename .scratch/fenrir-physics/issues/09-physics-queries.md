# What does the query API look like?

Type: grilling
Status: resolved
Blocked by: 03, 07

## Question

How does game code ask the physics world questions?

- Which queries in the foundation: raycast (closest, all), shape cast, overlap (shape, point)?
- What a hit returns (entity, point, normal, distance, collider) and how the result is returned without allocating per call.
- Filtering: by layer (from "How do collision layers and filtering work?"), by excluded entities, by sensors in or out.
- Which state a query sees: the last completed step, and whether it reflects ECS changes made since.
- How a system reaches the API (through the physics world, a free function taking the app/scene).

## Answer

- **Reached through the world:** `PhysicsWorld::Of(scene)` ("What is the physics world's lifecycle?").
- **Queries in the foundation:** `Raycast` (closest) and `RaycastAll`; `ShapeCast` and `ShapeCastAll`, sweeping a `CollisionShape` value (ADR-0011's union, e.g. `world.ShapeCast(SphereShape{0.5f}, pose, direction, distance)`); `Overlap` (bodies a shape at a pose touches) and `OverlapPoint` (bodies containing a point); `ForEachContact(entity, fn)` (current contacts, from "How do contact and trigger events reach game code?"). Character-style motion tests belong to the character controller family.
- **Hits are plain data** shared with C#: `RaycastHit { Entity body; Entity collider; Vec3 point; Vec3 normal; float distance; }`; `ShapeCastHit` the same, with the contact point on the cast shape; `OverlapHit { Entity body; Entity collider; }`. No backend-only fields (triangle or face index).
- **No allocation:** closest-hit queries return `std::optional<...>` (C#: `bool Raycast(..., out RaycastHit hit)`); all-hit queries write into a caller-supplied `std::span<...>` (C#: `Span<...>`), sorted by distance, returning how many were written and stopping when it is full. No per-hit callbacks.
- **Filtering:** an optional `QueryFilter { std::uint32_t mask = all; Entity ignoreBody = none; bool hitTriggers = false; }`. The mask uses collision-layer bits ("How do collision layers and filtering work?"); triggers are skipped by default.
- **State seen:** the last completed step. ECS changes since then (a moved kinematic body, a teleport, a new collider) are not visible until the next tick's sync. Rejected: flushing pending changes before every query (a sync per query, results dependent on call order). A `Tick` system after the step sees that step.
- **Concurrency:** queries only read, so systems the scheduler runs in parallel may query one world at once, never during its sync or step (which are FenrirPhysics's own systems).
- **Rays starting inside a solid** box, sphere, capsule or hull hit at distance 0; triangle-mesh back faces are ignored. Promised behaviour, pinned by tests (ADR-0010).
