# 22: Physics in C#: components and native functions

**What to build:** C# scripts can do with physics everything C++ can (spec: "FenrirPhysics: scripting extension"; ADR-0009). A C# contract assembly `Fenrir.Physics` mirrors every physics component (writable), including `CollisionShape` with checked views and implicit conversions (the pattern from ticket 11), and declares native functions in a static class in the `Fenrir.Physics` namespace: forces, queries (all-hit forms fill a `Span<T>`), `ContactsOf`, `IsSleeping`, `Bounds`, gravity, `SetDebugDraw`, `CreateCollisionMesh`/`ReleaseCollisionMesh`. The C++ side is a separate static library `FenrirPhysicsScripting`, so FenrirPhysics never links FenrirScripting; both build only with physics and scripting on. An app adds `Physics::ScriptingExtension()` to its scripting config. With no world, no body or a released handle, functions do nothing and return `false` or zero, warning once per call site in debug builds.

**Blocked by:** 11 (Tagged-union components in C#); 13 (All collision shapes and collision meshes); 16 (Forces, gravity and motion changes); 18 (Queries); 19 (Debug lines in the DemoApp).

**Status:** resolved

- [ ] A fixture script adds `RigidBody` and `Collider` to an entity, sets a shape by value, and the body falls (scripting test).
- [ ] From C#: an impulse moves a body; a raycast and a `RaycastAll` into a `Span` return the expected entities; `IsSleeping`, `Bounds` and gravity work; a collision mesh made from C# spans is usable by a collider.
- [ ] Calls on an entity without a body, or in a scene without physics, return no hit or `false` without throwing.
- [ ] Building with scripting off builds FenrirPhysics without FenrirPhysicsScripting or the contract; FenrirPhysics never links FenrirScripting.
- [ ] The DemoApp registers the physics extension when scripting is on.

**Resolved by:** merge of impl/22-physics-csharp (c25c172)
**Note:** the extension comes from the owner (physics.ScriptingExtension(), defined in FenrirPhysicsScripting); native functions reach the owner through a file-level weak_ptr slot, so the most recently made extension wins.
