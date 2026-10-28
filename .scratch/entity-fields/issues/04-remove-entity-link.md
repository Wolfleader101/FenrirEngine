# 04: Remove `EntityLink`: a stored entity reference is 4 bytes

**What to decide, then build:** the spec's "Removing `EntityLink` entirely" section. `EntityLink` exists because a component stores C++ `Fenrir::Entity`'s 16 bytes (id and `EntityList*`). Removing it means a component stores 4 bytes, one of two ways; the user picks, and the ticket is then built as that option says. Either option supersedes tickets 01 to 03 (mark them `wontfix`; stop ticket 01's session if it is still running).

**Option B: `Fenrir::Entity` becomes 4 bytes, the list from context.**

- `packages/FenrirECS/include/FenrirECS/Entity.hpp`, `src/Entity.cpp`, `EntityList.hpp`/`.cpp`: drop `m_entityList`; a current `EntityList` (thread-local; set by `Scene` around its systems, and by an `EntityList` made when none is current, restored when it is destroyed) serves `AddComponent`, `GetComponent`, `HasComponent`, `IsValid`, the hierarchy calls; `operator==` compares ids. The 1,755 call sites stand. Parallel systems: one scene per thread, asserted.
- `packages/FenrirECS/api/FenrirECS.api.toml`: `Fenrir::Entity` is a `bound-type`; `FenrirScripting.binding-defaults.toml` binds it as `Entity` (as `Fenrir::EntityValue` is, `wrapper = "u32"`), and the `EntityValue` bound type goes if nothing else needs it. `EntityLink.cs`, `Native.EntityLinkOwner` and the built-in binding's `[names]` entries for `Entity` go.
- Regenerate everything; the holders show `public Entity BodyA = Entity.None;` (the bound type's `default`).

**Option C: components store `Fenrir::EntityId`; `Fenrir::Entity` keeps its pointer and API. (Recommended.)**

- `packages/FenrirECS/include/FenrirECS/EntityId.hpp` (or in `Entity.hpp`): a 4-byte `EntityId` (the `entt::entity` value, `Null` as `~0u`), with `Entity::Id()`, implicit conversion from `Entity`, and `EntityList::GetEntity(EntityId)` resolving one. `Relationship`'s four fields and the 47 fields in physics (`Events.hpp`, `Queries.hpp`, `Vehicles.hpp`, `PhysicsWorld.hpp`, `Joints.hpp`, `Characters.hpp`), DebugDraw and SpawnNodes retype to `EntityId`.
- The 160 sites that call a method on an entity read out of a field resolve it first (`entities.GetEntity(joint.bodyA).GetComponent<T>()`): `FenrirECS/src/Entity.cpp` and `EntityList.cpp` (hierarchy), `FenrirPhysics/src` (`Joints.cpp`, `JointState.cpp`, `Contacts.cpp`, characters, vehicles), physics's tests, FenrirScripting's tests, `examples/PhysicsDemo/src` (the user checks the examples).
- `FenrirECS.api.toml`: `Fenrir::EntityId` is a `bound-type` bound as `Entity` in `FenrirScripting.binding-defaults.toml` (`constructor = ["value"]`, `wrapper = "u32"`, `default = "Entity.None"`); `Fenrir::Entity` is no longer selected or nested anywhere (`private-members` entries for it go). `EntityLink.cs`, `Native.EntityLinkOwner` and the `[names]` entries go; `EntityValue` goes if `EntityId` replaces it in the natives.
- Regenerate everything; the holders show `public Entity BodyA = Entity.None;`. Scripting's link tests (`Links.cs`, `Registry.cs`) and the examples' scripts use `Entity` and `Entity.None`.

Both: an ADR under `docs/adr/` (a stored entity reference is an id; how it is resolved), the glossary's "Entity link" entry replaced, the physics compile-fail tests still passing, every scripting test passing, both generators no-ops.

**Blocked by:** none

**Touches:** `packages/FenrirECS/`, `packages/FenrirPhysics/`, `packages/FenrirScripting/`, `packages/FenrirDebugDraw/`, `packages/FenrirAssets/include/FenrirAssets/SpawnNodes.hpp`, `examples/PhysicsDemo/`, `docs/adr/`, `GLOSSARY.md`

**Status:** ready-for-human

- [ ] The user has chosen B or C on this ticket (a Comments line), and tickets 01 to 03 are marked `wontfix` if so.
- [ ] `grep -rn "EntityLink" packages examples` finds nothing; `grep -rn "Entity::m_entityList\|EntityList\* m_entityList" packages/FenrirECS` finds nothing under B, or no `Entity` field in a shared header under C.
- [ ] Every scripting and ECS test passes under the msvc-x64-debug preset; both generators are no-ops; the ADR is recorded.

## Comments

Filed 2026-10-28 from the user's request on the spec: EntityLink should not be needed if the engine moves forward, and C++ `Entity` may not need its list pointer to keep `GetComponent<T>()` and friends. The spec's survey gives the figures; the recommendation is C.
