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

From "How is a character body modelled in the ECS?" (ADR-0014): physics never adds or removes a component on a character, so no `Grounded` marker; ground state is either fields physics writes or a `PhysicsWorld` query. If it is a component of its own (the Unity DOTS split of settings and state), game code adds it, as it adds `ContactEvents`. `velocity` is already written back with the parts into what the character hit removed; the velocity the character actually moved at is state for this ticket. The character is always visible to triggers as a kinematic body of its `Collider`, and events name its entity. A disabled or inactive character sends nothing and has its state cleared or kept (this ticket's call).

From "How does a character move each tick?": the move runs in a new `PostTick` step 3b, just before the step, and is written back in step 5, so state read in `Tick` describes the end of the last move, against the bodies' poses before that step. `velocity` is trimmed against end-of-move contacts, never measured, so the velocity the character actually moved at (including stair step-ups, floor snaps, ground and carried velocity) is this ticket's state. Ground velocity is added only after a move that ended on walkable ground; on leaving, it becomes **carried velocity** (cleared on landing, teleport or disable), which this ticket reports, along with the ground velocity game code needs for its own jumps. Rotating platforms never turn the character, so any ground angular velocity game code wants is state here. Character contacts arise in step 3b, before the bodies' step, so their events must be merged into step 6's sorted sending. Disabling clears carried velocity.
