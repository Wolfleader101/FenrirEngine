# How does a character interact with other bodies?

Type: grilling
Status: resolved
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

## Answer

No ADR: every call is a live field or private behaviour that follows from ADR-0014's kinematic inner body, and the one amendment (contact events, below) can be changed later without breaking callers. Glossary: **character body** amended (it pushes with a limited force and presses on its ground; only moving kinematic bodies push it). General collision groups stay in "Not yet specified": layers are enough for characters.

Facts checked in Jolt v5.6.0 (`CharacterVirtual.cpp`) and the spike's backend: Jolt places the inner body with `SetPositionAndRotation` after each move, so the step sees it with zero velocity; kinematic bodies never collide with it in the step; a push's impulse aims for `0.9 ×` the closing speed at the contact, so a light body is never launched faster than the character; the spike filters each move through the inner body's own object layer.

- **Pushing dynamic bodies:** Jolt's model. A move that hits a dynamic body gives it an impulse towards the character's speed at the contact point, clamped by a force cap and never downward. Static and kinematic bodies are never pushed. Two live `CharacterBody` fields, each switched off by 0:
  - `pushForce = 100.0f` (N; the most force a push applies, Jolt's max strength). A 1 kg box keeps pace (the spike); a 20 kg crate with friction 0.2 accelerates at about 3 m/s²; a 100 kg crate with friction 0.5 does not move.
  - `mass = 70.0f` (kg; used only for the character's weight on its ground).

  No push reading in `CharacterState`: contact events and `ForEachContact` already name what was touched. Rejected: mass-ratio pushing (Unreal), a push callback (Unity, Avian; contact events already serve), and a "neutral" setting such as a maximum pushable mass (it hides that friction matters).
- **Standing on bodies:** each move that ends on ground presses the ground body with `mass × gravity × dt` at the ground point (the world's gravity, scaled by how the ground normal faces it), so a character tips a seesaw or sinks a raft. Riding any ground body is ticket 33's. A stated limit: a body resting *on* a character (a box on its head) is not carried by friction, because the inner body is placed with zero velocity; it slips or is pushed out. A jump still pushes it up.
- **Being pushed by dynamic bodies:** never. In the step a dynamic body meets the inner body as an immovable kinematic wall and stops or bounces; the character stays put. Knockback is game code writing `velocity` (from a contact event). A physically shoved character is a dynamic `RigidBody` kept upright by game code (ADR-0014's rejected option). Rejected: recording step impacts and adding them to the next move (Unreal's mass-scaled shove): more surface, more tuning.
- **Being pushed by kinematic bodies** (doors, paddles, crushers): this tick's `MoveKinematic` ran before step 3b, so the move treats a kinematic body as a surface moving at its velocity, and the character is shoved along at exactly the body's speed into it, whatever its mass. Under ticket 33's trim (relative to the surface's velocity) that speed shows in the written-back `velocity` and lasts until game code overwrites it. The spec states two limits: a fast body may overlap the character for a tick until the next move pushes it out, and crushing (caught between a door and a wall) is not detected; the character stays overlapped and stuck, and game code can find it from `onWall` and `ForEachContact`.
- **Joints:** a character is not a body a joint can name. A `Joint` naming a character entity as `bodyA` or `bodyB` is inactive under "What happens at a joint's edges?" (component kept, one warning, back once valid). Grabbing and carrying are game code's.
- **Character against character:** characters block each other only through their inner bodies, found by the broad phase; Jolt's `CharacterVsCharacterCollision` interface (O(N²), not thread safe) is not used, as it would find each pair twice. Neither pushes the other (pushes go to dynamic bodies only). In entity order, a later character sees an earlier one's new position and the earlier one sees the later one's old position. Privately (Jolt's `OnAdjustBodyVelocity`), a move sees another character's inner body moving at that character's last `movedVelocity`, so standing on a character rides it and following one behaves like a moving wall; the step still sees zero velocity.
- **Contact events (amends ticket 34):** a body can bounce off the inner body during the step and be gone before the next move, so move contacts alone would miss it. Contacts between a character and a dynamic body are the union of the move's end-of-move contacts and the inner body's step contacts: the pair touches while either source has it, `ContactBegin` comes from whichever source sees it first (with that source's `relativeSpeed`), `ContactEnd` when neither has it. Still one pair per entity pair, sorted in step 6. Contacts with static and kinematic bodies and other characters still come from moves only (the step never finds them).
- **Filtering:** the entity's `CollisionFilter` is both the inner body's filter and the move's, under the one symmetric rule: a character ignoring layer X walks through X's bodies, and X's bodies, triggers and queries masking it out pass through it. Layers cover teams passing through each other, player against enemy, and debris to walk through. Only per-instance cases (a character and its own ragdoll, the vehicle it sits in) need collision groups, and the character spec has none.
- **`PhysicsMaterial`:** belongs to the inner body only: the friction and restitution other bodies feel when they hit or rest on the character. No material ever changes how a character moves (Jolt's `CharacterVirtual` ignores friction); ice is game code reading `state.groundCollider`'s material and easing `velocity`. The spike ignored it, so it needs building.
- **Queries:** as ADR-0014 says, raycasts, shape casts and overlaps hit characters, naming the character's entity as body and collider, at the inner body's pose after the last step (so after that tick's move). `QueryFilter::ignoreBody` accepts a character's entity and skips its inner body (a character's own aim ray). Hits carry no character flag; game code checks the entity for `CharacterBody`.
- **Shape so far:**

  ```cpp
  struct CharacterBody
  {
      Math::Vec3 velocity = Math::Vec3(0.0f); // world space, m/s; written back trimmed by hits
      float maxSlope = 0.785f;
      float stepHeight = 0.4f;
      float floorSnap = 0.5f;
      float pushForce = 100.0f;               // N; most force a push applies; 0 turns pushing off
      float mass = 70.0f;                     // kg; weight on the ground; 0 turns it off
      bool stopOnSlope = true;
      bool interpolate = true;
      bool enabled = true;
      CharacterState state;                   // read-back, overwritten each write-back
  };
  ```
