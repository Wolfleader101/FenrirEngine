# What joint state can game code read, and how do joints break?

Type: grilling
Status: open
Blocked by: 20, 22

## Question

What can game code read back from a joint, and how does a joint break? Builds on [What does Jolt offer for constraints?](20-what-jolt-offers-for-constraints.md) and [How are joints modelled in the ECS?](22-joints-in-the-ecs.md).

- **State read back:** the current angle or position along the joint's free axes (a door's opening), the force and torque applied last step; written back into the component each step (like a dynamic body's velocity), or asked for through `PhysicsWorld` queries.
- **Breaking:** a force and a torque threshold on the joint; compared with what, over how many steps.
- **A broken joint:** a field state (`broken`, or `enabled` set false by physics), or the component removed (a structural change physics would make); whether it can be repaired.
- **Hearing about it:** a `JointBroken` engine event (plain data naming the joint and both bodies, sorted and sent after the step like contact events), readable through `EventReader<T>`.
- **Determinism and order:** where in the `PostTick` sequence breaking is checked and events are sent.

Facts from "What does Jolt offer for constraints?": breaking is not built in (lambda against a threshold after the step); current values exist only for hinge, slider, pulley, path, and swing-twist and six-DOF rotation; lambda is stale while the joint sleeps.

From "How are joints modelled in the ECS?" (ADR-0013): `Joint` already has `bool enabled = true`, a candidate for what breaking sets; thresholds are expected as shared fields beside it. The joint entity is what an event would name as "the joint".
