---
status: accepted
---

# A ragdoll's mode is a field its glue applies to motion types and motors

ADR-0053 makes a ragdoll ordinary bodies and joints mapped to bones by name, glued by FenrirRagdoll. ADR-0040 says a ragdoll's bodies are its hit boxes, "switched between kinematic and dynamic by `RigidBody::motionType`", following the simulation pose while kinematic and driving the render pose through overrides while dynamic, and leaves when each switches to this spec. ADR-0038 gives override weights to gameplay and keeps time out of FenrirSkeleton. ADR-0011 wants anything switched during play to be a field edit.

## Modes

- **`Ragdoll::mode`** is `RagdollMode::Following` (every body kinematic, moved each tick to the simulation pose × offset), `Simulating` (every body dynamic, the glue writing world override targets each frame) or `Powered` (dynamic, with motors driving towards the base pose). **`RagdollBody::mode`** is `Inherit` by default or one of the three, so a subtree can differ from its ragdoll: a **partial ragdoll** is a `Simulating` subtree under a `Following` parent, its bodies hanging from a joint to a kinematic anchor (ADR-0040's case).
- **The glue owns the bodies' motion types and motor modes.** Each tick it turns the effective mode per body into `RigidBody::motionType` (kinematic for following, dynamic otherwise) and each joint's motor mode (`Position` when its child body is powered, `Off` otherwise). Game code writes the mode, never those fields; a write to them is overwritten next tick, documented.
- **Switching repositions nothing.** Bodies already sit on their bones when they go dynamic, and the velocity of the last kinematic move carries over, since the backend keeps a body's velocity across a motion-type change. A shove is `PhysicsWorld::AddImpulse` on a body, as for any body. The glue never touches override weights: gameplay sets the weight to 1 (on the subtree, for a partial ragdoll) when it switches, and the glue only starts writing targets.

## Powered ragdolls

- Each tick, for every joint whose child body is powered, the glue reads the child bone's base-pose rotation relative to its parent bone (`GetBaseLocal` with `PoseKind::Simulation`, or `GetBaseModel` where middle slots are empty), re-expresses it in the joint's frames and writes `SwingTwistMotor::targetRotation` or `JointMotor::targetPosition`, with torque and springs from `strength` (the description decision). No target velocity in the first version. The root body gets no drive.

## Tick-posed skeletons are required

- Following needs the simulation pose, so a ragdoll requires `SkeletonPose::timing == PoseTiming::Tick`. The glue does not set it: on a frame-posed skeleton the ragdoll is inactive, with one warning naming the fix.

## The skeleton entity follows the ragdoll

- While any body simulates, in `Update` before writing targets, the glue moves the skeleton entity so that the root body's world position, and only its yaw, land where the hips bone's base pose puts the hips relative to the entity: the entity stays upright, where the character stood, under the tumbling body. `Ragdoll::followRoot` (default true) turns it off. The character body under it is game code's (the demo disables it on the switch and re-enables it on getting up).

## Getting up

- Game code sets `Following`. Bodies become kinematic and the next tick's move snaps them to the simulation pose; the skeleton entity is already under the hips, so a get-up animation starts there. Overrides hold the last simulated pose and gameplay ramps the weights down (ADR-0038); the glue clears its entries once every weight it reads is 0, or at once through a clear call.

## Order and state

- Systems in `Tick` (modes, following moves, motor targets) after FenrirSkeleton's `PreTick` pass and before physics's `PostTick`, and in `Update` (follow root, override targets) after physics's `AfterTicks`: phase boundaries, no new phases (ADR-0040's rule).
- `RagdollState state` on `Ragdoll` (`bool anyAwake`); each body's effective mode is on its `RagdollBody`.

## Considered options

- **Game code writes motion types itself, the glue reacting to what it finds.** Rejected: three fields per body per switch, and C# would need to know the mapping between motion types, motors and overrides.
- **The glue sets override weights at the switch.** Rejected: weights are gameplay's (ADR-0038); setting them here would make partial ragdolls and timed blends fight the glue.
- **An impulse or velocity field on `Ragdoll`.** Rejected: `AddImpulse` already exists for any body.
- **The glue switching the skeleton to tick-posed.** Rejected: it would silently change how the character's animation is clocked.
- **Following the root body's full rotation.** Rejected for yaw only: cameras and attachments stay sane while the body tumbles; game code can read the root body.
- **A get-up helper that plays an animation.** Rejected: FenrirRagdoll knows nothing about clips.

## Consequences

- *Refines ADR-0040:* when a ragdoll's bodies switch is `RagdollMode`, applied by FenrirRagdoll.
- Vocabulary (`GLOSSARY.md`): *following*, *simulating*, *powered*, *partial ragdoll*; avoid *kinematic ragdoll*, *active ragdoll*, *blend in/out*, *physics blend weight*.
- Listed, not built: motor target velocities from the previous base pose; a world-space upright drive for the root; a modifier stage on the simulation pose (ADR-0040).
