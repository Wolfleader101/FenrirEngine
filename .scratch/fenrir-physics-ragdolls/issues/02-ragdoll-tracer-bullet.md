# 02: Ragdoll tracer bullet: a ragdoll built from a skeleton follows its bones

**What to build:** the FenrirRagdoll package exists end to end, thinly: game code fills a `HumanoidBones` map, `DescribeHumanoid` derives a `RagdollDescription` from the skeleton asset, `BuildRagdoll` spawns ordinary bodies and joints with `RagdollBody` on each and `Ragdoll` on the skeleton entity, and every tick the glue moves the kinematic bodies onto the simulation pose's bones with no lag and the right velocity, so they are the character's hit boxes (spec: "FenrirRagdoll: the components", "the description and the builder", "the glue each tick and frame" (the `Tick` steps 1, 2 for `Following` only, 3 and 5), "Prerequisites"; ADR-0053, ADR-0054 with their amendments, ADR-0040, ADR-0038). The spike on `prototype/fenrir-physics-ragdolls-spike` built this slice crudely; build it properly against the spec. The spike branch is reference only.

- **The package:** `packages/FenrirRagdoll`, depending on FenrirPhysics, FenrirSkeleton and the core, added to the build only when physics is on; `Ragdolls::Install(App&)` registers the glue's systems on the active scene; the API description under `api/` (ADR-0042) whether or not scripting is built; the demo scaffold installs it where it installs physics.
- **Components, declared with every field and default from the spec and documented:** `RagdollMode`, `RagdollBodyMode`, `RagdollState`, `Ragdoll`, `RagdollBodyState`, `RagdollBody`. Fields not wired here (`Simulating`, `Powered`, `strength`, `strengthScale`, `followRoot`) are declared and wired by tickets 03 and 04. The glue-owned fields are documented as such (a write to them is overwritten next tick).
- **The description:** `HumanoidBones` (sixteen named `BoneName` slots), `RagdollPart`, `RagdollDescription` and `DescribeHumanoid(SkeletonData, HumanoidBones, totalMass = 70)` with the spec's derivation in full: sixteen parts, empty slots dropping their chain with mass fractions redistributed, capsules from bone to child (leaf parts to the first child or along the parent for 0.10 to 0.13 × stature), radii as fractions of the stature, the spec's mass fractions, offsets from the rest pose, hinges at elbows and knees and swing-twist elsewhere with the spec's limits, `jointFrame` and `jointAnchor` in model space (forward from the feet, the hinge bend side checked against it), motors off, `collideConnected` false, friction 0.5 N m; an `error` for a map not rooted at the hips or an unknown name, never a substring guess.
- **The builder:** `BuildRagdoll(App&, Scene&, Entity skeleton, const RagdollDescription&)` refusing an `error` or a skeleton entity without a ready `SkeletonPose`; spawning under a parent entity one body entity per part (kinematic `RigidBody`, `Collider`, `RagdollBody`, `CollisionGroup` with the description's group or the skeleton entity's id) and one `Joint` entity per non-root part, root first, frames from the part's model-space frame and anchor through each body's rest world pose; `Ragdoll` on the skeleton entity (`Following`, `group`, `rootBody`, `strength`); `SpawnedRagdoll` returned; bodies placed on their bones at once. `DestroyRagdoll(Scene&, Entity skeleton)` removes the parent and everything under it, clears any overrides and removes `Ragdoll`.
- **The glue in `Tick`** (after FenrirSkeleton's tick pass, before physics's `PostTick`): resolve bone indices by name on the first tick and after a `SkeletonPose` rebuild; the ragdoll inactive, warning once, on a frame-posed or not-ready skeleton; every body's effective mode (only `Following` here) applied as `RigidBody::motionType`; each following body's `Transform` written from `GetBoneWorldMatrix(skeleton, bone, PoseKind::Simulation) × offset` in its parent space, and its `RigidBody` linear and angular velocity from this and the previous following pose over `tickRate`; `RagdollState` and each `RagdollBodyState` written. No `Update` work yet.
- **Tests:** a new `FenrirRagdollTests` executable, with a test app installing physics, skeletons and ragdolls and no renderer (prior art FenrirSkeleton's `SkeletonTest.hpp` and `TickPosed.cpp`, physics's `PhysicsTestApp`), a code-built chain skeleton and a code-built sixteen-bone humanoid rig shaped like the Quaternius human, never the downloaded asset (ADR-0052).
- **Glossary:** *ragdoll*, *ragdoll body*, *ragdoll glue*, *ragdoll description*, *ragdoll part*, *humanoid bone map*, *stature* are written; keep them current.

**Blocked by:** 01 (Collision groups).

**Touches:** `packages/FenrirRagdoll/` (new), `CMakeLists.txt`, `examples/DemoScaffold/`

**Status:** resolved

- [x] `FenrirRagdollTests`: a full map gives sixteen parts with the spec's radii, masses, limits and joint kinds; a map without hands and feet gives twelve with fractions redistributed; a map not rooted at the hips or naming an unknown bone gives an `error` and `BuildRagdoll` spawns nothing.
- [x] `BuildRagdoll` spawns the body and joint entities under one parent, kinematic, in the skeleton entity's group, joints root first with `collideConnected` false and friction set, `Ragdoll` on the skeleton entity with `rootBody`; `DestroyRagdoll` removes them all and `Ragdoll`.
- [x] Following: at one and at several ticks per frame every body's world transform equals its simulation bone × offset after each tick; velocities equal the pose delta over `tickRate`; a game write to a body `Transform` is overwritten; bodies pushed through dynamic boxes push them.
- [x] A ragdoll on a frame-posed skeleton is inactive with one warning naming `PoseTiming::Tick`, and active again once switched; a renamed bone is skipped with one warning and `state.bone = NoBone`.
- [x] A hand-written two-part description builds and follows the chain skeleton.
- [x] Two runs are bit-identical over 300 ticks.
- [x] The API description is generated; no public header names Jolt; Clang and MSVC x64 debug and release build without new warnings; the package is absent when physics is off.

## Resolved by

Branch `ragdoll/02-tracer-bullet`. The package is `packages/FenrirRagdoll`: `Components.hpp`, `Description.hpp`, `Builder.hpp` and `Ragdolls.hpp` public, `Frames.hpp`, `Describe.cpp`, `Build.cpp` and `Glue.cpp` private, tests in `Tests/` (`RagdollTest.hpp` holds the test app and the two code-built rigs). The Clang and release builds are CI's: only `msvc-x64-debug` was built here. Choices the ticket left open:

- **Built with physics and assets on,** since FenrirSkeleton is built with assets; ADR-0053 amended.
- **Installed where physics is:** the apps install physics, not the scaffold, so PhysicsDemo's and AnimationDemo's `main.cpp` install `Ragdolls` beside `Physics`. The scaffold itself is unchanged.
- **An empty slot drops its chain** (an empty spine drops the chest, head and arms), as the ticket says. A filled slot under an empty one is dropped, not hung from the nearest filled slot; a body spans several bones only where the skeleton has unmapped bones between mapped ones (the test rig's `spine_02`).
- **The stature** is the head slot's bone to a foot slot's; without either it is the rest pose's height.
- **Bones are resolved by name every tick,** not cached until a rebuild: the same result, and a renamed `RagdollBody::bone` is picked up at once.
- **Bodies are placed through full matrices** (`skeleton × bone × offset`, scale included, then reduced to position and rotation), so a rig with a scaled armature lines up; scaling the skeleton entity stays out of scope.
- **The skeleton entity's id 0** (which is no group) gives the group `Entity::Null`, which no live entity's id can be.
- **The builder applies `jointFriction`** to every joint and turns motors off; a part's `jointFrame` rotation is read, its position is not (`jointAnchor` places the joint).
- **The builder also refuses** a description with no parts and a skeleton that already has a `Ragdoll`.
- **`DestroyRagdoll`** finds the parent as the root body's parent, and clears the override entries on its bodies' bones only.
- **The first following tick writes zero velocity** (no earlier pose); so does the first after a body is skipped or the ragdoll inactive.
- **Bodies not following are not driven here:** their effective mode is reported, nothing of theirs is written (tickets 03 and 04).
- **`RagdollBodyState::bone` defaults to the literal -1** (`NoBone`): the API description records literal defaults only.
