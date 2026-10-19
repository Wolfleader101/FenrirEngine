# 02: Motion tests and QueryFilter::layers

**What to build:** Game code can build its own movement: `PhysicsWorld` gains three general motion tests for any shape, and `QueryFilter` gains `layers` so a query can filter exactly as a body's `CollisionFilter` does (spec: "FenrirPhysics: motion-test queries", "the Jolt backend"). None of it needs characters; it lands beside the character work.

- **`MoveCast(shape, pose, motion, filter)`** returns the closest `MoveCastHit` or nothing. `motion` is a displacement; `distance` is the **safe** distance, leaving a private fixed skin (0.02 m, the character's padding) between the shape and what blocked it. `point` is where the shape touches the body, `normal` the body's surface normal towards the shape. Bodies the shape starts inside or is moving away from are ignored, so a shape resting on the ground is not stuck at 0. Backend: Jolt's `ValidateMovement` pattern rebuilt on `CastShape` (fraction limit past 1 by the skin, fraction-0 hits and hits moving away dropped, the fraction backed off by the skin).
- **`Penetrations(shape, pose, hits, filter)`** writes `PenetrationHit`s (`point` the deepest point, `normal` the unit way out, `depth`), deepest first; with more overlaps than `hits` holds, the deepest are written. Sorted, since the backend's arrive in varying order. `Overlap`, `OverlapPoint` and `OverlapHit` are unchanged.
- **`Depenetrate(shape, pose, filter)`** returns the offset that frees the shape from everything it overlaps, over a few private iterations; zero when clear. In a crevice it may be partial; the doc comment says to check with `Penetrations`.
- All three take any `CollisionShape` scaled as a collider is, see the last completed step, never allocate, are safe from parallel systems, and find nothing for a `MeshShape` (documented, as on `ShapeCast`).
- **`QueryFilter::layers`** sits between `mask` and `ignoreBody`: when non-zero, a body is found only if its `CollisionFilter` mask includes one of them; 0 ignores masks, keeping every existing query's results. The doc comments on `layers` and `MoveCast` show filtering like a body: `{ .mask = cf.mask, .layers = cf.layers, .ignoreBody = me }`. No helper.
- Rejected: a safe distance on `ShapeCastHit`, a `skin` parameter, `MoveCastAll`.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

- [ ] `FenrirPhysicsTests`: `MoveCast` against a wall stops at the skin's gap; a clear motion returns nothing; a shape resting on the ground moving along it is not stuck at 0; grazing hits and starting in contact behave as documented.
- [ ] `Penetrations` lists overlaps deepest first with direction and depth, and with a short span writes the deepest; `Depenetrate` frees one overlap and two at once, and is zero when clear.
- [ ] A `MeshShape` finds nothing in all three; scale on the query shape applies as on a collider.
- [ ] `QueryFilter::layers` filters by bodies' masks; `layers = 0` gives today's results for every foundation query (rays, shape casts, overlaps).
- [ ] No public FenrirPhysics header names Jolt; Clang and MSVC x64 debug and release presets build without new warnings.
