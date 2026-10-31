# How do general collision groups work?

Type: grilling
Status: resolved
Blocked by: 49, 50

## Question

How does game code make any set of bodies ignore each other? The foundation left collision groups to the feature families ("How do collision layers and filtering work?"); the joints spec stops only the two bodies a joint names from colliding (`Joint::collideConnected`, "How do jointed bodies stop colliding?"), privately through one Jolt `GroupFilter` over a pair table "with room for general collision groups"; characters and vehicles were checked and need none. Ragdolls do: a character body and its own ragdoll must not touch, the parts of one ragdoll must not jam against each other beyond the parent–child pairs its joints already exclude (a thigh against the opposite thigh, a hand against the chest), and a chain should be able to exclude itself, while two ragdolls still collide with each other. Decide, within ADR-0011 (plain-data components physics diffs; switching is a field edit) and the layer rule (a group only removes contact the layers allow, as `collideConnected` does):

- **The model:** a `CollisionGroup` component on a body entity holding a group id, where bodies with the same id ignore each other (Jolt's `GroupID`; one group per body); a group id plus a membership bit mask inside it (Jolt's `SubGroupID` with a `GroupFilterTable`: bodies in the same group ignore each other unless their sub-groups are allowed, or the reverse); or an explicit pair list game code adds to (`IgnoreCollision(a, b)`, as Unity and Godot do). What a character body (not a rigid body, ADR-0014) and a trigger do with a group.
- **Default and scope:** no group by default (every body collides by its layers); whether a group id is a plain integer game code picks (as layer bits are) or handed out by physics (`PhysicsWorld::NewCollisionGroup()`), and whether ids are per world or per app.
- **Inside a group:** whether a group means "ignore each other" only, or also carries a "collide within, except these pairs" mode a ragdoll needs when its parts should mostly collide (fingers against a chest) but some pairs may not; how the joints' pair exclusions and a group compose (both apply; a joint's pair exclusion still works for bodies in different groups).
- **Queries and events:** groups ignore queries, as `collideConnected` does (Jolt's groups do not filter queries), unless a reason emerges; no events for a pair a group excludes.
- **Live edits:** changing a body's group takes effect on the next tick without a rebuild; what happens to resting contacts when a group is removed (they collide again, waking both).
- **Behind the seam:** how it maps onto Jolt's one `CollisionGroup` per body alongside the joints' pair table (one combined private `GroupFilter`, with the pair table and the group table consulted together), and what a second backend would need.
- **C#:** the component mirrored writable, nothing more, unless physics hands out ids.
- **Vocabulary:** *collision group* is already reserved in `GLOSSARY.md` as "bodies that ignore each other", distinct from a *collision layer*; confirm the term and the words to avoid.

Probably no ADR, unless the pair-list model is chosen (hard to reverse into a component).

Facts from the joints spec: Jolt gives each body at most one `CollisionGroup` (a `GroupFilter`, a `GroupID` and a `SubGroupID`); two bodies collide when their filters agree, and FenrirPhysics's private `JoinedPairs` filter sets every jointed body's `GroupID` to its body index and keeps a hash set of excluded pairs patched before the step. Queries ignore collision groups.

## Answer

No ADR. Vocabulary is in `GLOSSARY.md`: *collision group* (already reserved) now names the `CollisionGroup` component.

- **The model:** a `CollisionGroup` component on a body entity holding one `std::uint32_t group`. Two bodies whose groups are equal and non-zero never touch; `0`, and no component, is no group. One group per body (Jolt allows one `CollisionGroup` per body). A pair list (`IgnoreCollision(a, b)`) was rejected: a call, not state, so it does not survive a body rebuild or a scene save; a group plus sub-group mask was deferred (below).
- **Who picks the id:** game code, as plain integers compared only within one physics world (a ragdoll builder can use its skeleton entity's id). Physics hands out nothing: no `NewCollisionGroup()` native, nothing to mirror.
- **Inside a group:** a group means only "ignore each other". No "collide within, except these pairs" mode now. A ragdoll's default is every part and its character body in one group (stable, no self-jamming); a self-colliding ragdoll (arms stopped by the torso) is listed as later work, reached by leaving the parts ungrouped and relying on the joints' parent–child exclusions, or by a later within-group field backed by Jolt's `GroupFilterTable`, which needs a pose-based overlap pass at build time (the ragdoll spec's problem).
- **Characters and triggers:** a `CollisionGroup` on a `CharacterBody` entity applies to its move and its inner body, so a character and its own ragdoll ignore each other. A trigger reads the `CollisionGroup` of its own entity, as it reads its own `CollisionFilter`, so a character's trigger can skip its group-mates.
- **Queries:** ignore groups, as they ignore `collideConnected` (Jolt's groups never filter queries); `QueryFilter::ignoreBody` and layers skip a body. A group-aware query filter is addable if a need appears.
- **Live edits and events:** changing or removing a group is a field edit applied next tick, no rebuild; a pair that stops colliding gets a normal `ContactEnd`, and a pair that resumes wakes both bodies and gets a `ContactBegin`, the rules `collideConnected` toggles follow.
- **Behind the seam:** one combined private `GroupFilter` (the joints spec's `JoinedPairs`, which already sets every jointed body's Jolt `GroupID` to its body index, grows a body-to-group table consulted beside its pair set); every body with a group or a joint carries that filter. A second backend needs a per-pair "can collide" hook, which every candidate has.
- **C#:** `CollisionGroup` mirrored writable with C++'s default; no natives.
- **Words to avoid:** ignore list, exclusion, sub-group, collision exception (Godot's).
