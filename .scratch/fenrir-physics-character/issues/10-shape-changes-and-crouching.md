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

**Status:** ready-for-agent

- [ ] Tests: a crouch on the ground fits and lowers the head with the feet in place; in the air it tucks the feet up with the head in place.
- [ ] Tests: a stand-up under a box is blocked with `shapeBlocked`, the `Collider` unchanged and queries seeing the crouched shape; removing the box lets it stand by itself on the next move; a trigger does not block.
- [ ] Tests: a scale edit is checked the same way; box, sphere, capsule and hull characters move; the centre shift is not a teleport and not in `movedVelocity`.
- [ ] The tunnel crouch holds `shapeBlocked` until the exit, the roof-gap box blocks and the trigger does not, the crouch-jump clears the ledge, and G walks with the larger shape (checked by hand).
