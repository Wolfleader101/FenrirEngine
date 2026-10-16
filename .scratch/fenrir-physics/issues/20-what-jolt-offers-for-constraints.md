# What does Jolt offer for constraints?

Type: research
Status: resolved
Blocked by: none

## Question

What do JoltPhysics v5.6.0's constraints (the version pinned in "How does Jolt join the build?") offer to, and require of, FenrirPhysics, in the areas where those facts constrain the joints spec? Answer from Jolt's own source, docs and samples, pinned to v5.6.0:

- **Kinds:** every `TwoBodyConstraint` kind (fixed, point, distance, hinge, slider, cone, swing-twist, six-DOF, path, gear, rack-and-pinion, pulley) with its settings, and which are meant for ragdolls.
- **Limits, motors and springs:** `MotorSettings`, `SpringSettings`, motor states (off, velocity, position), soft limits; which kinds have which.
- **Runtime edits:** which settings can change on a live constraint and which need it destroyed and recreated; the cost of recreating.
- **Frames:** how anchors are given (`EConstraintSpace` local vs world, point plus axes), and what happens when a body's shape or centre of mass changes after creation (e.g. a compound body rebuilt).
- **Reading state:** current angle or position, the total lambda (impulse) applied last step, and whether that is how breaking is done (Jolt has no built-in breaking, as far as is known).
- **The world as one end:** `Body::sFixedToWorld`, and constraints with static or kinematic bodies.
- **Ignoring collision between connected bodies:** `GroupFilterTable` and collision groups, and how a group filter combines with object-layer filtering.
- **Lifetime and threading:** adding and removing constraints (`PhysicsSystem::AddConstraint`), what happens when a constrained body is removed, ref counting, what may be called during a step.
- **Sleep and waking:** whether a constraint wakes its bodies, and what keeps a jointed island asleep.
- **Determinism:** whether the order constraints are added in matters, and constraint priority.
- **Solver settings:** per-constraint velocity and position step overrides, and whether anything there is a tuning setting ADR-0010 would refuse.
- **Articulations:** whether Jolt has anything Featherstone- or multibody-like, or only maximal-coordinate constraints.

Put the findings on a `research/jolt-constraints` branch and link them here.

## Answer

Findings: branch `research/jolt-constraints`, `.scratch/fenrir-physics/research/jolt-constraints.md` (commit 05f7251), read against Jolt v5.6.0 (`e77f175`), every claim cited to file and line.

- **Kinds:** 12 two-body kinds. Only hinge and swing-twist are ragdoll kinds (the only two `Ragdoll` drives with motors; swing-twist is the humanoid joint). The vehicle constraint is one-body.
- **Limits, motors, springs:**
  - springs have three modes (frequency and damping by default); motors have four states (off, velocity, position, position-and-velocity);
  - soft limits only on distance, hinge, slider and six-DOF translation;
  - motors only on hinge, slider, swing-twist, six-DOF and path;
  - hinge and slider limits must include 0.
- **Runtime edits:**
  - live: enabled, priority, solver overrides, limits, springs, friction, motors and targets, a point joint's anchors;
  - destroy and recreate: bodies, frames and axes, space, swing type, ratios. Recreating is cheap, but it loses warm starting, changes the solve order and resets the zero angle.
- **Frames:** world space (captured at creation, which also fixes the zero angle) or local to each body's **centre of mass**, not its origin. A shape or centre-of-mass change is not passed on: the host must call `NotifyShapeChanged`, or the anchors drift. This matters for compound bodies built from child entities.
- **Reading state:** current values only for hinge (angle), slider (position), pulley, path, and swing-twist and six-DOF rotation. Lambda is the last step's impulse per constraint part, stale while asleep.
- **Breaking:** not built in. Compare lambda to a threshold after each step, then disable or remove.
- **World as one end:** `Body::sFixedToWorld` stands for the world. A constraint is solved only with one dynamic end and one awake end; kinematic ends are never pushed.
- **No-collide:** constraints never stop collision on their own. Each body has one `CollisionGroup`, ANDed after the layer test, so it can only remove pairs. It does not apply to queries. A body can be in only one group.
- **Lifetime:** constraints hold raw body pointers, so the host must remove a constraint before its bodies (otherwise undefined in release builds). No adding or removing during a step.
- **Sleep:** jointed dynamic bodies share an island. **No constraint edit wakes anything**; the host must call `ActivateConstraint`.
- **Determinism:** solve order is priority, then list position. A removal swaps the last constraint into its place, reordering another.
- **Solver settings:** per-constraint velocity and position step overrides (0 means the world default) raise the whole island. Whether these and priority are ADR-0010 "tuning" is left to the spec.
- **Articulations:** none. Jolt has only maximal-coordinate constraints.
