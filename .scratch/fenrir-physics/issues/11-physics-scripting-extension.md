# What does the physics scripting extension give C#?

Type: grilling
Status: resolved
Blocked by: 04, 08, 09, 16, 18

## Question

What does FenrirPhysics's scripting extension (ADR-0009) give C# code?

- Which components C# sees (body, colliders) and which fields are writable from C#.
- Which native functions: queries, applying forces and impulses, setting velocities, teleporting a body.
- Which events: the contact and trigger events from "How do contact and trigger events reach game code?".
- Native functions may read and write component data but make no structural changes (ADR-0009): which physics operations does that rule out, and is that acceptable?
- Where the C# contract project lives, and how it is built only when scripting is enabled.

## Answer

- **Components, all writable from C#:** `RigidBody`, `Collider` (its `CollisionShape` per "How do C# components hold a tagged union?"), `PhysicsMaterial`, `CollisionFilter`, `Trigger`, `ContactEvents`. Writing a velocity or a `Transform` means what it does in C++ (velocity change, teleport), found by the per-tick diff.
- **`Trigger` and `ContactEvents` carry `bool enabled = true`** (this refines "How is physics modelled in the ECS?", which had them as empty tags, for C++ and C# alike): `ComponentType::Native<T>()` refuses empty types, so a tag cannot reach C#; and switching a trigger or contact reporting off and on becomes a field edit rather than a structural change, as ADR-0011 prefers.
- **Native functions**, in a static class `Fenrir.Physics.PhysicsWorld`, each on the active scene's world:
  - forces: `AddForce`, `AddImpulse`, `AddTorque`, `AddAngularImpulse` (by `Entity`, queued for the next step); teleports and velocities are field writes, not functions;
  - queries: `Raycast` / `RaycastAll`, `ShapeCast` / `ShapeCastAll`, `Overlap`, `OverlapPoint`; the all-hit forms fill a `Span<T>` and return the count;
  - state: `ContactsOf(entity, Span<Contact>)`, `IsSleeping`, `Bounds`;
  - world: `Gravity` (get and set), `SetDebugDraw(flags)`;
  - collision meshes: `CreateCollisionMesh(ReadOnlySpan<Vector3> vertices, ReadOnlySpan<uint> indices)` returning a handle, `ReleaseCollisionMesh`.
- **Events:** `ContactBegin`, `ContactEnd`, `TriggerEnter`, `TriggerExit` registered as native events with C# mirror structs, read through `EventReader<T>` fields (ADR-0012).
- **The no-structural-changes rule rules nothing out:** forces are queued; queries, debug toggles and gravity touch only the world; cooking a mesh makes a physics resource; creating a body from C# is adding components through C#'s normal deferred changes. Caveat: cooking runs synchronously inside the calling system.
- **No world, no body, or a released mesh handle:** queries report no hits, forces do nothing, functions return `false` or zero; no exception, matching C++. A warning is logged once per call site in debug builds.
- **Where it lives:** the C# contract assembly `Fenrir.Physics` in `packages/FenrirPhysics/Managed/Fenrir.Physics/` (component mirrors, `CollisionShape`, events, `[NativeFunction]` declarations); the C++ native functions in a separate static library `FenrirPhysicsScripting`, so FenrirPhysics never links FenrirScripting. Both build only when `FENRIR_ENABLE_PHYSICS` and `FENRIR_ENABLE_SCRIPTING` are on. An app adds `Physics::ScriptingExtension()` to its scripting config (ADR-0009).
