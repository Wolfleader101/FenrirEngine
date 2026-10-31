# How do general collision groups work?

Type: grilling
Status: claimed
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
