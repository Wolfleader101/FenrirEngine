# How does a ragdoll switch between following and simulating, and how is it powered?

Type: grilling
Status: claimed
Blocked by: 52, 53

## Question

How does a ragdoll move through its life each tick and frame, given ADR-0038 (overrides, override weights gameplay ramps, base-pose motor targets), ADR-0040 (tick-posed skeletons, glue in `Tick`, motion type as the switch) and the model from [How is a ragdoll modelled in the ECS, and where does the glue live?](52-ragdolls-in-the-ecs.md) and [From what description is a ragdoll built, and with which bodies, joints, motors and strengths?](53-ragdoll-description-bodies-joints-and-motors.md)?

- **Modes:** following (every body kinematic, moved each `Tick` to the simulation pose × offset: hit boxes), simulating (every body dynamic, the glue writing overrides each `Update`), powered (dynamic with motors driving towards the base pose), and partial (one subtree simulating under an animated parent: a limp arm). Whether mode is a field on the ragdoll (`RagdollMode mode`, with a per-part override) that the glue turns into motion types, motor modes and override weights, or whether game code sets `RigidBody::motionType` per body itself and the glue only reacts to what it finds.
- **The switch to simulating:** the bodies are already on their bones (they followed), so they start from the right pose; carrying the bones' velocity into the bodies (the kinematic move already gives Jolt a velocity; whether it survives the switch, or the glue sets it from the last two simulation poses), adding an impulse (a hit), and resetting joint warm starts. Whether the glue sets the override weight to 1 at the switch or leaves every weight to gameplay (ADR-0038: weights are gameplay's).
- **Partial ragdolls:** the subtree's bodies dynamic, the parent body kinematic on its bone, so the subtree's root joint hangs from a kinematic anchor (ADR-0040's "dynamic body following a bone through a joint to a kinematic anchor"); which bones get overrides (the subtree's) and weights (gameplay sets the subtree's to 1).
- **Powered ragdolls:** motor targets each tick: for each joint, the child bone's base-pose rotation relative to its parent bone (`GetBaseLocal`, or `GetBaseModel` when a body spans several bones, with `PoseKind::Simulation` on a tick-posed skeleton; a frame late on a frame-posed one), expressed in the joint's frames as `SwingTwistMotor::targetRotation` or `JointMotor::targetPosition`; whether the glue also feeds a target velocity (Jolt's `DriveToPoseUsingMotors` with a previous pose); where the root body gets its drive (none: it falls, or a world-space spring to the animated root for a "standing" active ragdoll); strength ramps (going limp over time by lowering strength, not only override weight).
- **Does the ragdoll require a tick-posed skeleton?** Following needs the simulation pose to avoid the lag ADR-0037 records; whether the glue requires `PoseTiming::Tick`, sets it, or works degraded on a frame-posed skeleton with a warning.
- **The skeleton entity follows the ragdoll:** in `Update`, before writing targets, the glue moves the skeleton entity so the root body's position (and yaw, or full rotation) lands on the entity, or on a chosen bone; whether the skeleton entity's own parent (a character entity with a `CharacterBody`) is moved instead; what the camera and game code hold on to; what happens to the character body while the ragdoll simulates (disabled, kept at the hips).
- **Getting up:** game code switches back to following; the bodies snap to the pose (a kinematic move from wherever they lie, or a teleport), while gameplay ramps the override weights down over time from the held last pose (ADR-0038); whether the glue offers a helper that places the skeleton entity under the hips first so the get-up animation starts where the body lies.
- **Sleep and removal:** sleeping bodies keep their last override; bodies removed stop being written; an inactive ragdoll (no skeleton) writes nothing and clears its entries.
- **Order:** the glue's systems and the phases they run in (`Tick` after FenrirSkeleton's `PreTick` pass and before physics's `PostTick`; `Update` after physics's `AfterTicks`), with no new scheduling (ADR-0040's rule).
- **What game code reads back:** mode per part, whether any body is awake, the root body; a `RagdollState`.
- **Vocabulary:** *following*, *simulating*, *powered*, *partial ragdoll*, *strength*; words to avoid (kinematic ragdoll, active ragdoll, blend in/out).

Probably an ADR (mode as a field the glue applies, or motion types game code owns, is hard to reverse).
