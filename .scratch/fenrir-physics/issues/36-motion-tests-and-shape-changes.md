# What are the motion-test and sweep queries, and how does a character change shape?

Type: grilling
Status: resolved
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

From "How does a character interact with other bodies?": the inner body takes the character's shape, so a crouch changes what bodies, queries and triggers see too (Jolt sets the inner body's shape separately; both must change together). The entity's `CollisionFilter` filters both the move and the inner body, so motion-test queries for custom movement should take the character's own filter and skip its inner body (`QueryFilter::ignoreBody` accepts a character entity). Other characters block through their inner bodies, so a room-to-stand check sees them as obstacles. `pushForce` and `mass` are live fields beside the shape; a crouch changes neither.

## Answer

No ADR. The new queries are additive and follow the foundation's query rules. The shape-change rules are private behaviour plus one read-back field, and they fit ADR-0011 and ADR-0014. Of all the calls, a refused edit leaving the `Collider` ahead of what is simulated is the most surprising. It can still be changed later in one place without touching the public types, so it does not need an ADR. Glossary: **character body** amended (a shape change waits for room), **motion test** added.

- **Motion tests are general `PhysicsWorld` queries, not character-only.** They take any `CollisionShape` at any pose (scaled as a collider is), take a `QueryFilter`, see the last completed step, never allocate, and are safe from parallel systems, as the foundation's queries are.
  - Their users are custom movers (a kinematic `RigidBody` that game code moves itself, as with Avian's `cast_move`) and character logic: ledge and headroom checks, or "can I dash 5 m here?".
  - A character still moves only through `velocity`. Game code may teleport it (a position write) to a pose a motion test found safe.
  - Given a `MeshShape`, they find nothing, as `ShapeCast` already does (Jolt cannot cast a mesh). The doc comments say so.
- **The sweep: `MoveCast`.**

  ```cpp
  /** @brief Where a moved shape is blocked (PhysicsWorld::MoveCast). */
  struct MoveCastHit
  {
      Entity body;
      Entity collider;
      Math::Vec3 point = Math::Vec3(0.0f);  // where the shape touches the body, on the shape, world space
      Math::Vec3 normal = Math::Vec3(0.0f); // the body's surface normal, towards the shape
      float distance = 0.0f;                // how far the shape can travel along motion and stay clear (the safe distance)
  };

  [[nodiscard]] std::optional<MoveCastHit> MoveCast(const CollisionShape& shape, const Transform& pose,
                                                    const Math::Vec3& motion, const QueryFilter& filter = {}) const;
  ```

  - `motion` is a displacement (a `Vec3` whose length is how far to go), not a direction and a distance.
  - `distance` is the **safe** distance: moved that far, the shape stays a small gap (the skin) from what blocked it. No hit means the whole motion is clear.
  - It ignores bodies the shape starts inside or is moving away from, so a mover resting on a floor is not stuck at distance 0. Privately this is Jolt's `ValidateMovement` pattern rebuilt on `CastShape`: a fraction limit past 1 by the skin, hits at fraction 0 and hits moving away dropped, and the fraction backed off by the skin.
  - **The skin is fixed and private** at the character's padding (0.02 m), as ADR-0014 keeps padding private, so `MoveCast` has `ShapeCast`'s signature.
  - Closest hit only, with no `MoveCastAll`: a move needs only its first block, and `ShapeCastAll` already lists everything along a path.
  - Rejected: a safe distance on `ShapeCastHit`, which would change what `ShapeCast`'s distance 0 means; a `skin` parameter (Avian), which is more surface and leaks tuning.
- **The penetration query: `Penetrations`.**

  ```cpp
  /** @brief A body a shape overlaps, and how to get out of it (PhysicsWorld::Penetrations). */
  struct PenetrationHit
  {
      Entity body;
      Entity collider;
      Math::Vec3 point = Math::Vec3(0.0f);  // deepest point of the overlap, world space
      Math::Vec3 normal = Math::Vec3(0.0f); // unit direction that moves the shape out along the shortest path
      float depth = 0.0f;                   // how far along normal the shape must move to stop overlapping
  };

  std::size_t Penetrations(const CollisionShape& shape, const Transform& pose, std::span<PenetrationHit> hits,
                           const QueryFilter& filter = {}) const;
  ```

  - Deepest first; when there are more than `hits` holds, the deepest are written. Results are sorted, since Jolt's arrive in a varying order (determinism). Privately this is `CollideShape` with its penetration axis and depth.
  - `Overlap`, `OverlapPoint` and `OverlapHit` are unchanged, so "what is here" costs nothing more and `OverlapPoint` keeps its type.
- **The resolve helper: `Depenetrate`.**

  ```cpp
  [[nodiscard]] Math::Vec3 Depenetrate(const CollisionShape& shape, const Transform& pose,
                                       const QueryFilter& filter = {}) const;
  ```

  - It returns the offset that moves the shape out of everything it overlaps. Several overlaps are resolved together over a few private iterations (Avian's `depenetrate`), and the result is zero when nothing overlaps.
  - In a crevice the result may be partial. The doc comment says so: check with `Penetrations` when it matters.
  - It hides the iterative resolve every custom mover would otherwise write by hand.
- **Filtering like a character: `QueryFilter` gains `layers`.**

  ```cpp
  struct QueryFilter
  {
      std::uint32_t mask = ~0u;
      /** The layers the query is in: when non-zero, a body is found only if its CollisionFilter mask includes one of
       * them (the symmetric rule). 0 ignores bodies' masks. */
      std::uint32_t layers = 0u;
      Entity ignoreBody = Entity();
      bool hitTriggers = false;
  };
  ```

  - 0 keeps today's behaviour, so a ray still finds a hitbox that collides with nothing.
  - A custom move for a character filters exactly as its own move does with `{ .mask = cf.mask, .layers = cf.layers, .ignoreBody = character }` (`cf` its `CollisionFilter`, or the defaults without one). No helper function; ticket 38 may add sugar for C#.
  - It applies to every query, the foundation's included.
- **Allowed shapes:** box, sphere, capsule and convex hull. A `MeshShape` makes the character inactive (ADR-0014's rule: component kept, one warning, back once valid).
  - Which contacts count as ground is set privately per kind, measured along world +Y: the shape's bottom within its rounding. For a capsule that is its lower hemisphere (the spike's `-bounds.min.y - radius` plane), for a sphere its lower half, and for a box or hull its rounded bottom edge (their convex radius). It is recomputed whenever the shape, scale or rotation changes.
- **Scale:** the entity's world scale applies to a character's shape exactly as to any collider. A scale edit is a shape edit, with the room check below.
- **Shape changes (crouching) are refused and retried while there is no room:**
  - A shape edit (the `Collider`'s shape or the entity's world scale) is applied in step 3b, just before that tick's move, through Jolt's checked `SetShape` at a private limit (Jolt's sample uses 1.5 × the penetration slop). The spike's remaking of the character is replaced by this reshape.
  - **It fits:** the outer shape and the inner body switch together (both or neither), contacts and `CharacterState` refresh, and the move runs with the new shape.
  - **It doesn't fit:** physics keeps the old shape and tries again at every move until it fits. Bodies, queries, triggers and debug lines see the old shape meanwhile. The `Collider` keeps holding the shape game code asked for: physics never writes the `Collider` back. The diff copy holds the shape in use, so the retry is the same comparison every tick.
  - `CharacterState` gains `bool shapeBlocked`: true while the asked-for shape doesn't fit. Reset with the rest of the state when disabled or inactive.
  - What blocks: any solid body the character's `CollisionFilter` collides with, including other characters' inner bodies and loose boxes. Triggers never block.
  - Game code writes the standing shape once, when crouch is released, and the character stands as soon as there is room (Unreal's `UnCrouch` retries the same way). A headroom check ahead of time is `Penetrations` or `Overlap` with the standing shape at the pose it would have.
  - Rejected: applying the shape anyway and letting the move push the character out, which can push it through thin floors or sideways; writing the old shape back into the `Collider`, which would mean physics editing a tagged union game code owns, and game code re-asking every tick.
- **Where the centre goes:** physics shifts the character so the change happens where it should.
  - On ground (`OnGround` or `OnSteepGround` after the last move), the shape's lowest point along world +Y stays put: crouching lowers the head, and standing grows upward.
  - In the air (`InAir` or `Touching`), its highest point stays put, so crouching mid-jump tucks the feet up (Source's crouch jump), and standing in the air extends them down, refused while the ground is in the way.
  - The shift is written back in step 5 with the position, like simulated movement. It blends under interpolation, is not a teleport, and keeps `velocity` and carried velocity.
  - It is left out of `movedVelocity`, so animation speed does not spike on a crouch.
  - Without the shift, standing up around a fixed centre would push the feet into the floor and always be refused.
  - Rejected: game code shifting the `Transform` itself, which would be a teleport in the same tick as the edit.
- **Nothing else follows the shape:** `stepHeight`, `floorSnap`, `maxSlope`, `pushForce` and `mass` stay as game code set them, so a crouched character still steps 0.4 m unless game code lowers it (as in Unreal). A rotation-only change stays unchecked and is not a teleport (ticket 33): the next move pushes the shape out.
- **Shape so far:**

  ```cpp
  struct CharacterState
  {
      // ... ticket 34's fields ...
      bool onWall = false;
      bool onCeiling = false;
      bool shapeBlocked = false; // the Collider's shape (or scale) doesn't fit yet; the old one is still in use
  };
  ```
