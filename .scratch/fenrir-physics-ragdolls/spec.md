Status: ready-for-agent

# FenrirRagdoll: ragdolls built from a skeleton as ordinary bodies and joints, following, simulating and powered, with general collision groups, in C++ and C#

Origin: the FenrirPhysics wayfinder map (`.scratch/fenrir-physics/map.md`), whose ragdoll tickets (49–57) this spec collapses into one buildable plan. ADR-0053 (ragdolls are ordinary bodies and joints mapped to bones by name, glued by FenrirRagdoll) and ADR-0054 (a ragdoll's mode is a field its glue applies to motion types and motors) record the hard-to-reverse calls, each with dated amendments from the spike. ADR-0009 (packages extend scripting through extensions), ADR-0010 (the backend is hidden at compile time), ADR-0011 (physics components are plain data that physics diffs), ADR-0013 (joints live on joint entities with explicit local frames), ADR-0014 (character bodies), ADR-0028 and ADR-0031 (skeletons as bone arrays in FenrirSkeleton), ADR-0033 and ADR-0038 (pose overrides and gameplay-owned override weights), ADR-0037 (bone attachments by name), ADR-0039 (C# reaches skeletons through mirrors and per-bone functions), ADR-0040 (tick-posed skeletons and bodies that follow bones) and ADR-0042 to ADR-0046 (the generated API description and extension shape) still apply. A throwaway spike (`prototype/fenrir-physics-ragdolls-spike`, head d983b77f) proved the model through the seam with a walking human that falls, goes limp by one arm, is powered and gets up, user-checked in the PhysicsDemo on 2026-11-03. It builds on the physics foundation, joints and character specs (deleted once built; commit 6210be2 is the last to hold them) and on the renderer map's animation spec (`git show 592bb8d9^:.scratch/fenrir-renderer-animation/spec.md`), and cites rather than restates them.

## Problem Statement

FenrirEngine animates skinned characters and simulates rigid bodies, joints and characters, but the two never meet. A character cannot die and crumple onto the stairs, trail a broken arm after a hit, brace against a shove, or get up where it fell. Game code cannot ask which bone a bullet hit, because nothing physical follows the bones. Joining the two by hand means a hundred lines per game of bone lookups, offset maths, motion-type flips, override writes and motor targets, and each game gets the timing subtly wrong: bodies lag the pose by a frame, a limp arm stretches from its shoulder, the body flies off when it goes dynamic, the character capsule fights its own corpse, and the skeleton entity ends up under the floor.

Other engines show the traps: descriptions authored against a non-rest pose that rot (Godot, Unity), a capsule chasing the pelvis, a controller colliding with its own ragdoll, limbs stretching, jitter from self-collision and mass ratios, get-up pops when blending back by weight alone, and ragdoll tooling that is a wizard with hand tuning after.

The engine's own rules add constraints: FenrirPhysics depends only on the core and FenrirSkeleton on no physics, so neither may know the other; Jolt's `Ragdoll` is built only from its own settings and cannot wrap bodies and joints that already exist; a Jolt body has one collision group, so the joints' pair-table filter and any ragdoll grouping must share one private filter; and a body that becomes dynamic takes the velocity its component holds, not its kinematic move's.

## Solution

A new package, **FenrirRagdoll**, depending on FenrirPhysics, FenrirSkeleton and the core, built only when physics is on. It owns the ragdoll components, a builder, the glue systems, a debug-line category and its own scripting extension with a `Fenrir.Ragdoll` contract. FenrirPhysics gains one thing: **general collision groups**, a `CollisionGroup` component holding one plain integer, where bodies with the same non-zero group never touch.

A **ragdoll** is ordinary bodies and joints: one `RigidBody` plus `Collider` entity per part, joined parent to child by ordinary `Joint` entities (hinges at elbows and knees, swing-twist elsewhere). Each body entity carries a `RagdollBody` naming its ragdoll, its bone by name and its offset from that bone; the skeleton entity carries a `Ragdoll`. Game code works the ECS way:

- **Making one:** fill a `HumanoidBones` map with the rig's bone names, call `DescribeHumanoid` to derive a plain-data `RagdollDescription` (sixteen parts with capsules, masses, limits and offsets from the rest pose), edit it if wanted, and call `BuildRagdoll`, which spawns the entities and returns them. Hand authoring fills the same description.
- **Modes:** `Ragdoll::mode` is `Following` (every body kinematic, moved each tick to the simulation pose, the character's hit boxes), `Simulating` (every body dynamic, the glue writing the simulated pose into the skeleton as world overrides) or `Powered` (dynamic, with motors driving towards the base pose at a `strength`). `RagdollBody::mode` overrides per body, so one limp arm is three bodies set to `Simulating`. The glue turns modes into motion types and motor modes; game code never writes those.
- **Weights are gameplay's:** how much the simulated pose shows is FenrirSkeleton's per-bone override weight (ADR-0038), which game code sets to 1 when it drops the character and ramps to 0 as it gets up. The glue only writes targets, and clears them when every weight is 0.
- **The skeleton entity follows the ragdoll** horizontally and in yaw while any body simulates, so a get-up animation starts where the body lies.
- **A shove** is `PhysicsWorld::AddImpulse` on a body, as for any body. A tick-posed skeleton (`PoseTiming::Tick`) is required; the glue never sets it.

Everything is available from C# through the ragdoll scripting extension: the components and `HumanoidBones` as writable mirrors, `RagdollState` read-only, and three natives, `DescribeHumanoid`, `Build` and `Destroy`. A new debug-line category, `Ragdolls`, draws each body's bone frame and offset coloured by mode, and powered motor targets. The PhysicsDemo gets two ragdoll scenes on the Quaternius human, one built in C++ and one built in C#.

## User Stories

### Collision groups (FenrirPhysics)

1. As a game developer, I want to put bodies in a collision group by giving each a `CollisionGroup` with the same integer, so that a set of bodies ignores each other without touching layers.
2. As a game developer, I want a group to remove only contact the layers already allow, so that groups and layers compose instead of fighting.
3. As a game developer, I want a `CollisionGroup` on a `CharacterBody` entity to apply to its move and its inner body, so that a character never collides with its own ragdoll.
4. As a game developer, I want a trigger to read the group of its own entity, so that a character's trigger can skip its group-mates.
5. As a game developer, I want changing or removing a group to be a field edit applied next tick with the usual contact events, so that grouping during play costs nothing.
6. As a game developer, I want queries to ignore groups, so that a raycast still finds my own ragdoll's bodies and `ignoreBody` and layers stay the way to skip one.
7. As a game developer, I want to pick group integers myself, so that there is nothing to allocate or free.

### Building a ragdoll from C++

8. As a game developer, I want to fill a `HumanoidBones` map with my rig's bone names, so that the same code builds a ragdoll on Mixamo, Quaternius or glTF-sample rigs.
9. As a game developer, I want `DescribeHumanoid` to derive a description from the skeleton asset and that map, so that I see a ragdoll fall on the first day and tune after.
10. As a game developer, I want the description to be a plain-data value I can edit before building, so that I can change a part's shape, mass, limits or strength scale without a second API.
11. As a game developer, I want a slot left empty to drop its part and the chain below it, so that a ragdoll without hands or feet is a smaller map, not a different code path.
12. As a game developer, I want `BuildRagdoll` to spawn the body, collider and joint entities and return them, so that everything it made is mine to edit, query, draw and destroy.
13. As a game developer, I want the spawned bodies to start kinematic and following, in one collision group with the skeleton entity's id, so that a freshly built ragdoll is a set of hit boxes that touches nothing of its own.
14. As a game developer, I want `DestroyRagdoll` to remove what the builder spawned, so that I do not track its entities myself.
15. As a game developer, I want a description to refuse a map whose parts do not form one tree under the hips, or a name no bone has, with an error and nothing built, so that a bad map never half-builds.
16. As a game developer, I want the default numbers (radii, masses, limits, springs) to be the spike's, so that a 70 kg human falls believably without tuning.
17. As a game developer, I want to hand-write a description for a non-humanoid, so that a quadruped or a tentacle uses the same builder.

### Modes and the glue

18. As a game developer, I want a ragdoll's bodies to follow the simulation pose each tick with no lag and the right velocity while following, so that hit boxes are where the character is drawn and push things with its speed.
19. As a game developer, I want to drop a character by setting `Ragdoll::mode = Simulating` and the override weights to 1, so that going limp is two writes.
20. As a game developer, I want the bodies to keep their following velocity when they go dynamic, so that a running character tumbles forward instead of dropping on the spot.
21. As a game developer, I want to shove a body with `AddImpulse`, so that a hit is the same call as for any body.
22. As a game developer, I want to set one body's mode to `Simulating` under a following parent, so that one arm hangs limp from the animated shoulder.
23. As a game developer, I want `Powered` to drive every joint's motor towards the base pose at a `strength`, so that a character braces, staggers and recovers.
24. As a game developer, I want one `strength` in [0, 1] on the ragdoll and a scale per body, so that strength means the same on every rig and a weak arm is a scale.
25. As a game developer, I want to get up by setting `Following` and ramping the weights down, so that the body snaps to the pose and the skin blends back over the time I choose.
26. As a game developer, I want the skeleton entity to follow the hips horizontally and in yaw while the ragdoll simulates, so that cameras, attachments and the get-up animation are where the body lies.
27. As a game developer, I want to turn following off with `followRoot = false`, so that a ragdoll in a cutscene leaves its entity where I put it.
28. As a game developer, I want the glue to clear its overrides once every weight it reads is 0, so that nothing stale holds the pose after a get-up.
29. As a game developer, I want a ragdoll on a frame-posed skeleton to be inactive with one warning naming the fix, so that a wrong timing is a log line, not a frame of lag.
30. As a game developer, I want a body naming a bone the skeleton lacks, or an entity without a `RigidBody`, to be skipped with one warning, so that a renamed bone is a log line.
31. As a game developer, I want to read `RagdollState::anyAwake` and each body's effective mode and bone, so that I know when the body has settled and which part a contact names.
32. As a game developer, I want a raycast or contact event on a ragdoll body to give me its entity, so that `RagdollBody::bone` tells me the bone hit.
33. As a game developer, I want a character that never ragdolls to keep a following `Ragdoll`, so that hit boxes are the same mechanism.
34. As a game developer, I want a ragdoll body destroyed mid-simulation to leave its bone at its last override, so that losing a part never snaps the pose.
35. As a game developer, I want a cycling locomotion clip not to drive a powered ragdoll's legs into the ground, so that the docs tell me to pause or swap the clip when I power it.

### Using ragdolls from C#

36. As a script author, I want `CollisionGroup`, `Ragdoll`, `RagdollBody` and `HumanoidBones` as writable mirrors with C++'s defaults, so that grouping and modes are field writes.
37. As a script author, I want `Ragdoll.DescribeHumanoid`, `Ragdoll.Build` and `Ragdoll.Destroy`, so that I can build a ragdoll from C# alone.
38. As a script author, I want `RagdollState` read-only through `ref readonly`, so that I cannot write what the glue owns.
39. As a script author, I want `RagdollBody.Bone` to be the skeleton contract's `BoneName`, so that one bone-name type serves attachments and ragdolls.
40. As a script author, I want silent no-ops without a world or a ready skeleton, as the other extensions give, so that a script running before the scene is ready does nothing wrong.
41. As a script author, I want doc notes on switching and weights, partial ragdolls, strength, getting up and tick-posed skeletons, so that the first ragdoll script works.

### Engine-wide

42. As an engine developer, I want FenrirPhysics and FenrirSkeleton to stay unaware of each other, so that each is built and tested alone.
43. As an engine developer, I want nothing of Jolt's `Ragdoll` or skeleton classes behind the seam, so that a second backend needs only bodies, joints, motors and a per-pair collision hook.
44. As an engine developer, I want the glue's systems on phase boundaries (`Tick` between FenrirSkeleton's `PreTick` pass and physics's `PostTick`, `Update` after `AfterTicks`), so that no new phases or install order are needed.
45. As an engine developer, I want the same inputs to give the same ragdoll on the same machine and build, so that two runs are bit-identical.
46. As an engine developer, I want a `Ragdolls` debug-line category, so that mode and offsets are visible without reading names.

### PhysicsDemo

47. As someone evaluating the engine, I want to drop a walking human into a ragdoll, make an arm limp, power it and get it up, so that I see the whole life of a ragdoll.
48. As someone evaluating the engine, I want to shoot a ray and see the bone it hit logged, so that I see hit boxes work.
49. As someone evaluating the engine, I want the same scene built only from C#, so that I see the scripting surface is complete.
50. As someone evaluating the engine, I want two ragdolls that collide with each other and ignore their own characters, so that I see collision groups work.

## Implementation Decisions

### Prerequisites

- **The demo assets.** The ragdoll scenes use the Quaternius human and its clips, which AnimationDemo already fetches under `FENRIR_FETCH_DEMO_ASSETS` (ADR-0052). The PhysicsDemo adds them to its own fetch list (its asset root), and its ragdoll scenes are skipped with the scaffold's skipped line when the assets are not fetched, so CI without assets passes.
- **No FenrirScripting prerequisite.** Read-only fields through `ref readonly`, entity links, the `[TaggedUnion]` generator and the generated extension shape (ADR-0046) are built. `BoneName` is mirrored by the skeleton binding (ADR-0037, ADR-0044).
- **Nothing new in FenrirSkeleton or FenrirAnimation.** The glue uses `Skeletons::FindBone`, `GetBoneWorldMatrix` with `PoseKind::Simulation`, `GetBaseLocal` and `GetBaseModel`, `SetOverride`, `ClearOverride`, `GetOverrideWeight`, `GetSubtree` and `GetPoseState`, all of which exist.

### FenrirPhysics: general collision groups (ticket 51)

```cpp
struct CollisionGroup // optional, on a body entity
{
    std::uint32_t group = 0; // 0 is no group; equal non-zero groups never touch
};
```

- Two bodies whose groups are equal and non-zero never touch, whatever their layers allow; a group only removes contact. One group per body. No component, or 0, is no group.
- Game code picks the integer; physics hands out nothing. Groups are compared within one physics world.
- A group means only "ignore each other". No "collide within, except these pairs" mode.
- A `CollisionGroup` on a `CharacterBody` entity applies to its move and its inner body. A trigger reads the `CollisionGroup` of its own entity, as it reads its own `CollisionFilter`.
- Queries ignore groups, as they ignore `collideConnected`.
- A field edit applied next tick, no rebuild; a pair that stops colliding gets a normal `ContactEnd`, and a pair that resumes wakes both bodies and gets a `ContactBegin`.
- **Privately** the joints' excluded-pair filter grows a body-index-to-group table consulted beside its pair set: `CanCollide` refuses equal non-zero groups, then the pair set. A body carries the Jolt group while in any excluded pair or any group, so filters never mix (Jolt asks the first non-null filter of a pair, and a body holds one). The character's inner body carries the group and its move takes a body filter over the same rule. Diffed per tick with the body and character edits.
- `CollisionGroup` is mirrored writable in `Fenrir.Physics`; no native. The API description and the binding gain it.
- Glossary: *collision group* (already reserved) names the component; avoid ignore list, exclusion, sub-group, collision exception.

### FenrirRagdoll: the components (ADR-0053, ADR-0054)

```cpp
enum class RagdollMode : std::uint8_t { Following, Simulating, Powered };
enum class RagdollBodyMode : std::uint8_t { Inherit, Following, Simulating, Powered };

struct RagdollState // written by the glue
{
    bool anyAwake = false;
    std::int32_t bodies = 0;   // bodies the glue drives this tick
    bool inactive = false;     // no ready tick-posed SkeletonPose
};

struct Ragdoll // on the skeleton entity (the SkeletonPose entity); one per skeleton
{
    RagdollMode mode = RagdollMode::Following;
    float strength = 1.0f;         // [0, 1], Powered only
    bool followRoot = true;        // the skeleton entity follows the root body while it is not following
    std::uint32_t group = 0;       // the collision group the builder gave its bodies (informational)
    Entity rootBody;               // the hips' body entity
    RagdollState state;
};

struct RagdollBodyState // written by the glue
{
    RagdollMode mode = RagdollMode::Following; // the effective mode
    std::int32_t bone = NoBone;                // the resolved bone index
};

struct RagdollBody // on each body entity (a RigidBody + Collider entity)
{
    Entity ragdoll;                                   // the skeleton entity carrying the Ragdoll; explicit, never "my parent"
    BoneName bone;                                    // the bone it follows and drives, by name
    Transform offset;                                 // the body's pose relative to its bone
    RagdollBodyMode mode = RagdollBodyMode::Inherit;  // per-body override of the ragdoll's mode
    float strengthScale = 1.0f;
    RagdollBodyState state;
};
```

- **Bodies and joints are ordinary.** Each ragdoll body is a `RigidBody` plus `Collider` entity; each joint between a body and its parent body is an ordinary `Joint` entity with `collideConnected = false`. Events, queries, materials, breaking, debug lines and the C# physics surface apply unchanged. Nothing is a private Jolt `Ragdoll` (rejected in ADR-0053: it is made from its own settings and every runtime method is a loop the glue writes).
- **The mapping** is `RagdollBody` on each body entity, the bone by name resolved per instance through `Skeletons::FindBone`. A table on the `Ragdoll` is rejected (no growable arrays in blittable components).
- **The offset** is stored once and used both ways: following writes `bone × offset`; simulating writes `body × offset⁻¹` as the override target. The glue never recalibrates it; a rest-pose change in the skeleton asset leaves offsets stale until the ragdoll is rebuilt, documented.
- **Inactive** (writes nothing, warns once per transition): a `Ragdoll` on an entity with no ready `SkeletonPose`, or one whose `timing` is `Frame` (the warning names `PoseTiming::Tick`). A `RagdollBody` whose bone name matches nothing, whose entity has no `RigidBody`, or whose `ragdoll` is not a `Ragdoll` entity in the same scene is skipped with one warning and `state.bone = NoBone`.
- **The glue owns** `RigidBody::motionType`, each joint's motor mode and targets, following bodies' `RigidBody::linearVelocity` and `angularVelocity`, the bodies' `Transform`s while following, the skeleton entity's `Transform` while following the root, and its `PoseOverride` entries. Game code writes the modes, the weights and everything else. A write to a glue-owned field is overwritten next tick, documented.
- **Physics adds or removes nothing**; the builder and `DestroyRagdoll` are the only structural changes, and they are explicit calls.
- **Lifetime:** bodies live in the skeleton's scene. Destroying the skeleton entity or removing `Ragdoll` leaves the body entities (now inactive `RagdollBody`s, warned once); `DestroyRagdoll` removes them. A body destroyed while simulating stops being written, and its bone keeps its last override (ADR-0038).

### FenrirRagdoll: the description and the builder (ticket 53, amended by the spike)

```cpp
struct HumanoidBones // named slots game code fills by bone name; any may be empty
{
    BoneName hips, spine, chest, head;
    BoneName leftUpperArm, leftLowerArm, leftHand, rightUpperArm, rightLowerArm, rightHand;
    BoneName leftUpperLeg, leftLowerLeg, leftFoot, rightUpperLeg, rightLowerLeg, rightFoot;
};

struct RagdollPart
{
    BoneName bone;
    BoneName parent;            // empty for the root
    CollisionShape shape;       // a capsule by default; any kind
    Transform offset;           // body relative to bone, from the rest pose
    float mass;                 // kg
    JointKind joint;            // to the parent: HingeJoint or SwingTwistJoint with limits
    JointFrame jointFrame;      // the joint's rest frame in model space (axis X: hinge axis or twist axis)
    Math::Vec3 jointAnchor;     // the joint's anchor in model space (the child bone's origin)
    float strengthScale = 1.0f;
};

struct RagdollDescription // a plain-data value, not an asset
{
    std::vector<RagdollPart> parts;   // root first, each parent before its children
    std::uint32_t group = 0;          // 0: the builder uses the skeleton entity's id
    float jointFriction = 0.5f;       // N m, resting stiffness
    float strength = 1.0f;
    std::string error;                // non-empty: DescribeHumanoid refused, and BuildRagdoll builds nothing
};

RagdollDescription DescribeHumanoid(const SkeletonData& skeleton, const HumanoidBones& bones, float totalMass = 70.0f);
SpawnedRagdoll BuildRagdoll(App& app, Scene& scene, Entity skeleton, const RagdollDescription& description);
void DestroyRagdoll(Scene& scene, Entity skeleton);
```

- **The derivation**, `DescribeHumanoid`:
  - Sixteen parts when every slot is filled: hips, spine, chest, head and three per limb; hands and feet are in the default. An empty slot drops its part and skips the chain below it. A body spans one bone; spanning several is done by leaving the middle slots empty (the unmapped bones keep their animated local pose under the overridden parent, ADR-0038).
  - A capsule per filled slot from the bone to its child in the map; leaf parts (head, hands, feet) run to the bone's first child in the skeleton, or along the parent bone's direction for 0.10 to 0.13 × the **stature** (the head-to-foot rest distance). Radii are fractions of the stature, not of bone length, because torso bones are too short: hips 0.065, spine 0.055, chest 0.06, head 0.055, upper arm 0.025, lower arm 0.022, hand 0.02, upper leg 0.038, lower leg 0.03, foot 0.025; capsule half-height is length/2 − radius (minimum 0.005 m).
  - Mass from `totalMass` split by fixed fractions: hips 0.14, spine 0.12, chest 0.22, head 0.08, upper arm 0.03, lower arm 0.02, hand 0.01, upper leg 0.10, lower leg 0.045, foot 0.015 (each limb part per side; the fractions sum to 1). Dropped parts' fractions are redistributed proportionally.
  - Offsets from the rest pose: `offset = bone⁻¹ × body`.
  - Joints parent to child at the child bone's origin, hinges for elbows and knees, swing-twist elsewhere, with `jointFrame` and `jointAnchor` in model space (the hinge and twist axes cannot be derived from `JointKind` at build time). Forward is where the feet point (a foot bone's first child's horizontal direction), else +Z; the hinge bend side is found by rotating the limb a little about the axis and checking against forward.
  - Default limits (cone half-angles Y = Z / twist ±): spine and chest 20°/20°, head 30°/45°, shoulder 80°/45°, wrist 35°/20°, hip 45°/25°, ankle 30°/10°; hinges (elbow, knee) 0 to 140° on the bend side. Motors off. `collideConnected = false`.
  - Refuses (sets `error`, empty `parts`) a map whose filled slots do not form one tree rooted at the hips, or a name no bone has. It never guesses a slot by substring.
  - Jolt's `Stabilize` facts (ticket 49): mass ratios between parent and child parts within 0.8 to 1.2 reduce jitter. The derivation's fractions already keep adjacent parts within that band except at the hands and feet; the docs note the advice for hand-written descriptions. No automatic rebalancing.
- **The builder**, `BuildRagdoll`: takes the `App` for the asset store (the rest pose is the skeleton asset's); refuses a description with an `error` or a skeleton entity without a ready `SkeletonPose`. It spawns, under a parent entity it makes in the skeleton's scene, one body entity per part (`RigidBody` kinematic, `Collider` with the part's shape, `RagdollBody` with the part's bone, offset and strength scale, `CollisionGroup` with the description's group or the skeleton entity's id) and one `Joint` entity per non-root part, root first, frames from the part's model-space frame and anchor through each body's rest world pose (never captured from the scene later), the joint's friction set, motors off. It puts `Ragdoll` on the skeleton entity (`Following`, `group`, `rootBody` = the root part's entity, `strength` from the description) and returns `SpawnedRagdoll { Entity parent; std::vector<Entity> bodies; std::vector<Entity> joints; std::string error; }`. Bodies are placed on their bones at once (the rest pose × offset, then the first tick's follow). Joints are spawned root first, so solve order follows the tree (the joints spec's entity-order priority); no solver tuning.
- **`DestroyRagdoll`** removes the parent entity and everything the builder spawned under it, clears the glue's overrides and removes `Ragdoll`. Bodies game code added by hand elsewhere are left.
- **Hand authoring** fills `RagdollDescription` directly; the builder treats it the same. The description is not an asset kind now.
- **Strength:** each tick, for a powered body, its joint's motor gets `maxTorque` (or `maxForce`) = `strength × strengthScale × 30 N m` per kilogram of the child body, spring frequency `lerp(1 Hz, 10 Hz, strength)`, damping 1. The raw `JointMotor` and `SwingTwistMotor` fields stay reachable on the joint entities; the glue overwrites them only while the body is powered.
- Glossary: *ragdoll description*, *ragdoll part*, *humanoid bone map*, *strength*, *stature*; avoid physics asset, wizard, profile, rig.

### FenrirRagdoll: the glue each tick and frame (ADR-0054, amended by the spike)

`Ragdolls::Install(App&)` registers the systems; the demo scaffold installs it where physics is installed.

- **`Tick`** (after FenrirSkeleton's `PreTick` tick pass, before physics's `PostTick`), per active ragdoll, bodies in the order the builder spawned them (root first):
  1. **Resolve.** Bone indices from names on the first tick and after a `SkeletonPose` rebuild (`GetPoseState`); the ragdoll inactive on a frame-posed or not-ready skeleton.
  2. **Modes.** The effective mode per body (`Inherit` takes the ragdoll's); `RigidBody::motionType` kinematic for following, dynamic otherwise; each joint's motor mode `Position` when its child body is powered, `Off` otherwise. A body switched to kinematic from dynamic is not repositioned here; the follow below moves it over the next step (physics's kinematic move).
  3. **Follow.** For each following body: `GetBoneWorldMatrix(skeleton, bone, PoseKind::Simulation) × offset`, into the body entity's parent space, decomposed, written to its `Transform`; its `RigidBody::linearVelocity` and `angularVelocity` written from this and the previous following pose over `tickRate`, so the velocity carries over when the body goes dynamic (a body that becomes dynamic takes the velocity its component holds).
  4. **Motor targets.** For each powered body: the child bone's base-pose rotation relative to its parent bone (`GetBaseLocal` with `PoseKind::Simulation`; `GetBaseModel` where middle slots are empty, both bones' model matrices), re-expressed in the joint's frames, written as `SwingTwistMotor::targetRotation` or `JointMotor::targetPosition`, with the strength's torque and spring. No target velocity; the root body gets no drive.
  5. **State.** `RagdollState` and each `RagdollBodyState`.
- **`Update`** (after physics's `AfterTicks`), per active ragdoll:
  1. **Follow root.** While the root body is not following and `followRoot` is true (*amended by ticket 03, 2026-11-12*: "any body" would make a partial ragdoll's following root drag back an entity its character moved; ADR-0054): the skeleton entity is moved so that the root body's horizontal world position and yaw land where the hips bone's base pose puts the hips relative to the entity; the entity keeps its height and stays upright. The character body under it is game code's (the demo disables it on the drop and teleports and re-enables it on getting up).
  2. **Override targets.** For each simulating or powered body: `body (interpolated world) × offset⁻¹` as `SetOverride(skeleton, bone, Target { World, trs, Translation | Rotation, weight 1 })`. The glue never writes override weights.
  3. **Clearing.** When every body is following, the glue keeps its entries (holding the last simulated pose for the blend-back) until every weight of the bones it wrote reads 0, then clears them. `Ragdolls::ClearOverrides(scene, skeleton)` clears at once.
- **Switching repositions nothing.** Bodies already sit on their bones when they go dynamic and keep the velocity written in step 3. Override weights stay gameplay's: the docs say "set the weights to 1 when you drop, ramp them to 0 as you get up".
- **Partial ragdolls** follow from per-body modes: a simulating subtree under a following parent hangs from its joint to the kinematic parent (ADR-0040's case). The glue writes overrides for the simulating bodies' bones only; gameplay sets that subtree's weights.
- **Powered** drives towards the base pose, whatever animation produces: game code pauses or swaps a cycling locomotion clip for an idle or get-up pose when it powers the ragdoll (the spike's motors tracking a walk made the legs step on the ground). No second pose source on `Ragdoll`.
- **Sleep:** a sleeping simulating body is still written each frame (its interpolated pose is still); `anyAwake` reports the bodies' sleep.
- **Scenes:** the glue runs on the active scene only, as FenrirSkeleton's passes do; an inactive scene's ragdoll keeps its state.
- Glossary: *ragdoll mode* with *following*, *simulating*, *powered* and *partial ragdoll*; avoid kinematic ragdoll, active ragdoll, blend in/out, physics blend weight.

### FenrirRagdoll: debug lines (ticket 57)

- A new `PhysicsDebugCategories::Ragdolls` bit (C++ and C#, `All` includes it), chosen as the other categories are. FenrirRagdoll draws it into the scene's `DebugDraw` in the `Physics` group with duration 0, after its `Update` work: each body's bone frame (small axes at the bone's simulation world matrix) and a line from the bone to the body's origin, coloured by effective mode (following grey, simulating orange, powered green); for powered bodies, the motor's target as a small frame at the joint. Inactive or skipped bodies draw nothing. Joints and colliders keep their categories.

### FenrirRagdollScripting and Fenrir.Ragdoll (ticket 56)

- A separate `FenrirRagdollScripting` library and `Fenrir.Ragdoll` contract in ADR-0046's generated shape, built only with physics, assets and scripting on, installed by the demo scaffold beside the physics extension. Its API description (`api/`) is the artefact ADR-0042 asks for, whether or not scripting is built.
- **Mirrors:** `CollisionGroup` (in `Fenrir.Physics`), `Ragdoll` (mode, strength, followRoot, group, rootBody) writable with `RagdollState` read-only through `ref readonly`; `RagdollBody` writable (reusing `Fenrir.Skeleton`'s `BoneName`, ADR-0044) with `RagdollBodyState` read-only; `RagdollMode` and `RagdollBodyMode` enums; `HumanoidBones` as a plain struct of sixteen `BoneName` slots. Each `new` equals C++'s `{}`.
- **Natives:** `Ragdoll.DescribeHumanoid(Entity skeleton, in HumanoidBones bones, float totalMass)` derives from the entity's skeleton asset and returns a `RagdollDescriptionHandle` (a native resource, ADR-0029 and ADR-0043), or `None` with a warning; `Ragdoll.Build(Entity skeleton, RagdollDescriptionHandle)` spawns and returns the body and joint entities through the usual span-out pattern (the caller's buffers) or a count, and drops the handle; `Ragdoll.Destroy(Entity skeleton)`; `Ragdoll.ClearOverrides(Entity skeleton)`. Editing a description before building is C++-only in this version (C# edits the spawned entities); a described-but-not-built handle is dropped at scene end.
- **No-op rules:** the foundation's silent no-ops without a world or body, ADR-0039's for a skeleton that is not ready (a warning once, nothing built).
- **Doc notes** in the contract: switching and weights (the glue never sets weights), partial ragdolls, strength, getting up, tick-posed required, swapping the clip before powering.

### PhysicsDemo: the two ragdoll scenes (ticket 57)

The PhysicsDemo links FenrirSkeleton, FenrirAnimation and FenrirRagdoll (and their extensions with scripting), fetches the Quaternius human and its clips (idle, walk) into its asset root, and adds "Ragdolls (C++)" and "Ragdolls (C#)" after the vehicle scenes in the scaffold's list. Both are skipped with the scaffold's skipped line without the assets.

- **Ragdolls (C++)** (the spike's scene rewritten): a tick-posed human walking a 6 m lane on a capsule `CharacterBody`, both in one collision group, the root bone pinned at its rest translation in a hosted `Animate` item after the sampler (or an in-place clip), its 16-body ragdoll following as hit boxes; Space drops it (`Simulating`, every weight 1, a 120 N s shove on the chest, the character disabled); A toggles the left arm limp (three bodies `Simulating`) and blends it back over 1 s; K powers it, switching the clip to idle, with 1 and 2 stepping `strength` by 0.1; G gets up (`Following`, weights 1 → 0 over 1.5 s by demo code, the character teleported under the skeleton entity and re-enabled); H casts a ray from the camera and logs the bone hit through `RagdollBody::bone`; 3 drops a bystander human in a group of its own into the lane; B drops a 5 kg crate on the head; L logs state; three crates and a low wall in the lane; readouts in entity names (mode, strength, body count, awake, hips weight, hips body position, skeleton entity position, the character's enabled and ground); the scaffold's follow camera; P autopilots A, A, Space, K, G, 3, B, L over about 17 s for the smoke run; a `CheckSceneLog.cmake` entry for its lines.
- **Ragdolls (C#)**, built only from C# through `Fenrir.Ragdoll`: the same human described and built from C# (`DescribeHumanoid`, `Build`), a drop and get-up cycle on keys with the weight ramp written in C#, a partial ragdoll on one arm, a powered ragdoll with strength stepping on the idle clip, a second ragdoll in another group that collides with the first and ignores its own character, a bone-name hit log from a raycast; readouts in names in the C++ format; P autopilot; its own `CheckSceneLog.cmake` entry.
- F2's cycle gains "ragdolls". The README gets both scenes in the other families' format with the hand-check list below.

## Testing Decisions

- **What a good test is:** as in the foundation and the animation spec. It drives the engine through its public interface and asserts on component values (modes, states), world transforms (bodies, the skeleton entity), pose reads (`GetBoneWorldMatrix`, `GetOverride`), events, query results and debug lines. Never on private state or which backend calls were made. Tests run against real Jolt and real FenrirSkeleton with no renderer. Same-build determinism lets tests assert exact results; positions after a fall use a tolerance.
- **Seam 1, FenrirPhysics's App stepped a frame at a time** (`FenrirPhysicsTests`, prior art `PhysicsTestApp`, `PhysicsProbe` and `JointCollisions.cpp`), for collision groups: two boxes in one group fall through each other and in different groups stack; 0 and no component collide; a group removes only contact the layers allow; a character in its ragdoll's group walks through its bodies and its move ignores them; a trigger on a grouped entity skips group-mates; a raycast still hits a group-mate; changing a resting pair's group gives `ContactEnd`, removing it wakes both and gives `ContactBegin`; a grouped body in an excluded joint pair keeps both rules; two runs bit-identical.
- **Seam 2, FenrirRagdoll's App with physics and skeletons and no renderer** (`FenrirRagdollTests`, a new test executable, prior art FenrirSkeleton's `SkeletonTest.hpp`, `TickPosed.cpp` and `PoseOverride.cpp`, the physics `PhysicsTestApp`, and the spike's `[ragdoll-spike]` and `[demo-rig]` cases): a code-built chain skeleton and the Quaternius-shaped test rig (built in code, never the downloaded asset, ADR-0052):
  - the derivation: sixteen parts from a full map, fewer from a partial one with fractions redistributed, radii and masses as specified, hinges at elbows and knees, offsets placing bodies on bones at rest, an error for a map not rooted at the hips or an unknown name, nothing built on an error;
  - the builder: the entities spawned under a parent, kinematic, grouped, `Ragdoll` on the skeleton entity with `rootBody`, joints root first with `collideConnected` false and friction set; `DestroyRagdoll` removing them and clearing overrides; a hand-written description building the same;
  - following: bodies on their simulation bones every tick with no lag at one and several ticks per frame, velocities written from consecutive poses, a body's `Transform` write overwritten; inactive with one warning on a frame-posed skeleton and reactivation on `Tick`; a renamed bone skipped with one warning;
  - switching: `Simulating` makes bodies dynamic without moving them and they keep their following velocity; overrides on the simulating bodies' bones match the bodies through `offset⁻¹`; weights untouched by the glue; a partial arm hanging from a following shoulder within a tolerance of its joint anchor; `Following` again snaps bodies to the pose over one tick; entries held while a weight is non-zero and cleared once all are 0; `ClearOverrides` at once;
  - powered: motor targets equal to the base pose's child-relative rotations in joint frames, torque and spring from `strength` and `strengthScale`, a powered ragdoll holding a static pose against gravity above a floor at strength 1 and sagging at 0, no target velocity, the root undriven;
  - follow root: the skeleton entity moved horizontally and in yaw under the root body while simulating, height kept, not moved with `followRoot = false`, not moved while following;
  - lifetime: a body destroyed mid-simulation leaving its last override; `Ragdoll` removed leaving the bodies inactive with one warning;
  - determinism: a fall onto boxes over 300 ticks bit-identical in two runs, single- and multi-threaded;
  - debug lines: counts and colours for a known ragdoll per mode, nothing for inactive bodies.
- **Seam 3, the scripting fixture harness** (`RagdollScriptingTests`, prior art the physics and skeleton `ScriptingTests`, `ScriptingProbe`, `HeadlessApp`, `fenrir_expect_compile`): the mirrors validate against their native layouts and each `new` equals C++'s `{}`; `CollisionGroup` written from C# groups bodies; a C# system fills `HumanoidBones`, calls `DescribeHumanoid` and `Build` on a code-built rig, gets the entities, sets `Mode = Simulating` and reads `State.AnyAwake`; `Destroy` removes them; natives are no-ops with a warning on an unready skeleton; compile-fail cases for writing `Ragdoll.State` or `RagdollBody.State` through `Ref<T>`.
- **The PhysicsDemo scenes are verified by hand**, as the spike was:
  - Switching and smoke runs: PageDown reaches both ragdoll scenes; `FENRIR_DEMO_SCENE` with `FENRIR_DEMO_KEYS=1` runs each with no errors, Debug and Release; the CTest scene-log checks pass; without the assets both scenes report skipped; without scripting only the C++ scene is listed.
  - Ragdolls (C++): the hit boxes follow the walk with no lag under F2 "ragdolls" and "colliders"; Space makes the character go limp and tumble forward without popping or flying; A hangs the arm from the shoulder without stretching and blends it back; K holds the idle pose and recovers from a B crate; G stands the character up where it lies with no jump; H logs the bone hit; 3's bystander collides with the first ragdoll and neither touches its own character; two smoke runs print identical readouts; regressions to watch: the walk's root translation sliding the hit boxes, and powered legs stepping while a locomotion clip plays.
  - Ragdolls (C#): C# builds the scene with no warning, and each key does what the C++ scene's does.

## Out of Scope

- Self-colliding ragdolls (parts stopped by the torso), a within-group pair table backed by Jolt's `GroupFilterTable`, and a group-aware query filter.
- Physics handing out group ids.
- A ragdoll description as an asset kind, editing a derived description from C#, and offsets recalibrated across skeleton rest-pose changes.
- Several ragdolls on one skeleton; a body spanning several bones other than by empty slots.
- Motor target velocities (Jolt's `PositionAndVelocity`), a world-space upright drive for the root, and a "hold this pose" source for `Powered`.
- Automatic mass rebalancing (Jolt's `Stabilize`) and constraint priorities.
- Get-up animations, pose snapshots, root-motion extraction (ADR-0034) and clip selection: game code's.
- Modifiers on the simulation pose (IK on hit boxes, ADR-0040).
- Scaling the skeleton entity while a ragdoll is live.
- A separate hit-box component, hit events with bone names (the body entity and `RagdollBody::bone` suffice), and damage.
- Soft bodies: their own spec, after the renderer's dynamic meshes.
- Network prediction and replication of ragdolls.

## Further Notes

- **ADR-0053 and ADR-0054 already carry the spike's amendments** (dated 2026-11-03): `RagdollBody` has a mode override and `strengthScale`; the builder takes the `App`; the glue writes following velocities each tick; the skeleton entity follows horizontally; powered ragdolls need a non-cycling clip. The build changes nothing in them unless it learns otherwise, and then amends them as `CODING_STANDARDS.md` asks.
- **Decision history:** the wayfinder map `.scratch/fenrir-physics/map.md` and its ragdoll tickets 49–57 (in history once this spec is built); ADR-0053 and ADR-0054; research branches `research/jolt-ragdolls` (e4fc031f) and `research/engine-ragdoll-models` (e4188617); the spike branch `prototype/fenrir-physics-ragdolls-spike` (d983b77f), the primary source for the slice's shape, never merged.
- **Where tickets disagreed,** the later one is followed: sixteen parts (55, over 53's fifteen); `strengthScale` on `RagdollBody` and `jointFrame`/`jointAnchor` on the part (55, over 53); `BuildRagdoll(App&, …)` (55, over 53's `(scene, …)`); horizontal follow keeping height (55, over 54's full position); the glue writing velocities (55, over 54's "carries over by itself"); radii from stature (55, over 53's "fraction of the length").
- **Risks:**
  - The velocity write and the kinematic move interact: a body switched to dynamic in the same tick the follow wrote its `Transform` must not be teleported by physics's edit detection; the spike handled it, and the build's test for "switch keeps velocity" guards it.
  - Following the root in `Update` moves the skeleton entity after physics interpolated the bodies; the pose write in `PostUpdate` then uses the moved entity. The order is a phase boundary and needs no scheduling, but the test "follow root keeps height" should run at several ticks per frame.
  - The C# `Build` returns a variable number of entities; the span-out pattern or a count-then-fill pair is the extension's call, within ADR-0044's rules.
  - The Quaternius clips' root translation is a demo problem, pinned or replaced; the engine gains no root-motion extraction here.
  - MSVC Debug was the spike's only build; Clang, Release and Linux are CI's.
- **Engine changes land outside FenrirRagdoll:** `CollisionGroup` in FenrirPhysics (its component, filter, character body filter, API description and `Fenrir.Physics` mirror), `PhysicsDebugCategories::Ragdolls` in FenrirPhysics's enum, and the PhysicsDemo (dependencies, assets, two scenes, the F2 entry, README). `CollisionGroup` is independently useful and lands first.
- **Vocabulary** follows `GLOSSARY.md`: collision group, ragdoll, ragdoll body, ragdoll glue, ragdoll description, ragdoll part, humanoid bone map, strength, ragdoll mode (following, simulating, powered, partial ragdoll), stature (new, with this spec), plus the physics and animation sections' terms. Avoid "constraint" for a joint, "joint" for a bone, "part" for a live body, "physical bone", "physics asset", "rig", "kinematic ragdoll", "active ragdoll", "blend weight" for an override weight, "ignore list" for a group, and "hitbox" for a collider that is not a ragdoll body.
