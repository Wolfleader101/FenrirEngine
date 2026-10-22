# How do engines model skeletons, clips, blending, skinning and ragdoll blending?

Type: research
Status: resolved
Blocked by: none

## Question

How do engines and libraries model skeletal animation, in the areas FenrirEngine's animation spec and the physics map's ragdolls hang on? Read Bevy's animation, Unity (Animator, and DOTS or Entities animation), Unreal (skeletal meshes, AnimGraph, physical animation), Godot 4 (`Skeleton3D`, `AnimationTree`, `PhysicalBone3D`), O3DE EMotionFX and ozz-animation:

- **Skeleton:** bones as entities or as an array in one component; bind pose and inverse bind matrices; how a skinned mesh names its skeleton.
- **Clips and playback:** sampling, compression, looping, time and speed, events in clips, root motion.
- **Blending:** crossfades, additive layers, bone masks, blend trees against state machines; where the result pose lives (local or model space).
- **Skinning:** CPU or GPU, bone limits per vertex, the bone-matrix buffer, dual quaternions.
- **Ragdolls:** how a simulated pose is blended with an animated one (going limp, partial ragdolls, powered ragdolls driving motors towards the animation), and how bones are mapped to bodies and joints (Unreal's physics asset, Godot's physical bones).
- **Libraries:** whether ozz-animation (or another) would be worth using behind a seam, and what it would leak.

Write findings to a Markdown file on the `research/animation-models` branch (`.scratch/fenrir-renderer/research/animation-models.md`), citing each claim's source and pinning the versions read, and link it here.

## Answer

Findings: `.scratch/fenrir-renderer/research/animation-models.md` on branch `research/animation-models` (commit `6f17074`), against Bevy main `2614884` (0.20-dev), UnityCsReference `6c1fc79` with the Unity 6000.2 and Entities Graphics 1.4 manuals, Unreal 5.8's API reference and manual (source not read), Godot `6790667` (4.8 beta), O3DE `2462b90` (EMotionFX), ozz-animation `744eb9d` (0.17.0), Jolt v5.6.0 and the ACL README, each claim cited. Gaps: Unreal and Unity claims rest on their docs; no shipped Unity ECS animation package found; O3DE's skinning shaders not read.

- **Skeleton:** Bevy and Unity make each bone an entity or `Transform`, the skinned mesh listing its joints, inverse bind matrices in an asset (Bevy) or on the mesh (Unity); Godot, Unreal, EMotionFX, ozz and Jolt keep bones as arrays in one object. Meshes name bones by index (Unity, Unreal, ozz with a remap) or by name (Godot's `Skin`, Bevy's bone paths, EMotionFX); names are what let clips and meshes be shared across skeletons.
- **Clips:** ozz quantises keys and runs an offline hierarchy-aware reduction; Unreal uses ACL; Godot has an opt-in lossy `compress()`; Bevy stores raw curves. All but ozz have looping, speed and timed events; ozz leaves playback state to the app, with threshold-crossing float tracks for events.
- **Root motion:** Unity, Unreal, Godot, EMotionFX; ozz as offline extraction plus a runtime delta job; Bevy none.
- **Blending:** in local space everywhere (Unreal converts to component space for IK and physics; EMotionFX's pose holds both). Bevy: a small DAG (clip, blend, add) with 64 bitmask groups and a crossfade component; Unity, Unreal, Godot, EMotionFX: state machines, blend spaces, masks by name; ozz: one blending job with weighted layers, per-joint weights and additive layers, falling back to the rest pose at low total weight.
- **Skinning:** Bevy in the vertex shader from world-space joint matrices (4 influences, 256 joints); Godot once per frame in compute into a vertex buffer (4 or 8); Unreal both (4, 8, up to 12); Unity Entities Graphics in compute, not on desktop OpenGLCore; ozz CPU only; only EMotionFX defaults to dual quaternions.
- **Ragdoll mapping:** everywhere a list of bodies by bone, each a shape, offset and constraint to its parent body (Unreal's physics asset, Godot's `PhysicalBone3D`, Jolt's `Part` per joint); unmapped bones follow their nearest mapped ancestor; Jolt's `SkeletonMapper` maps a reduced ragdoll skeleton onto the full one.
- **Ragdoll blending:** Unreal per body (`PhysicsBlendWeight`, settable for a subtree); Godot simulates chosen subtrees blended by one `influence`; EMotionFX makes the ragdoll a graph node blended like a pose, ramping joint strength on switches.
- **Powered ragdolls:** Unreal's Physical Animation component (experimental) drives bodies towards the pose with motors, strength profiles per subtree; EMotionFX carries per-joint strength and damping in the pose; Jolt has `DriveToPoseUsingMotors` and `DriveToPoseUsingKinematics`; Godot, Unity and Bevy nothing.
- **ozz behind a seam:** MIT, clean runtime (sampling, blending, local-to-model, IK, tracks) plus offline tools and glTF import (FBX needs Autodesk's SDK); no playback state, graphs, GPU skinning or ragdolls. It would leak SoA math, depth-first joint order capped at 1024, its own binary format and ratio-based time; hidden per ADR-0010 by converting to glm or copying out model-space matrices.
- **Alternatives:** ACL is compression only (header-only). Jolt's skeleton types are at hand but would make animation depend on the physics backend, against "packages stay independent".
