# What does game code read back from a character, and which events?

Type: grilling
Status: resolved
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

## Answer

No ADR: every call can be reversed by adding fields or events, and all of it fits ADR-0011, ADR-0012 and ADR-0014. Glossary: **ground state**, **walkable ground** and **steep ground** added; **carried velocity** unchanged.

- **State is written back, not queried:** a nested plain struct `CharacterState state` inside `CharacterBody`, overwritten by physics at every write-back (step 5). Edits by game code are ignored and overwritten; physics keeps no diff copy of it. It describes the end of the last move (step 3b), against the bodies' poses before that tick's step.
  - Rejected: a `PhysicsWorld` query in the joints' style (every controller reads ground state every tick, which from C# would be a native call per character per tick, and Jolt has the state cached anyway) and a separate state component game code adds (one it can forget, so the character silently reports nothing).
- **Ground state:** `enum class GroundState : uint8_t { InAir, OnGround, OnSteepGround, Touching }`, default `InAir`:
  - `OnGround`: standing on walkable ground (no steeper than `maxSlope`);
  - `OnSteepGround`: the ground underneath is steeper than `maxSlope`;
  - `Touching`: touching something (a wall, a ledge) with nothing underneath;
  - `InAir`: touching nothing.
- **The ground:** `groundBody`, `groundCollider` (entities, named as contact events name them), `groundPoint`, `groundNormal` (world space, averaged over the ground contacts), `groundVelocity` (the ground's linear velocity at the character) and `groundAngularVelocity` (rad/s, world space; turning with a rotating platform is game code's, ticket 33). Set only for `OnGround` and `OnSteepGround`; empty or zero for `Touching` and `InAir`. A ground entity destroyed before game code reads it is detected by its version, as for events.
- **How it moved:** `movedVelocity`, the last move's displacement over `dt`, including stair step-ups, floor snaps, ground and carried velocity (for animation speed or footsteps). A teleport is not part of a move: the move after one is measured from the new position. `carriedVelocity`, ticket 33's carried velocity.
- **Walls and ceilings:** `bool onWall`, `bool onCeiling`, from the end-of-move contacts (the ones the `velocity` trim uses): a wall is a contact steeper than `maxSlope` that is not ground, a ceiling one whose normal points down. No normals in the state; `PhysicsWorld::ForEachContact(character)` lists a character's end-of-move contacts with point and normal (a wall jump's normal). Unlike a rigid body's, those come from before the step, and the doc comments say so.
- **Contact events:** the existing `ContactBegin`/`ContactEnd`, the character's entity as both body and collider; opt-in through `ContactEvents` on either side. No new character contact events.
  - Begins and ends come from comparing each move's end-of-move contacts with the previous move's; `relativeSpeed` is the closing speed along the normal at the start of the move.
  - The hidden inner body's own contacts during the step are never reported, so no pair is reported twice; a body sliding into a standing character is reported by that character's next move, at most a tick late.
  - Merged into step 6's sort by (entity A, entity B, collider A, collider B); character against character is one pair ordered by entity.
  - A character disabled, made inactive or losing its `CharacterBody` ends each reported contact on the next step (naming the possibly stale entity), as a removed body does, so every begin has an end; likewise when the other side drops `ContactEvents`.
- **Triggers:** a character sets off `TriggerEnter`/`TriggerExit` like any body, during the step, with `otherBody` and `otherCollider` both its entity. Disabling, inactivity or removal exits its triggers on the next step.
- **No landing or leaving events:** the state is written once per move and a `Tick` system sees every move, so game code finds a landing by comparing `state.ground` with its own copy from the last tick; fall damage uses the `velocity` game code wrote, which ADR-0014 already says it keeps. A `CharacterLanded` event can be added later without breaking anything.
- **Against flicker:** `floorSnap` only (Tnua's cling band). Coyote time is a game-code timer; no grace period or air-time counter in physics.
- **Disabled, inactive, paused:** disabled or inactive resets the state to its defaults (`InAir`, empty entities, zeros), so it never names a stale ground; a teleport tick fills it from that tick's move as usual; a paused world keeps it.
- **Foundation wording:** where `ContactBegin`, `ContactEnd`, `TriggerEnter`, `TriggerExit`, `Contact` and `ContactEvents` say "rigid body entity", the character spec says "body entity (a rigid body or a character body)".
- **Shape so far:**

  ```cpp
  enum class GroundState : uint8_t { InAir, OnGround, OnSteepGround, Touching };

  struct CharacterState // written by physics after each move; game code's edits are overwritten
  {
      GroundState ground = GroundState::InAir;
      Entity groundBody;                                // OnGround and OnSteepGround only
      Entity groundCollider;
      Math::Vec3 groundPoint = Math::Vec3(0.0f);        // world space
      Math::Vec3 groundNormal = Math::Vec3(0.0f);       // world space, unit, averaged
      Math::Vec3 groundVelocity = Math::Vec3(0.0f);     // m/s at the character
      Math::Vec3 groundAngularVelocity = Math::Vec3(0.0f); // rad/s
      Math::Vec3 movedVelocity = Math::Vec3(0.0f);      // last move's displacement / dt
      Math::Vec3 carriedVelocity = Math::Vec3(0.0f);
      bool onWall = false;
      bool onCeiling = false;
  };

  struct CharacterBody
  {
      Math::Vec3 velocity = Math::Vec3(0.0f); // world space, m/s; written back trimmed by hits
      float maxSlope = 0.785f;
      float stepHeight = 0.4f;
      float floorSnap = 0.5f;
      bool stopOnSlope = true;
      bool interpolate = true;
      bool enabled = true;
      CharacterState state;                   // read-back, overwritten each write-back
      // strength and mass from ticket 35
  };
  ```
