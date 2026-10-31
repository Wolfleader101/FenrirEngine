# How do other engines build and drive ragdolls?

Type: research
Status: open
Blocked by: none

## Question

How do other engines put ragdolls in front of game code, from their docs and source? Survey Unity (the Ragdoll Wizard, `CharacterJoint`, `Rigidbody` per bone, Animator and ragdoll blending, active ragdolls through `ConfigurableJoint` drives, and the Unity Physics / DOTS ragdoll sample), Unreal (the Physics Asset and its editor, `SetAllBodiesBelowSimulatePhysics`, `SetAllBodiesBelowPhysicsBlendWeight`, `PhysicalAnimationComponent` with its profiles and strengths, `RigidBodyAnimNode` in the animation graph), Godot (`Skeleton3D`'s "Create physical skeleton", `PhysicalBone3D`, `PhysicalBoneSimulator3D`, `physical_bones_start_simulation` with a bone subset), Jolt's own samples (as the backend's reference; the facts are in "What does Jolt offer for ragdolls?", so only the API shape here), and Bevy (bevy_rapier or Avian ragdoll examples or crates, if any). For each:

- **The description:** what a ragdoll is built from: a hand-authored asset (Unreal's Physics Asset), a wizard that derives bodies from named bones and lengths (Unity, Godot), code; how bodies are named and matched to bones (bone name, bone index, a node per bone); whether the description is per skeleton asset or per instance; what tooling generates it and how good the result is without hand tuning.
- **Bodies:** which bones get bodies in the usual humanoid (hips, spine, chest, head, upper and lower arms, hands, upper and lower legs, feet: 11 to 15), which shapes (capsules, boxes, spheres), how the body is placed relative to the bone (offset, along the bone to its child), and how mass is spread.
- **Joints:** which joint kinds join parent and child (Unity `CharacterJoint`: a swing-twist with asymmetric limits; Unreal: a six-DOF with cone and twist; Godot: cone or hinge per `PhysicalBone3D` joint type), what limits the wizards set, and whether elbows and knees are hinges or cones.
- **Collision inside and around the ragdoll:** how parts of one ragdoll avoid colliding with each other (parent–child only, overlap-based pairs at rest pose, Unreal's per-body collision toggles in the asset), and how a ragdoll avoids its own character controller or capsule (Unity's layer matrix and `Physics.IgnoreCollision`, Unreal's collision profiles and `IgnoreActorWhenMoving`, Godot's `add_collision_exception_with`): layers, groups or per-pair ignore lists, and what game code does when one character's ragdoll must still hit another's.
- **Switching:** how a character goes from animated to ragdoll (kinematic bodies following bones then set dynamic, bodies only created at death, `SetSimulatePhysics` per body), whether it is per body, per subtree or all at once, and how the initial velocity is carried over.
- **Blending:** how the simulated pose reaches the skeleton (a blend weight per body or per bone, Unreal's `PhysicsBlendWeight`, Godot's `PhysicalBoneSimulator3D` influence), and how a character gets up (a blend back over time, a get-up animation matched to the final pose, snapping the character root to the ragdoll's hips).
- **Powered ragdolls:** how motors are driven towards an animated pose (Unreal's physical animation profiles with strength and position and velocity gains; Unity drives on `ConfigurableJoint`; Godot's none), what a strength setting is in physical terms (spring, damping, max force), and whether the drive is in local joint space or world space.
- **Hit boxes:** whether hit detection uses the same bodies as the ragdoll (kinematic while animated) or separate shapes, and how hits are reported with a bone name.
- **The character's root:** what happens to the character's entity, capsule and camera while the ragdoll is live (follow the hips, stay put, teleport on get-up).
- **ECS fit** (Unity DOTS, Bevy): which components, how a ragdoll body references its bone and its ragdoll, and how the update fits a system.
- **Scripting access**, and **common complaints** (jitter, limbs stretching, self-collision jams, ragdolls flying away on switch, get-up pops, tuning joints by hand).

Put the findings on a `research/engine-ragdoll-models` branch, in `.scratch/fenrir-physics/research/engine-ragdoll-models.md`, and link them here.
