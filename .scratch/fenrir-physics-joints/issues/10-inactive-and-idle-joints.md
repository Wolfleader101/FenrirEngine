# 10: Inactive and idle joints

**What to build:** A joint naming something that is not a valid body never crashes and never loses its configuration (spec: "FenrirPhysics: edge cases"; "Testing Decisions", seam 2).

- **Inactive joints:** a joint is inactive when `bodyA` is empty, `bodyA == bodyB`, either name is not a body entity (no `RigidBody` and not a standalone static collider, a compound child, a trigger's entity), or either name is in another `EntityList` from the joint entity. It keeps its `Joint` untouched and has no backend constraint; one warning each time it goes from valid to invalid; no event; the remaining body is woken. It becomes active again by itself once both names are valid bodies. Removing a `RigidBody` but keeping its `Collider` leaves a static body, so the joint stays active.
- An inactive joint reads zeroed from `GetJointState`, excludes no collisions, and draws only the frames on its surviving bodies, in grey.
- **Idle joints:** both ends static or kinematic, or a static or kinematic `bodyA` joined to the world, are valid but never solved, silently, and start working when an end becomes dynamic.
- **Motion type changes** keep the constraint and log nothing; a kinematic end drags its dynamic partner and is never pushed.
- **Destroying a body** mid-swing removes its joints' constraints before the body; a destroyed entity never comes back (its id is versioned).
- **Inactive scenes:** nothing joint-specific; on resume the drain applies the net effect; nothing breaks, wakes or sends events while inactive.
- An `EntityList` test pins that `Entity`'s `==` compares lists, if none exists.
- The Joints (C++) scene adds key 3 destroying chain link 3.

**Blocked by:** 05 (Joints debug lines); 07 (Jointed bodies don't collide); 08 (Joint state).

**Status:** resolved

- [x] Tests: each inactive case gives one warning per valid-to-invalid transition and reactivates when both names are valid; a body removed and its `RigidBody` added back reactivates the joint; keeping only the `Collider` keeps it active.
- [x] Tests: an inactive joint reads zeroed state, excludes no collisions, and draws grey frames on its surviving bodies only.
- [x] Tests: an idle joint starts when an end becomes dynamic; motion-type switches keep the joint silently; destroying a body mid-swing does not crash; an inactive scene's joints resume correctly.
- [x] 3 gives one warning per joint, the links below fall and the inactive joints draw grey (checked by hand).
