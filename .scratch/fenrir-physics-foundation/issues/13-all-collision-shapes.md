# 13: All collision shapes and collision meshes

**What to build:** Colliders can be any collision shape: box, sphere, capsule, convex hull or triangle mesh (spec: "FenrirPhysics: components"; ADR-0011). Hull and mesh refer to a collision mesh that FenrirPhysics cooks once from vertex and index data the caller supplies (`CreateCollisionMesh`), shares between colliders and frees when released. Identical primitive shapes share one backend shape through a private cache keyed by value. World scale is baked into the shape (non-uniform where the backend allows, clamped otherwise, documented), and a box's convex radius is clamped for thin boxes.

**Blocked by:** 12 (Physics tracer bullet).

**Status:** ready-for-agent

- [ ] `CapsuleShape`, `ConvexHullShape` and `MeshShape` join the union with checked views; tests drop each onto the ground and check where it rests.
- [ ] A collision mesh made once is used by several colliders; releasing it while in use leaves those bodies inactive with a warning; a released handle is never reused.
- [ ] Changing a shape's dimensions or the entity's scale rebuilds the backend shape on the next tick (test); a very thin box does not assert.
- [ ] Many colliders with identical primitive shapes share one backend shape (observable through a diagnostic or test hook that stays private to FenrirPhysics's tests).
- [ ] The DemoApp can build a mesh collider from a model's vertex data without FenrirPhysics depending on the renderer.
