# What happens at a joint's edges?

Type: grilling
Status: open
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
