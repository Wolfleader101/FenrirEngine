# How other engines expose characters

Research for [31-how-other-engines-expose-characters](../issues/31-how-other-engines-expose-characters.md). This file reports how Unity, Godot, Unreal, bevy_rapier, Tnua, Avian and O3DE put a character controller in front of game code, and what users complain about. It does not choose anything for FenrirPhysics.

Sources are engine source on GitHub, pinned to the commit read, plus official docs and the engines' own issue trackers and forums. Line numbers refer to those commits:

- Unity C# reference source, `Unity 7000.0.0a10` @ [`6c1fc79`](https://github.com/Unity-Technologies/UnityCsReference/tree/6c1fc7919aa64a2698e06922c4516eb1daa91b75). This is the managed bindings only; the PhysX character controller behind them is closed. Unity docs are the 6000.2 Scripting Reference and Manual, plus the ECS Character Controller package 1.2 manual.
- Godot `master` @ [`4d10419`](https://github.com/godotengine/godot/tree/4d10419cacd6d3009976e8f9fd3abd970fd58018).
- Unreal Engine 5.8: the source needs an Epic account, so this file cites only the official API reference and manual on dev.epicgames.com.
- bevy_rapier v0.37.0, kept in the Rapier repo, `master` @ [`475caa3`](https://github.com/dimforge/rapier/tree/475caa3ff4646fd9a796b67d14116256f4962bc7) (`bindings/bevy_rapier` for the Bevy side, `src/control` for Rapier's controller).
- Tnua v0.33.0 (15 October 2026) @ [`0f5450e`](https://github.com/idanarye/bevy-tnua/tree/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb).
- Avian `main` (0.8.0-dev) @ [`b620b67`](https://github.com/avianphysics/avian/tree/b620b67121def1a88a9eb272763aa24e7d594ccc).
- O3DE `development` @ [`74006f5`](https://github.com/o3de/o3de/tree/74006f539d581ea2ae8d21d4ef629d7a610a3e01) (PhysX gem and AzFramework), plus docs.o3de.org.

Every link is a full URL.

The engines fall into three families, and the family decides most of the answers below:

- **Collide-and-slide over a sweep, called by game code** (Unity, Godot, bevy_rapier, Avian, O3DE, and Unreal's walking mode). The character is not simulated. Something sweeps its shape along a requested motion, slides along what it hits, and writes the new position.
- **A movement component with modes** (Unreal's `CharacterMovementComponent`, and its successor Mover). Collide-and-slide is buried inside a state machine of walking, falling, swimming and flying, and game code feeds input rather than motion.
- **A floating dynamic body** (Tnua). The character is an ordinary rigid body that a spring holds above the ground, and the controller only computes forces.

---

## Unity

Unity's `CharacterController` is a thin C# face on PhysX's capsule controller. Unity's ECS package has a separate, much richer character controller, summarised at the end of this section.

### Shape of the API

- **A move call that returns a result.** `CollisionFlags Move(Vector3 motion)` takes "absolute movement delta values" and returns which sides collided ([CharacterController.bindings.cs L353–L425](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/CharacterController.bindings.cs#L353-L425)). `CollisionFlags` is a bitmask of `None`, `Sides`, `Above` and `Below` ([L63–L83](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/CharacterController.bindings.cs#L63-L83)).
- `bool SimpleMove(Vector3 speed)` takes a velocity instead, ignores its y part, applies gravity itself, and returns whether the character is grounded. The docs recommend "only one call to Move or SimpleMove per frame" ([L308–L352](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/CharacterController.bindings.cs#L308-L352)).
- **Called from any phase.** The examples call `Move` from `Update`. The move happens at once, outside the physics step. The controller "is not affected by forces and will only move when you call the Move function" ([L297–L304](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/CharacterController.bindings.cs#L297-L304)).
- `velocity` is read-only: "simply the difference in distance for the current timestep before and after a call to Move" ([L426–L462](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/CharacterController.bindings.cs#L426-L462)).

### Settings

All are read-write properties: `slopeLimit` (degrees), `stepOffset` (metres), `skinWidth`, `minMoveDistance`, `radius`, `height`, `center`, `detectCollisions` and `enableOverlapRecovery` ([L516–L668](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/CharacterController.bindings.cs#L516-L668)).

- **No up direction and no floor snapping.** Up is fixed to world Y. A character sticks to downhill slopes only if game code keeps pushing it down.
- The Manual recommends a skin width of at least 0.01 and "more than 10% of the Radius", and a step offset of 0.1–0.4 for a 2 m human. A step offset larger than the height "will generate an error" ([Character Controller component](https://docs.unity3d.com/6000.2/Documentation/Manual/class-CharacterController.html)).
- **Editing a property in the Inspector recreates the controller**, so existing trigger contacts are lost and "you will not get any OnTriggerEntered messages until the controller is moved again" (same page).
- Overlap recovery depenetrates from static objects only; "dynamic objects are ignored by overlap recovery" ([L651–L668](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/CharacterController.bindings.cs#L651-L668)).

### Ground state

- `isGrounded` (a bool) and `collisionFlags`, both from the last call to `Move` or `SimpleMove`. `isGrounded` "returns true if the controller collided with any object below it during the movement" ([L463–L515](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/CharacterController.bindings.cs#L463-L515)).
- No ground normal, ground body or "on a steep slope" state. Game code gets the normal from the hit callback or its own raycast.

### Collisions and events

- **A callback per hit, during the move.** `OnControllerColliderHit(ControllerColliderHit hit)` is a `MonoBehaviour` message "called when the controller hits a collider while performing a Move" ([OnControllerColliderHit](https://docs.unity3d.com/6000.2/Documentation/ScriptReference/CharacterController.OnControllerColliderHit.html)).
- `ControllerColliderHit` carries `controller`, `collider`, `rigidbody`, `gameObject`, `transform`, `point`, `normal`, `moveDirection` and `moveLength`. Unity reuses one instance across calls ([L85–L295](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/CharacterController.bindings.cs#L85-L295)). The normal note warns that edge and corner normals differ between box and mesh colliders "due to a limitation in how PhysX handles Capsule/BoxCollider collisions" ([L204–L222](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/CharacterController.bindings.cs#L204-L222)).

### Platforms

**Not supported.** `velocity` "won't track movements to the transform that happen outside of the CharacterController (e.g. character parented under another moving Transform, such as a moving vehicle)" ([L430–L434](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/CharacterController.bindings.cs#L430-L434)). The Manual does not mention platforms. The usual answers are parenting the character to the platform or adding the platform's motion to `Move` by hand.

### Other bodies

- **Pushing is game code.** "The Controller does not react to forces on its own and it does not automatically push Rigidbodies away" ([Manual](https://docs.unity3d.com/6000.2/Documentation/Manual/class-CharacterController.html)). The `OnControllerColliderHit` example pushes non-kinematic rigidbodies by setting their velocity, sideways only ([OnControllerColliderHit](https://docs.unity3d.com/6000.2/Documentation/ScriptReference/CharacterController.OnControllerColliderHit.html)).
- **Being pushed:** never by physics. `detectCollisions` decides only whether "an incoming collider will be blocked by the controller's collider"; a box "may still fall through the controller if detectCollisions is false" ([L628–L650](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/CharacterController.bindings.cs#L628-L650)).
- **Character against character:** each blocks the other's `Move`, with `detectCollisions` as the switch.

### Shape changes

Write `height` and `center`. There is no built-in check before standing up; game code checks the space with a general query such as [`Physics.CheckCapsule`](https://docs.unity3d.com/6000.2/Documentation/ScriptReference/Physics.CheckCapsule.html).

### Queries

`CharacterController` derives from `Collider` ([L304](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/CharacterController.bindings.cs#L304)), so raycasts hit it and triggers report it. The Manual's note about lost `OnTriggerEnter` messages confirms that triggers see it.

### Unity's ECS Character Controller package

The DOTS package (`com.unity.charactercontroller`) is closer to an ECS design ([Character components](https://docs.unity3d.com/Packages/com.unity.charactercontroller@1.2/manual/concepts-character-components.html)):

- **Settings and state are split.** `KinematicCharacterProperties` holds settings, and "Nothing in the character update writes to this component". `KinematicCharacterBody` holds what the update writes: `IsGrounded`, `GroundHit`, `RelativeVelocity`, `ParentEntity`, and helpers such as `HasBecomeGrounded`.
- **Hits are dynamic buffers:** `KinematicCharacterHit` holds every hit of the update, and `StatefulKinematicCharacterHit` adds Enter, Stay and Exit.
- **Platforms are explicit.** `ParentEntity` "is typically used for moving platforms", and a platform carries a `TrackedTransform` component.
- **Pushing is deferred.** `KinematicCharacterDeferredImpulse` "Stores impulses during the character update, and is used to apply them later on a single thread", so character updates can run in parallel against a `StoredKinematicCharacterData` snapshot.

### Scripting access

The whole API is C#: properties on a `Component`, and a `MonoBehaviour` message for hits.

### Complaints

- **Ground-state flicker.** The controller depenetrates every frame, so "unless some force is applied downward every frame then it will only be grounded for a single frame" ([CharacterController.isGrounded is broken?](https://discussions.unity.com/t/charactercontroller-isgrounded-is-broken/827807)). Unity's tracker has old reports of `isGrounded` false while standing on a plane ([issue](https://issuetracker-mig.prd.it.unity3d.com/issues/charactercontroller-isgrounded-returns-wrong-values-while-character-is-standing-on-a-simple-plane)). The common fix is a constant small downward motion, or a separate raycast.
- **Moving bodies launch the character.** Walking into a moving collider throws the character into the air regardless of `slopeLimit`. The issue is closed "By Design" ([dynamic collider ignores slope limit and gives force](https://issuetracker.unity3d.com/issues/charactercontroller-dynamic-collider-ignores-slope-limit-and-gives-force)). A related bug, a controller ending up on top of a moving kinematic object, was fixed in 5.5 ([jumps over moving kinematic object](https://issuetracker.unity3d.com/issues/when-colliding-charactercontroller-jumps-over-moving-kinematic-object)).
- **Platforms:** characters fall through moving platforms that are moved by teleporting their transform ([forum](https://discussions.unity.com/t/why-does-the-charactercontroller-fall-through-moving-platforms-but-not-idle-platforms/501017)).
- **Stairs jitter** with networked characters ([issue](https://issuetracker-mig.prd.it.unity3d.com/issues/character-controller-the-character-is-jittering-when-using-netcode-and-moving-downstairs-or-upstairs)).

---

## Godot

### Shape of the API

- **A velocity field the engine consumes, through an explicit call.** `CharacterBody3D` holds `velocity`, and game code calls `move_and_slide()`, which "Moves the body based on velocity" and "Modifies velocity if a slide collision occurred". It returns whether anything was hit ([CharacterBody3D.xml L132–L139, L189–L191](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/CharacterBody3D.xml#L132-L139)). The docs warn that a common mistake is setting `velocity` to a motion (velocity times delta).
- **Physics phase, by convention.** The call "should be used in `Node._physics_process`", because it reads the physics delta. The code falls back to the frame delta outside the physics frame, under a comment calling it a "Hack" and "calling from thread is risky" ([character_body_3d.cpp L43–L45](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/character_body_3d.cpp#L43-L45)).
- A lower-level `move_and_collide()` and `test_move()` live on the base `PhysicsBody3D` ([PhysicsBody3D.xml L41, L72](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/PhysicsBody3D.xml#L72)).
- `get_real_velocity()` returns what actually happened, such as the diagonal movement when climbing a slope ([xml L70–L73](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/CharacterBody3D.xml#L70-L73)).

### Settings

All are plain properties that can change at any time, with these defaults ([character_body_3d.h L117–L152](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/character_body_3d.h#L117-L152)):

| Property | Default | Meaning |
|---|---|---|
| `floor_max_angle` | 45° | Steepest surface that counts as floor rather than wall |
| `floor_snap_length` | 0.1 | Distance to snap down to the floor, unless moving up |
| `floor_stop_on_slope` | true | Do not slide down a slope while standing still |
| `floor_constant_speed` | false | Same speed up and down slopes |
| `floor_block_on_wall` | true | Cannot walk up walls |
| `wall_min_slide_angle` | 15° | Below this, hitting a wall stops instead of sliding |
| `slide_on_ceiling` | true | Slide along a ceiling during a jump |
| `max_slides` | 6 | Slide iterations per call |
| `safe_margin` | 0.001 | Recovery margin (skin) |
| `up_direction` | +Y | Per body; decides floor, wall and ceiling |
| `motion_mode` | grounded | `MOTION_MODE_FLOATING` reports everything as wall |

**No step height.** Stairs are not supported (see Complaints).

### Ground state

- `is_on_floor()`, `is_on_wall()`, `is_on_ceiling()` and their `_only` variants, `get_floor_normal()`, `get_wall_normal()`, `get_floor_angle()`, `get_platform_velocity()` and `get_platform_angular_velocity()` ([character_body_3d.h L49–L68](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/character_body_3d.h#L49-L68)).
- All are "only valid after calling move_and_slide", and describe "the last call" ([xml L96–L131](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/CharacterBody3D.xml#L96-L131)). The floor's body is kept only internally (`platform_rid`).
- "The collision normal is not always the same as the surface normal" (xml L37).

### Collisions and events

**A collision list, read after the call.** `get_slide_collision_count()` and `get_slide_collision(i)` return `KinematicCollision3D` objects (collider, normal, position, the collider's velocity), and `get_last_slide_collision()` returns the latest ([xml L46–L88](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/CharacterBody3D.xml#L46-L88)). There is no signal.

### Platforms

**Built in.** `move_and_slide` re-reads the floor body's state on every call and moves by the platform's velocity at the character's position first, excluding the platform itself from that sweep ([character_body_3d.cpp L56–L116](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/character_body_3d.cpp#L56-L116)).

- `platform_floor_layers` (default: all) and `platform_wall_layers` (default: none) choose which bodies count as platforms ([xml L168–L175](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/CharacterBody3D.xml#L168-L175)).
- `platform_on_leave` adds the platform's velocity on leaving it: in full, upward only, or not at all ([cpp L127–L135](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/character_body_3d.cpp#L127-L135); constants at [xml L204–L212](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/CharacterBody3D.xml#L204-L212)).
- The docs point at `AnimatableBody3D` for the platforms themselves ([xml L8](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/CharacterBody3D.xml#L8)).

### Other bodies

- Character bodies "are not affected by physics at all, but they affect other physics bodies in their path" ([xml L7](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/CharacterBody3D.xml#L7)). There is no push strength or mass. Godot 3's `infinite_inertia` switch is gone, and users push rigid bodies by applying impulses from the slide collisions ([forum answer](https://forum.godotengine.org/t/charachter-bodies-moving-one-another/42212)).
- **Character against character:** another `CharacterBody3D` is an obstacle, and its motion carries the character like a platform ([xml L135](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/CharacterBody3D.xml#L135)). Users ask how to stop two characters shoving each other ([forum](https://forum.godotengine.org/t/is-there-any-way-to-prevent-two-characterbody-push-each-other-when-use-move-and-slide/71141)).

### Shape changes

Edit or swap the `CollisionShape3D`. There is no stand-up check; `test_move()` or a `ShapeCast3D` is the tool.

### Queries

`CharacterBody3D` is a `PhysicsBody3D`, so raycasts hit it, and an `Area3D`'s `body_entered` fires for "a PhysicsBody3D, SoftBody3D or GridMap" ([Area3D.xml L184–L187](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/Area3D.xml#L184-L187)).

### Scripting access

Everything is bound through `ClassDB`, so GDScript and C# see the same API.

### Complaints

- **No stairs.** The proposal to build step-up into `move_and_slide` ([godot-proposals #2751](https://github.com/godotengine/godot-proposals/issues/2751)) is still a proposal. Asset-library add-ons such as StairsCharacter3D replace `move_and_slide` with their own call ([asset](https://godotengine.org/asset-library/asset/3807)).
- **Ground-state flicker:** `is_on_floor()` flips when moving from floor onto a steep slope ([#50756](https://github.com/godotengine/godot/issues/50756)), and with capsules under Jolt ([godot-jolt #643](https://github.com/godot-jolt/godot-jolt/issues/643)).
- **Jitter** against walls and static bodies, worse far from the origin and with large shapes ([#75537](https://github.com/godotengine/godot/issues/75537), [#92445](https://github.com/godotengine/godot/issues/92445), [#67432](https://github.com/godotengine/godot/issues/67432)).
- **Rotating platforms:** the character slides off a rotating `AnimatableBody3D` ([#102763](https://github.com/godotengine/godot/issues/102763)).
- The behaviour was discussed at length when Godot 4 reworked it ([#50732](https://github.com/godotengine/godot/issues/50732)).

---

## Unreal Engine

### Shape of the API

- **Input feeds a component that owns velocity.** `UCharacterMovementComponent` "handles movement logic for the associated Character owner". Movement "is affected primarily by current Velocity and Acceleration. Acceleration is updated each frame based on the input vector accumulated thus far" ([UCharacterMovementComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UCharacterMovementComponent)). Game code calls `AddMovementInput` on the pawn, `Jump`, `Crouch` or `LaunchCharacter`, or `AddImpulse` and `AddForce` on the component. It can also write `Velocity` directly.
- **The component runs the move in its own tick**, not in the physics step. Epic's own description of the successor says CMC's "default modes … move scene components directly" ([Mover Features and Concepts](https://dev.epicgames.com/documentation/en-us/unreal-engine/mover-features-and-concepts-in-unreal-engine)). `MaxSimulationTimeStep` splits a long frame into sub-steps.
- **Movement modes:** walking, falling, swimming, flying, NavWalking and custom (`PhysCustom`), changed by `SetMovementMode`, with `OnMovementModeChanged` called afterwards. "Networking is fully implemented, with server-client correction and prediction included" (class page).

### Settings

All `EditAnywhere BlueprintReadWrite`, so they can change during play (class page): `MaxStepHeight`, `SetWalkableFloorAngle` (which also sets `WalkableFloorZ`), `PerchRadiusThreshold` (no standing on an edge closer than this), `bCanWalkOffLedges`, `bMaintainHorizontalGroundVelocity`, `bAlwaysCheckFloor`, `MaxDepenetrationWithGeometry`, `CrouchedHalfHeight`, and many speed and acceleration values. Gravity has a direction (`SetGravityDirection`, `GetGravityDirection`), but the shape is always a vertical capsule ([Comparing Mover and CMC](https://dev.epicgames.com/documentation/unreal-engine/comparing-mover-and-character-movement-component-in-unreal-engine)).

### Ground state

- **The mode is the main state:** `IsMovingOnGround()` and `IsFalling()`.
- `CurrentFloor`, a `FFindFloorResult`, is "Information about the floor the Character is standing on (updated only during walking movement)". `FindFloor` "Sweeps a vertical trace to find the floor"; `bForceNextFloorCheck` forces one even when not moving (class page).
- `GetMovementBase()` returns the component stood on (class page).

### Collisions and events

- On `ACharacter`: `Landed(Hit)`, `OnLanded` and `LandedDelegate` "upon landing when falling"; `OnWalkingOffLedge` "when the Character is walking off a surface and is about to fall"; `MovementModeChangedDelegate`; `OnCharacterMovementUpdated`; and `MoveBlockedBy(Impact)` ([ACharacter](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/ACharacter)).
- On the component: `HandleImpact` for each blocking hit, and `OnCharacterStuckInGeometry`, "Only called during walking movement" (class page). These are virtual functions to override, not delegates.

### Platforms

**Built in, as a "base".** `SetBase` records the primitive stood on, and the character moves with it (`BasedMovement` on `ACharacter`). `bImpartBaseVelocityX/Y/Z` and `bImpartBaseAngularVelocity` decide what is inherited when "falling off it (which includes jumping)". `bIgnoreBaseRotation` ignores its rotation, and `bStayBasedInAir` keeps the base during a jump (class page).

### Other bodies

- **Pushing is built in, as forces.** `bEnablePhysicsInteraction` lets "the player … interact with physics objects when walking into them". `PushForceFactor` (optionally per kg, `bPushForceScaledToMass`), `TouchForceFactor`, `StandingDownwardForceScale` (pressing on what it stands on) and `RepulsionForce` ("applied constantly to all overlapping components") set the strength. `ApplyImpactPhysicsForces` is the override point (class page).
- **Being pushed:** physics does not move the character. Epic lists "Limited physics interactions" against CMC ([Comparing Mover and CMC](https://dev.epicgames.com/documentation/unreal-engine/comparing-mover-and-character-movement-component-in-unreal-engine)).

### Shape changes

`bWantsToCrouch` means "try to crouch (or keep crouching) on next update". `Crouch()` "Checks if new capsule size fits (no encroachment)" and `UnCrouch()` "Checks if default capsule size fits (no encroachment)", then call `OnStartCrouch` and `OnEndCrouch` on the owner. `bCrouchMaintainsBaseLocation` keeps the feet in place (class and `ACharacter` pages). This is the only engine surveyed whose shape-change check is built in.

### Queries

The capsule is an ordinary `UCapsuleComponent` on the character, so traces and overlaps see it according to its collision profile.

### Mover (experimental successor)

Epic intends the Mover plugin "to become the next-generation approach to character movement and eventually replace the Character Movement Component" ([Mover](https://dev.epicgames.com/documentation/en-us/unreal-engine/mover-in-unreal-engine)). It changes several things the survey cares about ([Comparing Mover and CMC](https://dev.epicgames.com/documentation/unreal-engine/comparing-mover-and-character-movement-component-in-unreal-engine), [Features and Concepts](https://dev.epicgames.com/documentation/en-us/unreal-engine/mover-features-and-concepts-in-unreal-engine)):

- **State is guarded.** "there is no velocity property to manipulate directly"; game code uses modes, instant effects and layered moves, which apply "during the next available simulation tick". Inputs are combined into one input command per simulation tick.
- Exactly one mode is active. Layered moves (homing, repulsion, root motion) mix on top and survive mode changes. Movement modifiers change settings in batches, for crouching for example.
- Any root component, not only a vertical capsule, and +Z need not be up.
- A physics backend over Chaos lets modes only set up a move and leave "the actual movement to the physics simulation".

### Scripting access

Blueprint, through `BlueprintCallable` functions, `BlueprintReadWrite` properties and `BlueprintAssignable` delegates, plus C++ overrides.

### Complaints

- Epic's own comparison lists CMC as "Monolithic", "Requires ACharacter actor", "Limited to capsule shape", and "Limited physics interactions" ([Comparing Mover and CMC](https://dev.epicgames.com/documentation/unreal-engine/comparing-mover-and-character-movement-component-in-unreal-engine)).
- **Jitter on moving platforms**, mostly in multiplayer: based movement is replicated relative to the base, and client and server simulate the base at different times ([players jitter on moving platform](https://forums.unrealengine.com/t/players-jitters-on-moving-platform/551011), [multiplayer jitter on moving platforms](https://forums.unrealengine.com/t/multiplayer-player-movement-jittery-on-moving-platforms/477855), [character shakes on elevator](https://forums.unrealengine.com/t/character-shakes-when-on-moving-elevator/685181)).
- **Order between characters on one base:** each character updates its based movement in its own tick, so characters on a shared platform collide against stale positions ([characters jitter on a moving base](https://forums.unrealengine.com/t/characters-jitter-when-moving-into-each-other-on-a-moving-base/2686307)).

---

## bevy_rapier

### Shape of the API

Two forms, both over Rapier's `KinematicCharacterController::move_shape`:

- **A requested-translation field that a plugin system applies.** The `KinematicCharacterController` component holds `translation: Option<Vect>`, "The translations we desire the character to move by", plus its settings ([character_controller.rs L112–L183](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/character_controller.rs#L112-L183)). Game systems set it. The plugin's `update_character_controls` system moves the shape, adds the result to the entity's (or its rigid body's) `Transform`, writes the output, and resets `translation` to `None` ([systems/character_controller.rs L21–L209](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/character_controller.rs#L21-L209)).
- **A direct call.** `RapierContext::move_shape(...)` takes the motion, shape, mass, `MoveShapeOptions` and a filter, calls back per hit, and returns `MoveShapeOutput` ([context/mod.rs L2513–L2544](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/context/mod.rs#L2513-L2544)).
- **Phase:** `update_character_controls` runs first in `PhysicsSet::SyncBackend`, before the step ([plugin.rs L127–L137](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/plugin.rs#L127-L137)). The physics systems run in `PostUpdate` unless the plugin is put in `FixedUpdate` ([L111–L120](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/plugin.rs#L111-L120)).
- The character is usually a `KinematicPositionBased` body or a bare collider. The controller "only computes translations", and rotation is written by game code ([L112–L117](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/character_controller.rs#L112-L117)).

### Settings

All are fields on the component, so all can change during play. Rapier's defaults ([character_controller.rs L209–L220](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/character_controller.rs#L209-L220)):

- `up`: +Y, per character.
- `offset` (skin): 1% of the shape's height. Lengths are `CharacterLength::Relative` or `Absolute`.
- `slide`: true.
- `autostep`: off. When on, `max_height` defaults to 25% of the height, `min_width` to 50%, and `include_dynamic_bodies` to true ([L65–L85](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/character_controller.rs#L65-L85)). It is off because it "is currently a very computationally expensive feature" ([L183–L187](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/character_controller.rs#L183-L187)).
- `max_slope_climb_angle` and `min_slope_slide_angle`: 45° each.
- `snap_to_ground`: 20% of the height.
- `normal_nudge_factor`: 1e-4.

bevy_rapier adds `custom_shape`, `custom_mass`, `apply_impulse_to_dynamic_bodies` (default true), `filter_flags` (default excludes sensors), `filter_groups`, `exclude_colliders`, `exclude_rigid_bodies`, a `filter_predicate`, and a `ControllerIgnored` marker for colliders every controller ignores ([L210–L233](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/character_controller.rs#L210-L233)).

### Ground state

`KinematicCharacterControllerOutput { grounded, desired_translation, effective_translation, collisions, is_sliding_down_slope }` ([L235–L252](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/character_controller.rs#L235-L252)).

- **No ground normal or ground entity**, beyond what game code finds in `collisions`.
- **Updated only on ticks with a requested translation.** The system skips characters whose `translation` is `None`.
- **The output component appears a tick late.** The first run inserts it through `Commands` ([systems/character_controller.rs L187–L204](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/character_controller.rs#L187-L204)).

### Collisions and events

`collisions: Vec<CharacterCollision>` in the output, each with the entity hit, the character's pose at the hit, the translation applied and remaining, and the `ShapeCastHit` ([L12–L28](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/character_controller.rs#L12-L28)). The direct `move_shape` call takes a callback instead. No Bevy events.

### Platforms

**Kinematic platforms are followed during a move.** While detecting ground, Rapier takes the velocity of a kinematic parent body at each contact point and adds it to the remaining translation ([character_controller.rs L530–L560](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/character_controller.rs#L530-L560)). That runs only inside the slide loop. With a zero requested translation, ground detection runs without it ([L429–L440](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/character_controller.rs#L429-L440)), so a character that requests no motion is not carried. There is no "inherit on leaving" setting.

### Other bodies

- **Pushing:** `solve_character_collision_impulses` applies impulses to dynamic bodies hit during the move. The docs warn the result "is only approximate as it is not based on a global constraints resolution scheme" ([L875–L880](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/character_controller.rs#L875-L880)). The mass is `custom_mass` or the body's own mass.
- **Being pushed:** only depenetration. When the requested translation is zero, the controller pushes the character out of overlapping non-sensor colliders. This "stops an obstacle pushed into a non-moving character from tunneling through it", capped at a quarter of the height per call ([L235–L262, L313–L324](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/character_controller.rs#L313-L324)).
- **Character against character:** each sees the other as an ordinary collider.

### Shape changes

Change the `Collider` or `custom_shape`. There is no stand-up check; game code uses a shape-cast or intersection query.

### Queries

The character is an ordinary collider, so raycasts see it. **Sensors on fixed bodies do not, by default.** Rapier's `ActiveCollisionTypes` default is dynamic–dynamic, dynamic–kinematic and dynamic–fixed. Kinematic–fixed is off, so a kinematic character in a fixed sensor raises no event until the collider's types are widened ([collider_components.rs L262–L286, L356–L362](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/geometry/collider_components.rs#L356-L362)).

### ECS fit

- **Components:** the settings-and-request component `KinematicCharacterController`, the written-back `KinematicCharacterControllerOutput`, the usual `Collider`, `RigidBody::KinematicPositionBased` and `Transform`, and the `ControllerIgnored` marker.
- **A move fits a system** as a field write (`controller.translation = Some(...)`) before the plugin's sets. The result is read back from the output component in a later system.
- The component is not `Reflect` ("TODO: Reflect", [L123](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/character_controller.rs#L123)), and its `filter_predicate` "can't read ECS data" ([L169–L173](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/character_controller.rs#L169-L173)).

### Scripting access

Rust only.

### Complaints

- **The output is missing on the first tick** ([bevy_rapier #515](https://github.com/dimforge/bevy_rapier/issues/515)). Users also ask for a `Grounded` marker ([#504](https://github.com/dimforge/bevy_rapier/issues/504)).
- **Velocity tied to frame rate:** the `Velocity` reported for the kinematic body depends on the frame delta ([#475](https://github.com/dimforge/bevy_rapier/issues/475)).
- **Platforms:** the character dips into and can fall through a vertically moving platform unless it keeps moving ([rapier #488](https://github.com/dimforge/rapier/issues/488)). It is choppy or stuck on kinematic ground ([bevy_rapier #630](https://github.com/dimforge/bevy_rapier/issues/630)).
- **Slopes and ground:** `max_slope_climb_angle` not respected ([#534](https://github.com/dimforge/bevy_rapier/issues/534)). `grounded` can be true before contact, and snapping may resolve a floor contact without reporting it ([rapier #1031](https://github.com/dimforge/rapier/issues/1031)).
- **Pushing launches light bodies,** for example a box left at 44 times the character's speed ([rapier #1020](https://github.com/dimforge/rapier/issues/1020)).
- **Getting stuck** inside a collider while falling ([#520](https://github.com/dimforge/bevy_rapier/issues/520)). Filter groups do not filter dynamic bodies ([#400](https://github.com/dimforge/bevy_rapier/issues/400)).

---

## Tnua (Bevy, over Rapier or Avian)

Tnua is "a floating character controller, which means that instead of constantly touching the ground the character floats above it" ([README L8](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/README.md#L8)). It is the only design surveyed where the character is a real rigid body.

### Shape of the API

- **A desired-motion field, applied as forces over later ticks.** The character "must have a dynamic rigid body" plus `TnuaController<Scheme>` and a `TnuaConfig` asset handle ([lib.rs L60–L103](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/src/lib.rs#L60-L103)).
- Each tick a user system sets `controller.basis = TnuaBuiltinWalk { desired_motion, desired_forward }` and feeds actions such as `Jump`, which "must be fed as long as the player holds the button" ([L121–L157](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/src/lib.rs#L121-L157)). The control scheme is a user enum deriving `TnuaScheme`, with one variant per action ([L33–L58](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/src/lib.rs#L33-L58)).
- **Phases are system sets:** `Sensors` ("Data is read from the physics backend"), `Logic` and `Motors` ("Forces are applied in the physics backend") ([physics-integration-layer/src/lib.rs L105–L110](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/physics-integration-layer/src/lib.rs#L105-L110)). User control systems go in `TnuaUserControlsSystems`. All of them must be put in the physics schedule when it is `FixedUpdate` ([lib.rs L28–L31](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/src/lib.rs#L28-L31)).
- The output to the backend is a `TnuaMotor` of velocity changes ("these numbers ignore mass and are applied directly to the velocity") ([data_for_backends.rs L245–L262](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/physics-integration-layer/src/data_for_backends.rs#L245-L262)).

### Settings

`TnuaBuiltinWalkConfig`: `speed`, `float_height` (which "Must be larger than the height of the entity's center from the bottom of its collider"), `cling_distance`, `spring_strength`, `spring_dampening`, `acceleration`, `air_acceleration`, `coyote_time`, `free_fall_extra_gravity`, tilt-correction values, `turning_angvel`, `max_slope` and `headroom` ([builtins/walk.rs L39–L145](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/src/builtins/walk.rs#L39-L145)). The config is an asset, so changes go through the asset or a new handle.

- **No step height:** the float height takes the character over steps and slopes ("Running up/down slopes/stairs", [README L32](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/README.md#L32)).
- `max_slope` defaults to 90°, which disables slipping ([README L61](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/README.md#L61)).
- Up and gravity come from the body's gravity, or a `TnuaGravity` component ([data_for_backends.rs L294–L296](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/physics-integration-layer/src/data_for_backends.rs#L294-L296)).

### Ground state

- A `TnuaProximitySensor` (by default one ray, optionally a shape via `Tnua<backend>SensorShape`) reports the `entity`, `proximity`, `normal`, and the ground's `entity_linvel` and `entity_angvel` ([data_for_backends.rs L70–L116](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/physics-integration-layer/src/data_for_backends.rs#L70-L116); [lib.rs L105–L113](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/src/lib.rs#L105-L113)).
- `controller.is_airborne()` ([controller.rs L568–L574](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/src/controller.rs#L568-L574)), and the walk basis's memory exposes `standing_on_entity()`, `standing_offset` and `running_velocity` ([walk.rs L186–L206](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/src/builtins/walk.rs#L186-L206)).
- **Hysteresis is built in:** `cling_distance` keeps the spring pulling down within a band above the float height, and only beyond it is the character "in the air" ([walk.rs L65–L71](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/src/builtins/walk.rs#L65-L71)). Coyote time adds a grace period.
- Updated every tick in `Sensors`.

### Collisions and events

**None of Tnua's own.** It reads only its sensors. Contacts are the physics backend's ordinary collision events for the dynamic body. Animation helpers (`TnuaAnimatingState`) report state transitions.

### Platforms

**Built in:** "Moving platforms" and "Rotating platforms" ([README L34–L35](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/README.md#L34-L35)). `running_velocity` is "relative to the velocity of that entity" when standing on one (walk.rs L193–L198). `TnuaNotPlatform` excludes colliders, and `TnuaGhostPlatform` makes jump-through platforms, which need solver groups set ([data_for_backends.rs L264–L302](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/physics-integration-layer/src/data_for_backends.rs#L264-L302)).

### Other bodies

**All from the physics engine:** a dynamic body pushes, is pushed, and collides with other characters by mass.

### Shape changes

`TnuaBuiltinCrouch` lowers the float height (`float_offset`) and does not change the collider. Standing up is allowed only if the upward `headroom` sensor shows room ([crouch.rs L115–L117](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/src/builtins/crouch.rs#L115-L117); [walk.rs L59–L63](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/src/builtins/walk.rs#L59-L63)).

### Queries

A dynamic body with a collider, so raycasts and sensors see it like any other body.

### ECS fit

- **Components:** `TnuaController<S>` (input and state together), `TnuaConfig<S>` (an asset handle), sensor entities, and backend-written `TnuaRigidBodyTracker`, `TnuaProximitySensor` and `TnuaMotor`.
- **Switches are mostly field edits:** `TnuaToggle` disables Tnua, or only its motor ([data_for_backends.rs L8–L31](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/physics-integration-layer/src/data_for_backends.rs#L8-L31)).
- **A move is a field write** in a user system ordered before Tnua's sets. Nothing happens synchronously.

### Scripting access

Rust only.

### Complaints

- **Jitter and stutter:** walking off a moving platform ([#99](https://github.com/idanarye/bevy-tnua/issues/99)), crouching ([#90](https://github.com/idanarye/bevy-tnua/issues/90)), and a demo with Rapier ([#98](https://github.com/idanarye/bevy-tnua/issues/98)). Earlier, the character was pushed when a platform changed direction ([#13](https://github.com/idanarye/bevy-tnua/issues/13), [#28](https://github.com/idanarye/bevy-tnua/issues/28)).
- **Floating side-effects:** the character "bends" over low platforms when tilt is not locked ([#87](https://github.com/idanarye/bevy-tnua/issues/87)), and gets stuck on walls when jumping into them ([#75](https://github.com/idanarye/bevy-tnua/issues/75)).
- How to check "grounded" was a user question early on ([#4](https://github.com/idanarye/bevy-tnua/issues/4)). Users asked for a kinematic backend, to avoid a full physics engine ([#36](https://github.com/idanarye/bevy-tnua/issues/36)).

---

## Avian (Bevy)

Avian ships **a move-and-slide utility, not a controller**, plus two example controllers.

### Shape of the API

- **A move call that returns a result.** `MoveAndSlide` is a `SystemParam`, "also known as *collide and slide* or *step slide*", added in 2025 ([PR #894](https://github.com/avianphysics/avian/pull/894)). `move_and_slide(shape, position, rotation, velocity, delta_time, &config, &filter, on_hit)` sweeps, slides, and depenetrates before and after the move ([move_and_slide.rs L17–L68, L397–L483](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/character_controller/move_and_slide.rs#L17-L68)).
- It returns `MoveAndSlideOutput { position, projected_velocity }`. The docs say to "Set your Transform::translation to this value", and to feed `projected_velocity` back as next tick's velocity, with `CustomPositionIntegration` so the integrator does not also move the body ([L251–L275](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/character_controller/move_and_slide.rs#L251-L275)).
- `cast_move`, `depenetrate`, `intersections` and `project_velocity` are exposed for custom controllers ([L45–L55](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/character_controller/move_and_slide.rs#L45-L55)).
- **Any system can call it**, since it is a read-only spatial query. The kinematic example runs it in `FixedUpdate` ([kinematic_character_3d/plugin.rs L11–L31](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/crates/avian3d/examples/kinematic_character_3d/plugin.rs#L11-L31)).

### Settings

`MoveAndSlideConfig` is a plain `Reflect` struct passed per call: iteration counts, `max_depenetration_error`, `penetration_rejection_threshold`, `skin_width` ("Decrease it when you notice jittering, especially around V-shaped walls"), initial `planes` (for example the ground plane), `plane_similarity_dot_threshold` and `max_planes` ([L88–L166](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/character_controller/move_and_slide.rs#L88-L166)). **No slope limit, step height, snapping or up direction:** those belong to the controller built on top. The kinematic example's `GroundDetection.max_angle` defaults to 30° ([plugin.rs L77–L99](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/crates/avian3d/examples/kinematic_character_3d/plugin.rs#L77-L99)).

### Ground state

Not in Avian. Both examples add a `Grounded` **marker component**, inserted and removed through `Commands` each tick by a shape-cast system ([kinematic plugin.rs L101–L107, L166–L203](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/crates/avian3d/examples/kinematic_character_3d/plugin.rs#L166-L203); [dynamic_character_3d/plugin.rs L35–L38](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/crates/avian3d/examples/dynamic_character_3d/plugin.rs#L35-L38)). That makes grounding a structural change on every transition.

### Collisions and events

**A callback during the move.** `on_hit` receives `MoveAndSlideHitData` (entity, safe distance, point, normal, and mutable position and velocity) and returns `Accept`, `Ignore` or `Abort` ([L277–L340](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/character_controller/move_and_slide.rs#L277-L340)). The kinematic example collects hits into a `CharacterCollisions` component ([plugin.rs L109–L125](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/crates/avian3d/examples/kinematic_character_3d/plugin.rs#L109-L125)).

### Platforms

**Not handled** by the utility or the examples.

### Other bodies

- **Pushing is the example's own system.** `apply_forces_to_dynamic_bodies` applies an impulse of the relative normal velocity times the character's mass at each hit point ([plugin.rs L448–L480](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/crates/avian3d/examples/kinematic_character_3d/plugin.rs#L448-L480)).
- **Being pushed:** the kinematic body is not pushed. The depenetration passes move it out of anything that ends up overlapping it.
- The **dynamic** example is a rigid body with `LockedAxes::ROTATION_LOCKED` and a `ShapeCaster` for the ground, so pushing and being pushed are physical ([dynamic_character_3d/plugin.rs L58–L118](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/crates/avian3d/examples/dynamic_character_3d/plugin.rs#L58-L118)).

### Shape changes

Swap the `Collider`. There is no stand-up check; `intersections` or a shape cast does it.

### Queries

The character is a kinematic body, so raycasts see it. The broad phase pairs kinematic bodies with static ones and skips only static–static pairs without a sensor ([bvh_broad_phase.rs L124–L125](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/collision/broad_phase/bvh_broad_phase.rs#L124-L125)). Static sensors therefore see kinematic characters.

### ECS fit

- **A move is a call inside a user system**, not a component. Settings and state are the user's own components: the example's `CharacterController` marker requires `RigidBody::Kinematic` and `CustomPositionIntegration`, plus `CharacterMovementSettings` and `GroundDetection` ([plugin.rs L40–L99](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/crates/avian3d/examples/kinematic_character_3d/plugin.rs#L40-L99)).
- The example is a chain: update grounded, apply gravity, movement, damping, move and slide, push ([L18–L30](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/crates/avian3d/examples/kinematic_character_3d/plugin.rs#L18-L30)).
- Tnua and Bevy Ahoy are the off-the-shelf controllers for Avian (Tnua's [README L180–L184](https://github.com/idanarye/bevy-tnua/blob/0f5450e3bd2338bbb616244ac4b3b4dee39bfbfb/README.md#L180-L184)).

### Scripting access

Rust only.

### Complaints

- **Documentation and examples:** questions on Discord come up "a lot" ([#437](https://github.com/avianphysics/avian/issues/437)), and there is a plan for fuller platformer, ARPG and FPS examples ([#450](https://github.com/avianphysics/avian/issues/450)).
- **The examples' own gaps:** `MaxSlopeAngle` is not fully implemented in the dynamic example ([#757](https://github.com/avianphysics/avian/issues/757)). Collision resolution fails when pushing a block onto a ledge in the kinematic example ([#797](https://github.com/avianphysics/avian/issues/797)).
- `MoveAndSlide` panics when two colliders overlap ([#973](https://github.com/avianphysics/avian/issues/973)). A kinematic character over a sensor pressure plate is not detected as grounded ([#769](https://github.com/avianphysics/avian/issues/769)).

---

## O3DE (PhysX gem)

### Shape of the API

- **Queued velocity requests, applied on the physics tick.** `AddVelocityForTick` and `AddVelocityForPhysicsTimestep` accumulate requests ("the effects of animation and gravity can be applied in two separate requests"). `ApplyRequestedVelocity` sums and clamps them to `m_maximumSpeed` and moves once. This "avoids performing multiple expensive character updates, and avoids any effects from the order of requests within a tick" ([Character.h L131–L166](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Code/Framework/AzFramework/AzFramework/Physics/Character.h#L131-L166)).
- The component calls `ApplyRequestedVelocity` from the scene's simulation-start event, once per fixed physics step, and clears per-tick requests after the simulation ([CharacterControllerComponent.cpp L472–L488](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/PhysXCharacters/Components/CharacterControllerComponent.cpp#L472-L488)). `m_applyMoveOnPhysicsTick` (default true) controls this ([Character.h L87](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Code/Framework/AzFramework/AzFramework/Physics/Character.h#L87)).
- **A direct move also exists:** `Move(requestedMovement, deltaTime)` calls `PxController::move` and returns nothing ([Character.h L168–L173](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Code/Framework/AzFramework/AzFramework/Physics/Character.h#L168-L173); [CharacterController.cpp L656–L672](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/PhysXCharacters/API/CharacterController.cpp#L656-L672)). PhysX's collision flags are discarded.

### Settings

`CharacterConfiguration`: collision layer and group, materials, `m_upDirection` (+Z), `m_maximumSlopeAngle` (30°), `m_stepHeight` (0.5), `m_minimumMovementDistance` (0.001), `m_maximumSpeed` (100) ([Character.h L65–L88](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Code/Framework/AzFramework/AzFramework/Physics/Character.h#L65-L88)). The PhysX side adds `SlopeBehaviour` (`PreventClimbing` or `ForceSliding`), `m_contactOffset` (0.1) and `m_scaleCoefficient` (0.8) ([CharacterController.h L22–L43](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/PhysXCharacters/API/CharacterController.h#L22-L43)).

- Step height, slope limit, maximum speed, up direction, layer and group have runtime setters ([Character.h L102–L119](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Code/Framework/AzFramework/AzFramework/Physics/Character.h#L102-L119)). Script reaches only some of them (Scripting access below).
- **No floor snapping.**

### Ground state

**None on the controller.** The separate Character Gameplay component, which "provides example implementations of logic which is likely to be more game-specific, such as gravity" ([docs](https://docs.o3de.org/docs/user-guide/components/reference/physx/character-gameplay/)), offers `IsOnGround()`. It runs a box overlap under the feet on demand and returns true if anything but the character is there ([CharacterGameplayComponent.cpp L112–L160](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/PhysXCharacters/Components/CharacterGameplayComponent.cpp#L112-L160)). It has no normal, ground body or slope state.

### Collisions and events

- **C++ only.** `CharacterControllerCallbackManager` wraps PhysX's `onShapeHit`, `onControllerHit` and `onObstacleHit`, plus filter and riding callbacks, as settable `std::function`s ([CharacterController.h L47–L146](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/PhysXCharacters/API/CharacterController.h#L47-L146)).
- No component sets them at this commit: the setters are called nowhere outside the class.
- `CharacterNotifications` reports only activation and deactivation ([CharacterBus.h L111–L176](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Code/Framework/AzFramework/AzFramework/Physics/CharacterBus.h#L111-L176)).

### Platforms

PhysX can ride objects, but O3DE's default behaviour flags are 0 for shapes and controllers and `eCCT_CAN_RIDE_ON_OBJECT` only for PhysX obstacles ([CharacterController.cpp L182–L216](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/PhysXCharacters/API/CharacterController.cpp#L182-L216)). So by default a character does not inherit a moving body's motion, unless C++ installs a riding callback.

### Other bodies

- **Dynamic bodies do not block the character.** The example object pre-filter returns no hit for non-kinematic dynamic actors ([CharacterController.cpp L1050–L1074](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/PhysXCharacters/API/CharacterController.cpp#L1050-L1074)).
- **Pushing** is done by PhysX's hidden kinematic actor, sized by "Internal Body Scale". Below 1 it "will not touch surrounding rigid bodies"; at 1 or more it pushes them, "potentially resulting in wall tunneling or sudden movement explosion" ([L62–L70](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/PhysXCharacters/API/CharacterController.cpp#L62-L70)). The docs: "Kinematic controllers behave as if they have infinite mass when simulated objects collide with them" ([Character Controller component](https://docs.o3de.org/docs/user-guide/components/reference/physx/character-controller/)).
- O3DE also creates a kinematic "shadow body" to carry extra colliders. It is driven by kinematic targets after each move ([CharacterController.cpp L312–L344, L656–L692](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/PhysXCharacters/API/CharacterController.cpp#L312-L344)).
- **Character against character:** the example filter makes controllers block each other according to their collision layers ([CharacterController.h L266–L268](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/PhysXCharacters/API/CharacterController.h#L266-L268)).

### Shape changes

`Resize(height)` and the height, radius and extent setters on `CharacterControllerRequestBus` ([CharacterControllerBus.h L22–L57](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Include/PhysX/CharacterControllerBus.h#L22-L57)). `Resize` validates only the dimensions; there is no space check ([CharacterController.cpp L772–L800](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/PhysXCharacters/API/CharacterController.cpp#L772-L800)).

### Queries

The PhysX controller's actor and the shadow body are both in the scene, so queries see the character. Two bodies for one character is reported as a bug ([#19702](https://github.com/o3de/o3de/issues/19702)).

### Scripting access

- Script Canvas and Lua reach `CharacterControllerRequestBus` for position, step height, slope limit, maximum speed, velocity and `AddVelocityForTick` ([Utils.cpp L77–L95](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Code/Framework/AzFramework/AzFramework/Physics/Utils.cpp#L77-L95)).
- They also reach the PhysX resize bus ([CharacterControllerComponent.cpp L42–L57](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/PhysXCharacters/Components/CharacterControllerComponent.cpp#L42-L57)) and the gameplay bus with `IsOnGround`.
- They cannot reach the up direction setter ([#14081](https://github.com/o3de/o3de/issues/14081)), `m_applyMoveOnPhysicsTick` ([#14284](https://github.com/o3de/o3de/issues/14284)), or hit callbacks.

### Complaints

- **Ground detection:** "Is On Ground" counts triggers as ground ([#11895](https://github.com/o3de/o3de/issues/11895)), works only with the gameplay component ([#11070](https://github.com/o3de/o3de/issues/11070)), and gravity stops while on the ground ([#16196](https://github.com/o3de/o3de/issues/16196)).
- The `ForceSliding` slope behaviour does not work ([#14333](https://github.com/o3de/o3de/issues/14333)).
- The defaults are unrealistic for humanoids ([#2494](https://github.com/o3de/o3de/issues/2494)), and runtime collider offsets misbehave ([#14338](https://github.com/o3de/o3de/issues/14338)).

---

## Comparison

| | Unity `CharacterController` | Godot `CharacterBody3D` | Unreal CMC | bevy_rapier KCC | Tnua | Avian `MoveAndSlide` | O3DE PhysX |
|---|---|---|---|---|---|---|---|
| **API shape** | `Move(delta)` returns flags; `SimpleMove(speed)` | `velocity` field plus `move_and_slide()` call | Input vector, acceleration and `Velocity`; modes | `translation` request field applied by a plugin system; or `move_shape` call | Desired motion in a controller component; forces | `move_and_slide(...)` call returns position and velocity | Queued velocity requests; or `Move` |
| **Where it runs** | Immediately, any phase | Immediately; meant for the physics process | The component's own tick, sub-stepped | Before the step, in `SyncBackend` | Sensors, logic, motors sets; the physics step moves it | Any user system | Physics simulation start, once per step |
| **Slope limit** | `slopeLimit` | `floor_max_angle` (45°) | Walkable floor angle | Climb angle and slide angle (45° each) | `max_slope` (off by default) | The controller's own | 30°, prevent or slide |
| **Step height** | `stepOffset` | None | `MaxStepHeight` | `autostep` (off: costly) | Float height | None | `m_stepHeight` (0.5) |
| **Floor snap** | None | `floor_snap_length` (0.1) | Walking mode keeps to the floor | `snap_to_ground` (20% of height) | Spring and cling distance | The controller's own | None |
| **Up direction** | Fixed +Y | Per body | Gravity direction; capsule upright | Per controller | From gravity | The controller's own | Per character (no script setter) |
| **Skin** | `skinWidth` | `safe_margin` | Internal | `offset` (1%) | n/a | `skin_width` | `m_contactOffset` |
| **Runtime changes** | All properties (Inspector edits recreate it) | All properties | All `BlueprintReadWrite` | All fields | Config asset | Per call | Setters, some in script |
| **Ground state** | `isGrounded`, `collisionFlags` from last move | Floor, wall and ceiling bools, normals, platform velocity, from last call | Mode, `CurrentFloor`, movement base | `grounded`, `is_sliding_down_slope` in output component; no normal or body | Sensor entity, distance, normal, ground velocity; `is_airborne` | None (examples use a `Grounded` marker) | None (example overlap `IsOnGround`) |
| **Hits** | `OnControllerColliderHit` per hit, during move | Slide-collision list after the call | Virtual `HandleImpact`, `MoveBlockedBy`, `Landed`, ledge and mode events | `collisions` in output; or callback | Backend contact events only | `on_hit` callback per hit (accept, ignore, abort) | C++ callbacks, not wired |
| **Platforms** | None | Built in, layer-filtered, inherit on leave | Built in (base), impart velocity per axis | Kinematic platforms, only while moving | Built in, moving and rotating | None | Off by default |
| **Pushing dynamics** | Game code in hit callback | Pushes with no mass; impulses are game code | Forces, scaled by mass | Approximate impulses (default on) | Physics | Example system | Hidden kinematic actor, infinite mass |
| **Being pushed** | No | No | No | Depenetration only | Yes | Depenetration only | No |
| **Character against character** | Block | Block, and carry like a platform | Block | Block | Physics | Block | Layer-based filter |
| **Crouch check** | None | None | `Crouch` and `UnCrouch` check encroachment | None | Headroom sensor | None | None |
| **Raycasts see it** | Yes (a `Collider`) | Yes | Yes (capsule) | Yes | Yes | Yes | Yes (two bodies) |
| **Triggers see it** | Yes | Yes | Yes (overlaps) | Not fixed sensors by default (kinematic–fixed off) | Yes | Yes | Yes |
| **ECS shape** | n/a (DOTS package: properties, body state, hit buffers, deferred impulses) | n/a | n/a | Request plus settings component, output component | Controller, config, sensor entities, motor | User components and a system parameter | n/a |
| **Scripting** | C# | GDScript and C# | Blueprint and C++ | Rust | Rust | Rust | Script Canvas and Lua, partial |
| **Common complaints** | `isGrounded` flicker; launched by moving bodies; no platforms | No stairs; floor flicker; wall jitter; rotating platforms | Monolithic, capsule only; platform jitter in multiplayer; tick order on shared bases | Output a tick late; vertical-platform dips; slope limit; impulse launches | Platform and crouch jitter; float side-effects | Docs and examples; overlap panic | Ground check counts triggers; force sliding broken; script gaps |

---

## What this means for FenrirPhysics

FenrirPhysics keeps plain-data components that physics diffs each tick, and an entity holds at most one component of each type (ADR-0011). C# native functions may read and write component data but make no structural changes (ADR-0009). Against those rules, the surveyed engines leave these options for tickets 32–39.

**How game code drives a character**

- **A. A desired-velocity field that physics applies in its own phase** (Godot's `velocity` without the explicit call; O3DE's queued velocity; Unreal and Mover's input-then-tick; Tnua's desired motion).
  - It is a field edit, so it fits ADR-0011 and needs no native call from C#.
  - The result arrives after the step, so game code reads ground state from the previous tick.
  - O3DE's accumulation (gravity, animation and input added separately, applied once) and Mover's "no velocity to poke, only inputs for the next tick" are two strictness levels within it.
- **B. A requested-translation field cleared after use** (bevy_rapier). Same timing as A. It also carries a meaning ("no request this tick") that A expresses as zero velocity, and bevy_rapier shows the cost: no request means no ground update.
- **C. A move call that returns a result** (Unity `Move`, Godot `move_and_slide`, Avian `MoveAndSlide`, Rapier `move_shape`).
  - Game code sees the outcome at once and can run several moves or custom logic in one tick.
  - In FenrirPhysics it would be a native function. It writes the `Transform`, which ADR-0009 allows, but it runs outside physics's tick order, and its effects interleave with the diff.
  - Godot's "Hack" for calling outside the physics frame and Unity's "one Move per frame" advice show what happens when call sites are unconstrained.
- **A and C can coexist:** a field-driven body for most characters, plus a lower-level sweep query (Avian's `cast_move` and `depenetrate`, Godot's `test_move`) for custom movement. That query is ticket 36's territory.

**Ground state**

- **Written back into a component each tick** (bevy_rapier's output, Tnua's sensor output, the Unity ECS package's `KinematicCharacterBody`), versus asked for on demand (O3DE's overlap) or kept on an object (Unity, Godot, Unreal).
- **What it holds** ranges from one bool (Unity, bevy_rapier) to floor, wall and ceiling with normals and platform velocity (Godot), to the ground entity, normal and velocity (Tnua). The DOTS package adds transition flags (`HasBecomeGrounded`).
- **Flicker is the most common complaint.** The engines that avoid it hold the character to the floor: Godot's snap, Rapier's `snap_to_ground`, Tnua's `cling_distance` and coyote time. Unity and O3DE do not, and users add a downward push or their own raycast.
- A marker component for "grounded" (the Avian examples) would make every landing a structural change, which ADR-0011 rules out.

**Hits and events**

- **During the move** (Unity's message, Avian's callback, PhysX's and Jolt's contact listeners), versus **a list written back** (Godot's slide collisions, bevy_rapier's `collisions`, Unity ECS's hit buffers).
- ADR-0012's events, where each reader sees each event once, would be a third form: per-hit or per-transition events, such as "landed" and "left ground" (Unreal's `Landed`, `OnWalkingOffLedge`; Unity ECS's Enter and Exit hits).

**Platforms.** Three levels:

- **None** (Unity, Avian, O3DE by default).
- **Follow the ground body's velocity while touching it** (Rapier's kinematic friction, but only while moving).
- **Follow, plus choose what is inherited on leaving** (Godot's `platform_on_leave`, Unreal's per-axis `bImpartBaseVelocity`, Tnua's relative `running_velocity`).

Godot and Unreal also filter which bodies count as platforms (layers, `TnuaNotPlatform`). Order matters: Unreal's shared-base jitter and Rapier's vertical-platform dips come from moving the character against a platform pose from a different time.

**Other bodies**

- **Pushing:** game code in a hit callback (Unity, Avian's example), approximate impulses from the controller (Rapier, on by default; launches light bodies), forces scaled by mass (Unreal), or an infinite-mass kinematic proxy (O3DE, Godot).
- **Being pushed:** only Tnua, a dynamic body, is pushed physically. The kinematic designs offer depenetration at most.
- **Character against character:** everywhere, each blocks the other's sweep. Only O3DE exposes a filter for it.

**Shape changes.** Only Unreal (`UnCrouch` checks encroachment) and Tnua (headroom sensor) check before standing up. Everyone else leaves it to a game-code query. The options are a built-in check on shape change, or a query FenrirPhysics exposes and game code calls.

**Queries and triggers.** Every engine lets raycasts see characters. Triggers differ by pairing rule: Rapier skips kinematic–fixed pairs by default, so kinematic characters do not set off fixed sensors, while Avian and the PhysX engines pair them. O3DE's two bodies per character is a warning about adding a second body just so queries see a character.

**Settings.** The common set is slope limit, step height, floor snap, skin, and an up direction. All engines let them change during play (as fields, properties or setters), so they fit as fields on one component. The open question is whether step height and floor snap are on by default: Rapier turns autostep off for cost, Godot has no step at all, and both are the top stairs complaints.

**Scripting.** The C# surface mirrors the components. The gap to avoid is O3DE's, where hit callbacks and some setters are C++-only.
