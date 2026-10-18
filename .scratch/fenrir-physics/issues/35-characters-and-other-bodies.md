# How does a character interact with other bodies?

Type: grilling
Status: open
Blocked by: 30, 32

## Question

How do characters and the rest of the physics world affect each other? Builds on [What does Jolt offer for characters?](30-what-jolt-offers-for-characters.md) and [How is a character body modelled in the ECS?](32-character-bodies-in-the-ecs.md).

- **Pushing:** a character pushing dynamic bodies, and how hard (Jolt's max strength, or a neutral setting).
- **Being pushed:** by dynamic bodies, by kinematic bodies (doors, paddles), by joints.
- **Standing on bodies:** dynamic bodies under a character (does it press down on them?), kinematic platforms (riding them).
- **Character against character:** colliding, pushing each other, cost.
- **Filtering:** a character's `CollisionFilter` layers and mask, and whether general collision groups (in the map's fog) are now needed.
- **Queries:** whether raycasts, shape casts and overlaps hit characters, and what they name.

Facts from "What does Jolt offer for characters?": pushing is an immediate impulse clamped by max strength (100 N default) and never downward; the character is pushed only during its own update. Platform velocity comes through `GetGroundVelocity`. Character against character is an app-supplied interface (brute force by default, O(N²), not thread safe). Without an inner body, queries do not see a character; with inner bodies, each character's queries skip only its own.

From "How do other engines expose characters?": pushing ranges from game code in a hit callback (Unity, Avian) to automatic impulses (Rapier, which can launch light bodies at 44× the character's speed) and mass-scaled forces (Unreal); only a dynamic-body character (Tnua) is pushed back. Raycasts see characters everywhere; Rapier's kinematic character misses fixed sensors by default; O3DE's two bodies per character is a filed bug.

From "How is a character body modelled in the ECS?" (ADR-0014): a character is always visible to bodies, queries and triggers as a kinematic body of its own `Collider` (privately Jolt's inner body), so raycasts and overlaps hit it and name its entity, and fast bodies cannot tunnel through it; each character's own moves skip its inner body. The entity's `CollisionFilter` and `PhysicsMaterial` apply as for any body. Strength and mass become live fields on `CharacterBody`. A disabled or inactive character is invisible to all of these. Characters move in physics's own phase in a deterministic order, never from a game-code call, which bounds the character-against-character order question.

From "How does a character move each tick?": characters move just before the step (`PostTick` step 3b), one at a time in ascending entity id on the app thread, never in parallel; so pushes and the character's weight on its ground (computed from the world's gravity and the character's mass) act in that same step, and character-against-character resolution follows entity order. This tick's `MoveKinematic` has already set platform velocities, so riding is added privately from the ground velocity (any ground body, including dynamic) and kept as carried velocity on leaving. A body the step moves into a character may leave it slightly overlapping, pushed out by the next move. Backend character IDs come from entities, not Jolt's global counter. Strength and mass join `maxSlope`, `stepHeight`, `floorSnap`, `stopOnSlope` and `interpolate` as live fields.

From "What does game code read back from a character, and which events?": a character's contacts are its end-of-move contacts from step 3b, reported through the existing `ContactBegin`/`ContactEnd` (opt-in by `ContactEvents` on either side, the character's entity as body and collider); its inner body's step contacts are never reported, so a body pushed into a standing character is reported by that character's next move. Character against character is one pair ordered by entity. `PhysicsWorld::ForEachContact(character)` lists the end-of-move contacts. `CharacterState` reports `groundBody`, `groundVelocity` and `groundAngularVelocity` for whatever the character stands on, dynamic bodies included; any push reading (what was pushed, how hard) this ticket adds would go into `CharacterState` or the contact events, not a new component.
