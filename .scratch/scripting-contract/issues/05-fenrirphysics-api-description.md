# 05: FenrirPhysics's API description

**What to build:** the first real API description (spec: "Stage 1", "Scope"): FenrirPhysics selects what it shares, and the tool from ticket 04 describes it. Nothing consumes the description yet; ticket 07 does.

- **`packages/FenrirPhysics/api/FenrirPhysics.api.toml`** selecting, from `Components.hpp`, `Joints.hpp`, `Characters.hpp`, `Vehicles.hpp`, `Events.hpp` and the query headers: as components every type registered today in `PhysicsScripting.cpp`'s `extension.components` (`RigidBody`, `Collider`, `PhysicsMaterial`, `CollisionFilter`, `Trigger`, `ContactEvents`, `Joint`, `CharacterBody`, `Vehicle`, `VehicleWheel`, `VehicleDifferential`, `VehicleAntiRollBar`, and the rest of that list); as events the five in `extension.events` (`ContactBegin`, `ContactEnd`, `TriggerEnter`, `TriggerExit`, `JointBroken`); as shared structs every type a native takes or returns by pointer (`QueryFilter`, `RaycastHit`, `ShapeCastHit`, `MoveCastHit`, `OverlapHit`, `PenetrationHit`, `Contact`, `Aabb`, `JointFrame`, `JointFrames`, `JointState`, and whatever else `PhysicsWorld.cs` crosses); the shape, joint-kind and vehicle-kind structs and `CollisionMesh` as the struct types the above contain; as bound types `Math::Vec3`, `Math::Quat` and `Fenrir::Transform`; `Fenrir::Entity` as a struct whose second member is a pointer (its `EntityList*`).
- **`fenrir_describe_api(FenrirPhysics ...)`** in `packages/FenrirPhysics/CMakeLists.txt`, unconditional (the description is the package's artefact whether or not scripting is on).
- **The committed `FenrirPhysics.api.json`**, reviewed by hand once against today's hand-written mappings: every field, offset and default in `PhysicsScripting.cpp`'s mappings and `Fenrir.Physics`'s structs appears with the same value; every `Detail::*Layout` stand-in corresponds to a described real type.
- Where the tool refuses a type, the fix is in the header (a default expression it should record goes to ticket 04 as a supported form; a type with a member that is not plain data is a design bug to raise, not to work around).

**Blocked by:** 04 (the tool).

**Touches:** `packages/FenrirPhysics/api/`, `packages/FenrirPhysics/CMakeLists.txt`, `packages/FenrirPhysics/include/FenrirPhysics/` (only if a header needs a default the tool can read).

**Status:** ready-for-agent

- [ ] `fenrir-describe-api` produces `FenrirPhysics.api.json` with every component, event and shared struct of the physics extension, three tagged unions (`CollisionShape`, `JointKind`, `VehicleKind`), three empty structs and the bound types; CI's clean-tree check passes.
- [ ] A hand check, recorded in the PR: each default in the description equals the C++ default initialiser and the C# field initialiser it will replace; each offset equals the `FieldOffset` or `offsetof` used today.
- [ ] No header changed except for a default expression rewritten into a recordable form, if any; FenrirPhysics tests pass unchanged.
