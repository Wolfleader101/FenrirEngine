# What does game code read back from a character, and which events?

Type: grilling
Status: open
Blocked by: 30, 32

## Question

What does a character report after it moves, and how does game code hear what it touched? Builds on [What does Jolt offer for characters?](30-what-jolt-offers-for-characters.md) and [How is a character body modelled in the ECS?](32-character-bodies-in-the-ecs.md), and follows the foundation's events ("How do contact and trigger events reach game code?") and joint state ("What joint state can game code read, and how do joints break?").

- **Ground state:** on the ground, on a steep slope, not supported, in the air; the ground normal, point and body entity; written back into the component or asked for through `PhysicsWorld`.
- **Walls and ceilings:** whether hitting them is reported (Unity's `collisionFlags`).
- **Contacts:** reusing `ContactBegin`/`ContactEnd` (which name bodies and colliders) or new character contact events; opt-in like `ContactEvents`; their order.
- **Triggers:** whether a character entering a `Trigger` sends `TriggerEnter`/`TriggerExit`, and what it names as the other body.
- **Landing and leaving the ground:** events or derived by game code from the state.

Facts from "What does Jolt offer for characters?": four ground states (OnGround, OnSteepGround, NotSupported, InAir) with averaged normal and velocity, cached until the next update. Contact callbacks run on the updating thread; removed-contact callbacks fire in hash-map order, so FenrirPhysics must sort before sending events. Sensors (triggers) only see a character through its inner body.

From "How do other engines expose characters?": the richest ground state is Tnua's (ground entity, normal, ground velocity) with a cling band and coyote time against flicker, the most common complaint everywhere. Godot reports floor, wall and ceiling separately. bevy_rapier's output arriving a tick late is a known bug.
