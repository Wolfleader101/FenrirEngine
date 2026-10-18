# What are the motion-test and sweep queries, and how does a character change shape?

Type: grilling
Status: open
Blocked by: 30, 32

## Question

The foundation deferred "character controller (including motion-test queries)" to this spec. What motion-test queries does `PhysicsWorld` gain, and how does a character crouch? Builds on [What does Jolt offer for characters?](30-what-jolt-offers-for-characters.md) and [How is a character body modelled in the ECS?](32-character-bodies-in-the-ecs.md), and follows the foundation's queries ("What does the query API look like?": plain-data hits, caller-supplied spans, `QueryFilter`, the last completed step).

- **Motion tests:** a query that sweeps a shape from a pose along a motion and reports the first blocking hit and a safe position; a penetration query (`Collide`) that reports overlaps with depth and direction. Whether they are general `PhysicsWorld` queries or character-only.
- **Shape changes:** changing a character's shape (crouch), and the check that there is room before standing back up; what happens when there is not.
- **Allowed shapes:** capsule only, or any convex shape.
- **Scale:** whether a character's world scale applies to its shape.

Facts from "What does Jolt offer for characters?": `SetShape(shape, maxPenetration)` refuses a shape that penetrates deeper than the limit (the sample uses 1.5 × penetration slop), which is the room-to-stand check; the inner body's shape is set separately. `CastShape` gives a fraction, normal and initial overlaps; `CollideShape` gives a penetration axis and depth. The padding back-off Jolt uses for a safe position is private; today's `ShapeCast`/`Overlap` report no depth or safe fraction.

From "How do other engines expose characters?": only Unreal (`UnCrouch`) and Tnua (a headroom sensor) check for room before standing up.

From "How is a character body modelled in the ECS?" (ADR-0014): a character's shape is its entity's own `Collider` (no compounds; solid child colliders are ignored with a warning), so crouching is a `Collider` edit, and the room-to-stand check is about what happens to that edit when there is no room. Its `Transform` sits at the collider's centre, so a capsule that shrinks also moves its centre down unless physics or game code shifts it; this ticket decides which. The shape follows the entity's full world rotation; scale applies as for any collider unless this ticket says otherwise. A shape kind characters cannot use makes the character inactive with one warning; this ticket names the kinds (at least triangle meshes). There is no move call, so the motion-test queries here are how game code builds custom movement (Avian's `cast_move` and `depenetrate`).

From "How does a character move each tick?": a rotation change alone is applied without a sweep and is not a teleport (the next move pushes the shape out); only a position change teleports. Which contacts count as ground is set privately from the shape (Jolt's supporting volume, the lower part of the shape), so each allowed shape kind needs a rule for it, and a shape change must update it. `stepHeight` (0.4 m) and `floorSnap` (0.5 m) are live fields; a crouch that changes the shape's height interacts with both. Shape edits apply at the next move, in step 3b.

From "What does game code read back from a character, and which events?": ground state, `onWall` and `onCeiling` come from the end-of-move contacts, so a shape change that refreshes contacts (Jolt's checked `SetShape`) also refreshes `CharacterState`; whether a refused stand-up is reported belongs in `CharacterState` (written each write-back, game edits overwritten) rather than an event or a new component. `ForEachContact(character)` lists end-of-move contacts with point and normal, which the motion-test queries need not duplicate.
