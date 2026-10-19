# 10: Shape changes and crouching

**What to build:** Game code crouches by editing the character's `Collider` once, and stands back up by writing the standing shape once; the stand-up waits until it fits (spec: "FenrirPhysics: shape changes and crouching", "the Jolt backend" (checked reshape)).

- **Allowed shapes:** box, sphere, capsule and convex hull (a `MeshShape` is ticket 06's inactive rule). World scale applies as to any collider; a scale edit is a shape edit.
- **Applied in step 3b**, just before that tick's move, through the backend's checked reshape at a private penetration limit (1.5 × the penetration slop), replacing the spike's remake:
  - **fits:** the outer shape and the inner body switch together, contacts and `state` refresh, and the move runs with the new shape;
  - **doesn't fit:** the old shape is kept and tried again at every move; bodies, queries and triggers see the old shape; `state.shapeBlocked` is true while it waits. The `Collider` keeps what game code asked for; physics never writes it. The diff copy holds the shape in use.
- **What blocks:** any solid body the character's `CollisionFilter` collides with, including other characters. Triggers never block.
- **Where the centre goes:** on ground (`OnGround` or `OnSteepGround`) the shape's lowest point along +Y stays put; in the air its highest point does (standing in the air is refused while the ground is in the way). The shift is written back with the position, blends, is not a teleport, keeps `velocity` and carried velocity, and is left out of `movedVelocity`.
- **Nothing else follows the shape:** `stepHeight`, `floorSnap`, `maxSlope`, `pushForce` and `mass` stay as set. The private ground rule is recomputed. A rotation-only change stays unchecked.
- **DemoApp:** a crouch tunnel with a box over a roof gap and a trigger volume; a crouch-jump ledge; X toggles crouch, G scales by 1.5× and back. The autopilot crouches into the tunnel and toggles crouch off inside, so `shapeBlocked` shows.

**Blocked by:** 03 (Character tracer bullet).

**Status:** resolved, but for the DemoApp hand check, which is the user's (built test-first on branch `claude/character-shape-changes-c645d0`). Where the build settled something the ticket left open:

- The shape edit is diffed and applied in 3b, just before each character's move, against the shape and world scale physics last gave the character (its diff copy), not in step 2: nothing else in step 2 touches characters yet, and ticket 05 adds the teleport and rotation diff there.
- The backend's checked reshape moves the character by the shift first and calls Jolt's `SetShape` there, at 1.5 × the penetration slop, putting it back where it was if the shape is refused; on a fit the inner body's shape and the supporting volume (the private ground rule) follow, and the bodies around the old and new shapes are woken. The shift comes from each shape's farthest point along world ±Y, rounding included, so it holds for a rotated or scaled shape too.
- The move after a reshape measures `movedVelocity` from the shifted position, which is how the shift stays out of it. That the shift is not a teleport is tested only as far as this build has teleports: `velocity` is kept and the ground stays; ticket 05's interpolation and carried velocity add the rest of the meaning.
- A `Collider` edited to a `MeshShape` (or a hull whose mesh is released) is left alone here, neither applied nor blocked: making the character inactive is ticket 06's.
- The `Same(CollisionShape, CollisionShape)` diff comparison moved into `Same.hpp`, so the character diff shares it.
- The DemoApp's tunnel has a loose 1 kg box lying across a 1 m gap in its roof, and its trigger sits just past the far end, clear of the ground and walls, so the waiting stand-up fits inside it. The ledge is 1.6 m: a standing jump lifts the feet about 1.3 m, and the autopilot crouches as soon as the jump leaves the ground. The character's look shows the shape in use.

- [x] Tests: a crouch on the ground fits and lowers the head with the feet in place; in the air it tucks the feet up with the head in place.
- [x] Tests: a stand-up under a box is blocked with `shapeBlocked`, the `Collider` unchanged and queries seeing the crouched shape; removing the box lets it stand by itself on the next move; a trigger does not block.
- [x] Tests: a scale edit is checked the same way; box, sphere, capsule and hull characters move; the centre shift is not a teleport and not in `movedVelocity`.
- [ ] The tunnel crouch holds `shapeBlocked` until the exit, the roof-gap box blocks and the trigger does not, the crouch-jump clears the ledge, and G walks with the larger shape (checked by hand).
