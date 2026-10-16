# 18: Queries

**What to build:** Game code can ask the world questions (spec: "FenrirPhysics: queries"). `PhysicsWorld` offers `Raycast`/`RaycastAll`, `ShapeCast`/`ShapeCastAll` (with a `CollisionShape` value), `Overlap`, `OverlapPoint`, `IsSleeping` and `Bounds`. Hits are plain data naming body and collider entities; closest-hit queries return an optional, all-hit queries fill a caller-supplied span sorted by distance and return the count. An optional `QueryFilter { mask, ignoreBody, hitTriggers = false }` filters. Queries see the last completed step and are safe from systems running in parallel.

**Blocked by:** 15 (Physics materials and collision layers).

**Status:** resolved

- [ ] Tests: a ray hits the nearest body with the right point, normal, distance and entities; `RaycastAll` lists hits in distance order and stops when the span is full.
- [ ] Shape casts and overlaps find the expected bodies for box and sphere shapes.
- [ ] The mask excludes layers, `ignoreBody` excludes the caster, and triggers are skipped unless asked for.
- [ ] A ray starting inside a solid primitive hits at distance 0; triangle-mesh back faces are ignored (once mesh shapes exist).
- [ ] A body moved since the last step is found at its old position until the next tick (documented behaviour, tested).
- [ ] `IsSleeping` and `Bounds` report a resting body correctly; queries in a scene without a world find nothing.

**Resolved by:** merge of impl/18-queries (74a15b5)
**Follow-ups:** the trigger-skipping test lands with ticket 17; hits naming the collider entity land with ticket 14.
