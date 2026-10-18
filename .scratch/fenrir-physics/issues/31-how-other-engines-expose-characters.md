# How do other engines expose characters?

Type: research
Status: resolved
Blocked by: none

## Question

How do other engines put a character controller in front of game code? Survey Unity (`CharacterController`: `Move`, `SimpleMove`, `isGrounded`, `collisionFlags`, `slopeLimit`, `stepOffset`, `OnControllerColliderHit`), Godot (`CharacterBody3D`: `velocity`, `move_and_slide`, floor and wall state, `floor_snap_length`, platforms), Unreal (`CharacterMovementComponent` and its movement modes), Bevy (bevy_rapier's `KinematicCharacterController`, Tnua, Avian's kinematic character examples) and O3DE, from their docs and source. For each:

- **Shape of the API:** a move call that returns a result, or a velocity field the engine applies; where the call happens (any phase, or the physics phase only).
- **Settings:** slope limit, step height, floor snapping, up direction, skin width, and which can change at runtime.
- **Ground state:** what is reported (grounded, on a steep slope, in the air, the ground normal and body) and when it is updated.
- **Collisions and events:** how game code hears what the character hit (callbacks, collision lists, events).
- **Platforms:** riding moving and kinematic bodies, and inheriting their velocity.
- **Other bodies:** pushing dynamic bodies, being pushed, character against character.
- **Shape changes:** crouching and the check before standing up.
- **Queries:** whether raycasts and triggers see a character.
- **ECS fit** (Bevy especially): which components, and how a move fits a system.
- **Scripting access**, and **common complaints** (jitter on slopes, stairs, platforms, tunnelling, ground-state flicker).

Put the findings on a `research/engine-character-models` branch, in `.scratch/fenrir-physics/research/engine-character-models.md`, and link them here.

## Answer

Findings: branch `research/engine-character-models`, `.scratch/fenrir-physics/research/engine-character-models.md` (commit 53ccebe): one section per engine (Unity and its ECS Character Controller package, Godot, Unreal's CharacterMovementComponent and its successor Mover, bevy_rapier, Tnua, Avian, O3DE), a comparison table, and the options for FenrirPhysics with no choice made. Engine sources are pinned to commits; Unreal is from the 5.8 docs.

- **Move call or field:**
  - a move call returning a result: Unity `Move`, Avian `MoveAndSlide` (a system parameter since late 2025), Rapier `move_shape`;
  - a field physics applies: bevy_rapier's translation request (cleared after the move), O3DE's queued velocity (once per physics step), Unreal's input, acceleration and `Velocity`, Tnua's desired motion;
  - Godot is between the two: a `velocity` field, but game code calls `move_and_slide()`; its own fallback off the physics frame is labelled a "Hack";
  - Unreal's successor, Mover, takes only inputs for the next tick, with no direct velocity writes.
- **Ground state:**
  - a bool from the last move: Unity, bevy_rapier (its output component lands a tick late, #515);
  - floor, wall and ceiling bools with normals and platform velocity: Godot;
  - a movement mode plus the current floor: Unreal;
  - the ground entity, normal and velocity, with a cling band and coyote time against flicker: Tnua;
  - nothing: Avian (examples add a `Grounded` marker, a structural change on every landing) and O3DE (an overlap on demand).
- **Platforms:** built in for Godot, Unreal and Tnua; Rapier follows kinematic platforms only while the character requests motion; not handled by Unity, Avian or O3DE by default.
- **Pushing:**
  - left to a hit callback: Unity, Avian;
  - approximate impulses, on by default: Rapier, with an issue showing a light box launched at 44× the character's speed;
  - forces scaled by mass: Unreal;
  - an infinite-mass kinematic body: O3DE, Godot;
  - only Tnua, a dynamic body, is pushed back.
- **Standing up:** only Unreal (`UnCrouch`) and Tnua (a headroom sensor) check for room.
- **Raycasts and triggers:**
  - raycasts see characters everywhere;
  - triggers: Rapier's kinematic–fixed pairs are off by default, so its character does not set off a fixed sensor;
  - O3DE ends up with two bodies per character, filed as a bug (#19702).
- **Common complaints:**
  - ground-state flicker (Unity, Godot, Rapier);
  - no stairs (Godot) or costly autostep (Rapier);
  - platform jitter or falling through (Unreal, Rapier, Tnua);
  - moving bodies launching the character (Unity, closed as by design);
  - wall and corner jitter (Godot);
  - scripting gaps (O3DE);
  - Epic's own list of the CharacterMovementComponent's limits: monolithic, capsule only, limited physics interaction.
- **Options named for FenrirPhysics:** a velocity field physics applies in its own phase, a one-shot request field, or a native move call; and whether a field-driven character body sits on top of a lower-level sweep query.
