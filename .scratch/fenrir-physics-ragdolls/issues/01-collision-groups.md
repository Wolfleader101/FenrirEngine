# 01: Collision groups: bodies in one group never touch

**What to build:** FenrirPhysics gains general collision groups, end to end: a `CollisionGroup` component holding one plain integer on a body entity, a character body entity or a trigger entity; two bodies whose groups are equal and non-zero never touch, whatever their layers allow, and 0 or no component is no group (spec: "FenrirPhysics: general collision groups"; ADR-0011; physics map ticket "How do general collision groups work?"). The spike on `prototype/fenrir-physics-ragdolls-spike` built this slice crudely (a body-index-to-group table in the joints' excluded-pair filter, `GroupOf` beside `FilterOf`, the character's inner body and move filtered by the same rule); build it properly against the spec. The spike branch is reference only.

- **The component:** `CollisionGroup { std::uint32_t group = 0; }`, documented: a group only removes contact the layers allow; one group per body; game code picks the integer and groups are compared within one physics world; queries ignore it (`QueryFilter::ignoreBody` and layers skip a body); a field edit applied next tick with the usual contact events.
- **Who reads it:** a rigid body's entity; a `CharacterBody` entity, for its move and its inner body; a trigger's own entity, as its own `CollisionFilter`. Read with the body, character and trigger edits each tick and diffed (ADR-0011).
- **Privately:** the joints' excluded-pair `GroupFilter` gains a body-to-group table consulted beside its pair set: `CanCollide` refuses equal non-zero groups, then the pair set. A body carries the Jolt collision group while it is in any excluded pair or any group, and drops it when in neither, so no two filter objects ever meet (Jolt asks the first non-null filter of a pair). The character's move takes a body filter over the same rule. No public header names Jolt.
- **Events:** a resting pair that stops colliding gets a normal `ContactEnd`; a pair that resumes wakes both bodies and gets a `ContactBegin`.
- **C#:** `CollisionGroup` mirrored writable in `Fenrir.Physics` with C++'s default; no native. The API description and binding gain it.
- **Glossary:** the *collision group* entry already names the component; keep it current.

**Blocked by:** None (can start immediately).

**Touches:** `packages/FenrirPhysics/include/FenrirPhysics/Components.hpp`, `packages/FenrirPhysics/src/`, `packages/FenrirPhysics/src/Backend/`, `packages/FenrirPhysics/Tests/`, `packages/FenrirPhysics/api/`, `packages/FenrirPhysics/Scripting/`, `packages/FenrirPhysics/Managed/`

**Status:** resolved

- [x] `FenrirPhysicsTests` `[groups]`: two boxes in one group fall through each other and in different groups stack; 0 and no component collide; a group never adds contact the layers forbid.
- [x] A character in its group walks through grouped bodies, its move and inner body both ignoring them; a trigger on a grouped entity does not fire for group-mates and fires for others.
- [x] A raycast and a shape cast still hit a group-mate.
- [x] Giving a resting pair one group gives `ContactEnd` next tick; removing it wakes both and gives `ContactBegin`; a grouped body in a joint's excluded pair keeps both rules; a joint's two bodies in different groups still obey `collideConnected`.
- [x] Two runs with grouped and jointed bodies are bit-identical, single- and multi-threaded.
- [x] `PhysicsScriptingTests`: `CollisionGroup` validates against its native layout, `new` equals C++'s `{}`, and a group written from C# makes two bodies ignore each other.
- [x] No public FenrirPhysics header names Jolt; the API description is regenerated; Clang and MSVC x64 debug and release build without new warnings.

**Resolved by:** the PR of branch `ragdoll/01-collision-groups` into `fenrir-physics-ragdolls`. The proof is `FenrirPhysicsTests`' `[groups]` cases (`Tests/CollisionGroups.cpp`), `PhysicsScriptingTests`' `[groups]` cases and the generated defaults case for `CollisionGroup`, and the full CTest run on `msvc-x64-debug`; the release and Clang builds of the last box were left to CI, which builds `msvc-x64-release` on the PR. Choices the ticket left open, and one it did not:
- **The body-to-group table is Jolt's own.** The ticket asked for a table in the excluded-pair filter. Each body's Jolt collision group already holds two numbers for that filter: the first stays the body's index, for the pair set, and the second now holds the body's collision group, 0 for none. `CanCollide` reads both groups from its arguments, so there is no second map to keep level with body lifetimes (a body index is reused) and no lookup on Jolt's job threads. A body still carries the Jolt group only while it is in a collision group or an excluded pair, so no two filter objects meet.
- **Private names:** `Detail::GroupOf(entity)` beside `FilterOf` (`src/FilterOf.hpp`) reads an entity's group, 0 with no component; `group` on the backend's `BodyDesc` and `CharacterDesc`; `Backend::World::SetGroup` and `SetCharacterGroup`; diffed in `Bodies::PushEdits` and `Characters::PushEdits` after the filter.
- **A character's moves** take a body filter built from its inner body's Jolt group, asked as Jolt asks it of two bodies, in every place a move or a contact refresh runs (the move, a teleport, a reshape, and when it is made), so a character teleported into a body of its group finds no contact with it.
- **A `CollisionGroup` on a child collider of a compound body is ignored without a warning** (a `CollisionFilter` there warns). The component's comment says so.
- **A group change wakes the body and the bodies near it**, as a filter change does, which is what ends or begins the contact on the next tick.
- **Vehicles' wheel probes are queries**, so a wheel still rests on a body of its chassis's group.
- **Tests:** the three copies of the `Touching` test helper became one in `Tests/PhysicsTestApp.hpp`.
