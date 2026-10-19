# 13: Motion tests in C#

**What to build:** C# systems can build custom movement with the same motion tests and filtering as C++ (spec: "FenrirPhysicsScripting and Fenrir.Physics"; ADR-0009).

- **`Fenrir.Physics` contract:** `MoveCastHit` and `PenetrationHit` in `Queries.cs` as plain structs with plain `Entity` fields, like `ShapeCastHit`; `QueryFilter` gains `public uint Layers;` between `Mask` and `IgnoreBody`, matching C++'s layout.
- **On `PhysicsWorld`**, in the foundation's query pattern: `bool MoveCast(shape, pose, motion, out MoveCastHit hit, QueryFilter? filter = null)`, `int Penetrations(shape, pose, Span<PenetrationHit> hits, QueryFilter? filter = null)` and `Vector3 Depenetrate(shape, pose, QueryFilter? filter = null)`, through three `PhysicsNative` entries; `Depenetrate`'s is `bool Depenetrate(in shape, in pose, in filter, out Vector3 offset)`, shaped like `Gravity`.
- **Doc comments** show filtering like a body, `new QueryFilter { Mask = cf.Mask, Layers = cf.Layers, IgnoreBody = me }`; no filter sugar.
- **Robustness:** with no physics world, `MoveCast` returns `false`, `Penetrations` 0 and `Depenetrate` zero, each with the debug warning once per native site; nothing throws. In `PhysicsWorld`'s remarks.
- **`FenrirPhysicsScripting`:** the `QueryFilter` mirror gains `layers`, the hit structs are mapped, and the three natives are registered. FenrirPhysics still names nothing from Iris.

**Blocked by:** 02 (Motion tests and QueryFilter::layers).

**Status:** ready-for-agent

- [ ] `PhysicsScriptingTests`: `MoveCastHit`, `PenetrationHit` and the new `QueryFilter` validate against their native layouts.
- [ ] The three motion tests from a C# system match C++ for the same scene; `Layers` filters as in C++, and 0 keeps existing C# query results.
- [ ] With no world, the three return `false`, 0 and zero with one warning each, and do not throw.
