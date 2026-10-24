# What happens at a joint's edges?

Type: grilling
Status: resolved
Blocked by: 22

## Question

How does a joint behave when the bodies around it change? Builds on [How are joints modelled in the ECS?](22-joints-in-the-ecs.md) and the foundation's sync rules (component watches drained in a batch, diffs in a stable entity order, `PostTick` sequence).

- **A body goes away:** its entity destroyed, its `RigidBody` or `Collider` removed, its motion type changed to static; is the joint kept dormant, dropped with a warning, or removed?
- **Invalid joints:** naming an entity with no body, itself, an entity in another scene, or two static bodies.
- **Teleports:** writing one end's `Transform`; does the other end follow, snap, or get pulled over the next steps?
- **Reparenting and compound rebuilds:** a body's shape or child colliders change, moving its centre of mass under the joint's anchor.
- **Sleep:** whether editing a joint or its motor wakes its bodies.
- **Order and determinism:** where joints are created, diffed and destroyed in the `PostTick` sequence relative to bodies, and in what order.
- **Inactive scenes:** nothing new expected (the world pauses), but confirm.

Facts from "What does Jolt offer for constraints?": the host must remove a constraint before either body (undefined otherwise in release builds); shape or centre-of-mass changes need `NotifyShapeChanged` or the anchors drift; no constraint edit wakes the bodies; a removal reorders another constraint in the solve order; a constraint is solved only with one dynamic, awake end.

From "How are joints modelled in the ECS?" (ADR-0013): joints name body entities only, an empty `bodyB` is the world, and the joint entity may be one of its bodies (so destroying that body also destroys the joint). Frames are local to each body's origin and follow world scale; physics owns the private body-to-joints index and removes constraints before bodies.

From "What joint state can game code read, and how do joints break?" (24): breaking sets `enabled = false` in a new `PostTick` step 5b, and setting `enabled = true` repairs. Decide here whether a repair, or lowering a sleeping joint's `breakForce`/`breakTorque`, wakes its bodies (a sleeping joint is never checked, so without a wake a lowered threshold waits until something else wakes it). `PhysicsWorld::JointState` returns a zeroed state for an entity with no backend joint: say whether an invalid or dormant joint (a body gone) counts as that. A body removed between a break and the next step still leaves the queued `JointBroken` naming it, as stale entities do in contact events.

## Answer

No ADR: every call keeps ADR-0011 and ADR-0013 (physics makes no structural ECS changes, constraints go before bodies) and is private or reversible by adding behaviour. Glossary: **inactive joint**.

- **A joint whose bodies are not valid goes inactive; nothing is removed.** An inactive joint keeps its `Joint` untouched and has no backend constraint.
  - Physics logs one warning each time the joint goes from valid to invalid, and creates the constraint again automatically once both names are valid bodies (a `RigidBody` added back). A destroyed entity never comes back: its id is versioned.
  - No event: `JointBroken` is only for threshold breaks (24). The remaining body is woken.
  - `PhysicsWorld::JointState` gives the zeroed state for an inactive joint, as for a non-joint.
  - Removing a `RigidBody` but keeping its `Collider` leaves a static body, so the joint stays active.
  - Rejected: removing the `Joint` (Unity; a structural change by physics that loses configuration) and dropping it for good (a body rebuilt or re-added would need the joint re-authored).
- **Invalid (inactive, with the warning):**
  - `bodyA` empty (the world is only ever `bodyB`, never swapped);
  - `bodyA == bodyB`;
  - either name not a body entity: no `RigidBody` and not a standalone static collider, a compound child, or a trigger's entity;
  - either name in a different `EntityList` (scene) from the joint entity (`Entity` carries its list, so this is checkable).
- **Valid but inert, silently:** both ends static or kinematic, or a static or kinematic `bodyA` joined to the world. The backend never solves it, and motion types change at runtime, so it starts working when an end becomes dynamic.
- **Motion type changes keep the constraint:** dynamic, kinematic and static switches never recreate it and log nothing. A kinematic end drags its dynamic partner and is never pushed.
- **Teleports:** writing one end's `Transform` does not move the other end. The joint pulls them together over the next steps, violently after a large jump. Physics privately resets warm starting on the teleported body's joints. The spec tells game code to move a whole jointed group in the same tick (each body's `Transform`) so the frames still line up; no group-teleport helper, since there is no defined group. Kinematic moves are not teleports and need nothing.
- **Shapes, scale and rebuilds:**
  - A shape or centre-of-mass change on a body that keeps its backend body (a `Collider` edit, a compound child added, removed or moved) keeps the constraint; physics privately tells the backend the shape changed, so anchors stay where they are relative to the body's origin.
  - A scale change moves the anchor (frames follow scale), so it recreates the joint's constraint like a frame edit.
  - A body rebuilt (removed then added in one drain) has its joints' constraints removed before it and created after it.
  - Reparenting a body entity changes its world pose: a teleport.
- **Sleep: every diffed change to a `Joint` wakes both its bodies** (the world excepted): live edits, recreation, adding or removing the joint, disabling it (a released body falls rather than hanging asleep), re-enabling it (repair), and lowering `breakForce` or `breakTorque` (so it is checked from the next step). A break needs no wake (24). No edit is too small to wake, which keeps sleep hidden.
  - This covers `collideConnected` (25): a toggle, or a joint created, enabled or disabled, wakes both bodies, so a changed exclusion applies from the next step rather than waiting on a sleeping pair's kept contact. An inactive joint excludes nothing, like a disabled one: going inactive drops its pair, and becoming valid again adds it back with the constraint in step 1(c).
- **Order in `PostTick`:**
  - Step 1 drains the `Joint` watch with the body watches: (a) remove constraints of removed joints and of every joint touching a body about to be destroyed or rebuilt; (b) destroy and create bodies; (c) create constraints for added joints and for joints valid again.
  - Step 2 diffs bodies first, then joints, each in stable entity order, since body shape and scale diffs decide whether a joint is notified or recreated.
  - Within a batch, constraints are created and removed in joint entity order. Steps 5b and 6 are as in 24.
- **Solve order follows joint entity order, not history:** each constraint gets a private backend priority from its joint entity's order, so within an island the solve order depends only on which joints exist, not on the backend's swap-on-removal. This keeps ticket 23's promise; the priority is never exposed (ADR-0010).
- **Inactive scenes:** nothing joint-specific. Watches keep recording; on resume step 1 applies the net effect. Nothing breaks, wakes or sends events while inactive. Destroying the world removes every constraint before any body.
- **Stale names:** a body removed between a break and the next step leaves the queued `JointBroken` naming it, as contact events do.
