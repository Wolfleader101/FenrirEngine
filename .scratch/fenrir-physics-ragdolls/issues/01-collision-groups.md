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

**Status:** ready-for-agent

- [ ] `FenrirPhysicsTests` `[groups]`: two boxes in one group fall through each other and in different groups stack; 0 and no component collide; a group never adds contact the layers forbid.
- [ ] A character in its group walks through grouped bodies, its move and inner body both ignoring them; a trigger on a grouped entity does not fire for group-mates and fires for others.
- [ ] A raycast and a shape cast still hit a group-mate.
- [ ] Giving a resting pair one group gives `ContactEnd` next tick; removing it wakes both and gives `ContactBegin`; a grouped body in a joint's excluded pair keeps both rules; a joint's two bodies in different groups still obey `collideConnected`.
- [ ] Two runs with grouped and jointed bodies are bit-identical, single- and multi-threaded.
- [ ] `PhysicsScriptingTests`: `CollisionGroup` validates against its native layout, `new` equals C++'s `{}`, and a group written from C# makes two bodies ignore each other.
- [ ] No public FenrirPhysics header names Jolt; the API description is regenerated; Clang and MSVC x64 debug and release build without new warnings.
