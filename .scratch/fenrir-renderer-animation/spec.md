Status: ready-for-agent

# FenrirRenderer animation: skeletons, clips, blending, GPU skinning, bone attachments and ragdoll handover, with C# and a demo app

Origin: the FenrirRenderer wayfinder map (`.scratch/fenrir-renderer/map.md`). This spec collapses the map's animation tickets (24, 26–34 and 62) into one buildable plan on top of the foundation spec (`.scratch/fenrir-renderer-foundation/spec.md`, built and removed: read it with `git show 28d8f69:.scratch/fenrir-renderer-foundation/spec.md`), which it cites rather than restates. The hard-to-reverse calls are recorded in ADRs:

| ADR | Decision |
|---|---|
| 0028 | Skeletons are bone arrays in FenrirSkeleton, animated by FenrirAnimation and skinned by the renderer |
| 0031 | Skeleton poses live in FenrirSkeleton, and models import through cgltf and ufbx into one bind space |
| 0033 | Pose stages run as FenrirSkeleton's pose pipeline, and modifiers edit a live final pose in order |
| 0034 | Clips play through a game-owned `Animator`, and FenrirAnimation owns playback time and events |
| 0035 | Skinned meshes name their skeleton through `SkinnedMesh` and read a per-frame palette buffer |
| 0036 | Clips blend in FenrirAnimation through layer entities, crossfades and bone masks |
| 0037 | Bone attachments are plain components, placed in dependency order under any parent |
| 0038 | Ragdoll poses enter as world pose overrides, scaled by gameplay-owned override weights |
| 0039 | C# reaches skeletons and animation through mirrors, per-bone functions and extension stages |
| 0040 | Tick-posed skeletons evaluate their base pose each fixed tick, for bodies that follow bones |
| 0041 | FenrirRenderer reaches skeleton poses through a skinning source that FenrirSkeleton registers (new, from writing this spec) |

There are no ADRs numbered 0030 or 0032. Everything here builds on the foundation: FenrirAssets and its cgltf and ufbx importer, the renderer's draw list, frame and material variants, FenrirDebugDraw, the FenrirScripting prerequisite and extensions, and the demo scaffold.

## Problem Statement

Once the foundation is built, the engine draws rigid meshes only. Its importer leaves every skinned mesh out with a diagnostic and reads no skeleton or clip. Nothing in the engine can:
- hold a skeleton or pose it, by animation, by code or by a ragdoll;
- play, crossfade or layer a clip, or tell gameplay when a clip reaches a footstep;
- draw a skinned mesh;
- hang a sword on a hand, or move a hit box with a bone in step with physics;
- do any of this from C#, or on a server with no renderer.

The engine developer wants animation that follows the engine's rules:
- **Its own packages:** skeleton state works without animation, and animation works without a renderer.
- **No dependencies between rendering, animation and physics:** they exchange plain data, and glue joins them.
- **Plain-data components**, game-owned, with system state kept privately, as physics and the renderer do.
- **Bones as arrays, not entities,** so crowds, layers and IK stay cheap.
- **Wrong configurations visible and skipped,** never drawn as plausible but wrong geometry.
- **C# with the same powers as C++**, joining the pose stages rather than running a frame late.
- **An animation demo app** on the shared scaffold, smoke-run like the others.

## Solution

Two new packages, an interface in FenrirAssets, and additions to four existing packages:

- **FenrirSkeleton:** each skeleton instance's pose, kept as arrays in a scene resource and named by a plain `SkeletonPose` component on its skeleton entity. It owns:
  - bone lookup and pose reads in local, model and world space;
  - the pose pipeline (`Animate`, `Modify`, `Late` among its steps) and pose modifiers;
  - the first modifier, the pose override, with override weights;
  - finalisation, the lazy skinning palette and the pose generation;
  - bone attachments;
  - tick-posed skeletons, with their simulation and render poses.
- **FenrirAnimation:** clips played through a game-owned `Animator` and `AnimationLayer` entities, with crossfades, additive layers, bone masks and bounded clip events, written into the base pose in `Animate`.
- **FenrirAssets:**
  - `SkeletonData`, `ClipData` and `BoneMaskData` assets;
  - the skin section of `MeshData`;
  - an importer that reads skins, skeletons and clips from glTF and FBX into one bind space;
  - the `SkinningSource` interface FenrirSkeleton implements (ADR-0041).
- **FenrirRenderer:** `SkinnedMesh` and GPU skinning from a per-frame palette buffer under the `SKINNED` variant, reached through the skinning source with no link to FenrirSkeleton.
- **FenrirScripting:** extension stages, so C# systems run inside pose stages.
- **Scripting extensions:** `Fenrir.Skeleton` and `Fenrir.Animation`, and `SkinnedMesh` in `Fenrir.Renderer`.

Game code works the ECS way:
- **Spawning:** `SpawnModel` spawns a model with its skeletons, skinned meshes and attachment points.
- **Playing:** write the `Animator` on the skeleton entity (clip, speed, loop mode, playing, fade time); add `AnimationLayer` entities for upper-body or additive clips; read clip events with `EventReader`.
- **Posing:** write the base pose by hand, or add a modifier in `Modify`, or set world-space overrides from ragdoll glue and ramp their weights.
- **Attaching:** put a `BoneAttachment` on the sword.
- **Following bones in the physics step:** set `SkeletonPose::timing = PoseTiming::Tick`, and let glue move the kinematic body from the simulation pose.

A new **AnimationDemo** app shows each feature in its own scene, with C# twins of playback and attachments.

## User Stories

### Skeletons and spawning

1. As a game developer, I want to load a glTF, GLB or FBX character and spawn it with its skeletons, skinned meshes and attachment points with one call, so that an animated model appears like a static one.
2. As a game developer, I want a model with several meshes on one skeleton, or several skeletons, to spawn with one skeleton entity per skeleton, so that unrelated rigs never share a pose.
3. As a game developer, I want a skeleton to work with no animation and no renderer, posed by hand, so that a server can move hit boxes and a test can check bones.
4. As a game developer, I want to find a bone by name and read its local, model-space and world transform, so that gameplay can aim at a hand or a head.
5. As a game developer, I want both the base pose and the final pose readable, so that powered ragdolls can chase what animation wanted, not what physics did.
6. As a game developer, I want a skeleton whose asset goes stale to report itself unavailable, and to recover by itself, so that a reload never crashes or draws nonsense.
7. As a game developer, I want to build a skeleton in code, so that procedural rigs and tests need no file.
8. As a game developer, I want a failed skin or skeleton in a file to fail only itself, with its meshes left out rather than drawn wrongly, so that one broken part does not cost the whole model.

### Playing clips

9. As a game developer, I want to play a clip by writing an `Animator` (clip, speed, loop or once, playing), so that playback is plain data I can inspect and change.
10. As a game developer, I want to seek explicitly, and to read a clip's time, loops and whether it finished, so that I never have to guess whether a write was a seek.
11. As a game developer, I want negative speed to play in reverse, and pausing to hold the pose, so that I can scrub and freeze.
12. As a game developer, I want glTF step, linear and cubic keys sampled exactly, and FBX clips baked, so that clips look as their tools showed them.
13. As a game developer, I want a clip to play on any skeleton with matching bone names, with a diagnostic per missing bone, so that I can share clips between characters of one rig.
14. As a game developer, I want timed events in a clip delivered as ECS events once each, in order, even across loops or a long frame, so that footsteps and hits line up with the animation.
15. As a game developer, I want a hitch to send a bounded number of events and count the rest, so that one slow frame cannot flood the event queues.

### Blending

16. As a game developer, I want changing a clip to crossfade over a time I set, with a smooth or linear curve, so that transitions need no extra code.
17. As a game developer, I want an interrupted crossfade to continue smoothly into the newest clip, so that rapid changes never pop.
18. As a game developer, I want override layers with a weight and a bone mask, so that an upper-body clip plays over a walk.
19. As a game developer, I want additive layers measured from the clip's start or the rest pose, so that leaning and breathing add onto any pose.
20. As a game developer, I want a layer's weight to change its effect but never its clock, so that fading a layer out and back in stays in sync.
21. As a game developer, I want events only from clips that contribute, merged across layers in time order, so that a silent layer never fires a footstep.
22. As a game developer, I want to build my own state machine by writing these components, so that the engine imposes none.

### Pose modifiers, overrides and ragdolls

23. As a game developer, I want to add an ordered pose modifier that writes local, model or world-space targets with a weight and a channel mask, so that IK and procedural posing plug in after animation.
24. As a game developer, I want later modifiers to see earlier modifiers' results, so that IK can run after a ragdoll blend.
25. As a game developer, I want to set world-space overrides on bones and scale them per bone or per subtree, so that a ragdoll can take over a body, or one limb, and blend back.
26. As a game developer, I want bones with no body to keep their animated pose and follow their nearest mapped ancestor, so that fingers do not freeze when the arm goes limp.
27. As a ragdoll glue author, I want names, indices, parents, a subtree query, world-target writes, override weights and base-pose reads with documented timing, so that I can join physics bodies to a skeleton without either package knowing the other.

### Bone attachments

28. As a game developer, I want a `BoneAttachment` (skeleton entity, bone name, offset) to place its entity at a bone every frame, under any parent, so that weapons and props follow hands.
29. As a game developer, I want attachments on attachments and attachments in other hierarchies placed in the right order, so that a scope on a rifle in a hand follows correctly.
30. As a game developer, I want an attachment that cannot resolve to keep its last transform and report why, so that failures are diagnosable and recover by themselves.
31. As a game developer, I want to detach an object keeping its local or its world transform, so that a dropped weapon stays where it was.

### Following bones in the physics step

32. As a game developer, I want a skeleton animated at tick time, so that a kinematic shield or hit box follows a bone with no lag and the right velocity.
33. As a game developer, I want the drawn pose of such a skeleton blended between ticks at physics's fraction, so that the character and its bodies stay together on screen.
34. As a game developer, I want reads to say whether they want the simulation or the render pose, so that no read changes meaning with the phase it runs in.

### Skinned rendering

35. As a game developer, I want a skinned mesh drawn from its skeleton's pose on the GPU, so that characters animate on screen.
36. As a game developer, I want several meshes on one skeleton, and several cameras, to share one palette upload per pose, so that cost follows skeletons, not draws.
37. As a game developer, I want correct normals and tangents under non-uniform scale and mirroring, so that lit characters shade correctly.
38. As a game developer, I want conservative world bounds for a skinned mesh, so that gameplay and later culling can use them.
39. As a game developer, I want a skinned mesh with no skeleton, an unavailable skeleton, a skeleton it was not baked for, or a shader without skinning to be skipped or drawn magenta with one warning, so that a wrong setup is visible but never fatal.
40. As a game developer, I want rigid rendering to work with no skeleton package installed, so that a game without animation pays nothing.

### C#

41. As a script author, I want `SkeletonPose`, `BoneAttachment`, `Animator`, `AnimationLayer` and `SkinnedMesh` as writable mirrors, so that C# sets up animation as C++ does.
42. As a script author, I want to read bones per bone or as one copy per skeleton, and write the base pose, so that C# can pose and inspect skeletons.
43. As a script author, I want C# systems inside the `Animate`, `Modify` and `Late` stages, ordered among C++ work, so that C# modifiers and bone-following cameras need no frame of lag.
44. As a script author, I want `Seek`, playback state, clip events, clip-event authoring, bone masks, overrides and weights, so that C# can be ragdoll glue or a state machine.
45. As a script author, I want stale handles and missing packages to do nothing rather than throw, so that scripts are robust.

### Demo and engine

46. As the engine developer, I want AnimationDemo scenes for playback, the skeleton, skin diagnostics, blending, attachments, going limp and a crowd, with C# twins of playback and attachments, so that I can check each feature by eye.
47. As the engine developer, I want AnimationDemo smoke-run on null and llvmpipe like the other apps, with expected-misconfiguration diagnostics as warnings, so that CI catches crashes but not deliberate mistakes.
48. As an engine maintainer, I want FenrirSkeleton, FenrirAnimation, FenrirRenderer and FenrirPhysics to depend on none of each other beyond FenrirAnimation on FenrirSkeleton, so that each can change and be tested alone.

## Implementation Decisions

### Packages, build and install (ADR-0028, ADR-0041)

- **FenrirSkeleton** depends on FenrirAssets, FenrirECS, FenrirScene, FenrirApp, FenrirMath and FenrirTime. Its owner and C++ facade class is **`Skeletons`**, because `Skeleton` is the asset marker type (`AssetHandle<Skeleton>`; see "Settled while writing this spec"). Every `Skeleton::…` in the ADRs reads `Skeletons::…`.
- **FenrirAnimation** depends on FenrirSkeleton and FenrirAssets. Its owner and facade class is `Animation`. Its runtime is our own, on glm; no ozz.
- **FenrirRenderer** depends on FenrirAssets and links neither package. It reaches live poses only through the skinning source (below).
- None of them depends on FenrirPhysics, and FenrirPhysics depends on none of them.
- **Build:** both packages are built under `FENRIR_ENABLE_ASSETS`, as FenrirDraw2D is, with no option of their own. They link no library; cgltf and ufbx stay FenrirAssets' (foundation "Build"). Package tests and scripting extensions follow that option, and extensions also need `FENRIR_ENABLE_SCRIPTING`. `PublicHeaders.cpp` checks both packages as the foundation's do.
- **Install** follows the foundation's failure shape ("Engine prerequisites"):
  - `Skeletons::Install(App&)` registers the skinning source and the pose pipeline's systems, and returns the move-only `Skeletons` owner. It fails without FenrirAssets installed.
  - `Animation::Install(App&, AnimationConfig)` adds FenrirAnimation's sampler to `Animate` and returns the `Animation` owner. It fails without a valid `Skeletons` owner.
  - Each failure logs one diagnostic and returns an owner whose `IsValid()` is `false`.
  - `AnimationConfig::maxEventsPerInstancePerFrame` defaults to 64.
  - `Renderer::Install` does not require FenrirSkeleton.
- **The skinning source** (ADR-0041):
  - `SkinningSource` is an abstract interface in FenrirAssets' public headers, holding no state.
  - Per scene and skeleton entity it answers `PoseState` (`Ready`, `Unavailable`, `Missing`; the enum is declared here and used by FenrirSkeleton), the instance's `AssetHandle<Skeleton>`, its `PoseGeneration`, its palette and its model matrices.
  - It also offers `SpawnSkeletons(scene, model, spawned)`.
  - `Skeletons::Install` registers FenrirSkeleton's implementation as an app resource, and teardown removes it. A second registration is logged and ignored.
  - The renderer looks it up in each `Render` and each call that needs it, so install order does not matter.

### FenrirAssets: skeletons, clips, masks and skins (ADR-0031, ADR-0034, ADR-0035, ADR-0036)

- **Handles:** the foundation's reserved `AssetHandle<Skeleton>` and `AssetHandle<Clip>`, plus `AssetHandle<BoneMask>`. They resolve to `SkeletonData`, `ClipData` and `BoneMaskData`. Every `AssetHandle<SkeletonData>`, `AssetHandle<ClipData>` and `AssetHandle<BoneMask>` in ADR-0034 to ADR-0040 is spelled this way.
- **`SkeletonData`**, immutable once built. Per bone, in a parents-first order:
  - the source name;
  - a unique **engine name**: the source name, suffixed `#2`, `#3`… only on duplicates, and shortened at a UTF-8 boundary then suffixed when over 255 bytes, each with a diagnostic;
  - the model node index;
  - the parent index: lower than the bone's own, or −1 for a root; several roots are allowed;
  - the rest local TRS;
  - the inverse bind. Rest and bind are distinct.

  The asset also holds:
  - the name map, derived at build, never stored. Lookup by engine name is exact; by source name it returns the first match;
  - u16 bone indices in storage;
  - **`bindSignature`**, ADR-0035's versioned FNV-1a 64 encoding (count, parents, engine names, inverse-bind bits with `-0` → `+0`), where 0 is reserved for "unchecked";
  - **queries:** bone count, parent, find by engine or source name, `IsDescendant(bone, ancestor)` and iteration over a bone's subtree.

  `Add<SkeletonData>` builds procedural rigs and validates them as the importer does.
- **`MeshData`'s skin section** (ADR-0035):
  - Per vertex, `u16x4` joints and `unorm16x4` weights, stored exactly as the shader reads them.
  - `boneCount` (the highest joint index + 1) and `expectedSignature` (0 for unchecked procedural meshes).
  - One box per bone in bone space, plus a box of zero-weight vertices in bind space, rebuilt on import, `Add` and `Replace`.
  - **Quantisation:** weights are normalised and quantised to sum exactly to 65535, with the remainder given a unit at a time to the largest fractional parts. All-zero weights stay zero, with an import warning. A joint index ≥ `boneCount`, or a negative or non-finite weight, fails the skin at import and is refused by `Add` and `Replace`.
- **`ClipData`:**
  - **Channels:** each holds its target's source node identity and engine name, a path (translation, rotation or scale), an interpolation (`Step`, `Linear`, `CubicSpline`), authored key times and values, and tangents for cubic keys. The duration is the last key's time.
  - **Events:** `{ float time; std::string name; }`, sorted by time, with equal times in authored order. An event is identified by its index plus the clip's revision. `ClipEventId` is the 32-bit FNV-1a of the name, a lookup convenience only.
  - **Event edits on the store,** as the foundation's material edits are: add an event (time, name), remove one by index, each bumping the revision. Reads: duration, events, and every index with a given name.
- **`BoneMaskData`:** ordered `{ boneName, weight, scope }` entries, `scope` being `Bone` or `Subtree`. Later entries override earlier ones, and unlisted bones get 0. The store creates masks and edits them (`Include(mask, name, weight, scope)`, `Clear(mask)`), each edit bumping the revision. An empty mask handle on a layer means every bone at 1.
- **`Model`:** the foundation's node record gains its kind (ordinary node, synthetic owner, or bone *i* of skeleton *k*). The model lists its skeletons and clips as sub-assets labelled like its meshes.

### Import (ADR-0031, ADR-0034, ADR-0035)

- **This replaces the foundation's static-only rule.** Skinned meshes, skeletons and clips are imported; the "skinned mesh left out" diagnostic goes, and the foundation's skinned-fixture test now expects a skinned mesh.
- **Bones:** a node is a bone if it is one of these:
  - a skin joint;
  - on the path between a skeleton's joints and its root bone;
  - below a bone, animated by a clip in the file, and carrying no mesh, camera or light;
  - an FBX `LimbNode`.

  Every other node is an ordinary node, empty children of bones included.
- **Skeletons:**
  - Skins are grouped by source joint identity (skin index, then joint order, then parents-first), and joint references and vertex indices are remapped to that order.
  - Disjoint skins are separate skeletons.
  - Skins sharing joints merge if their binds agree, or through one checked conversion `C` baked into the later skin's vertices. Otherwise the later skin fails.
- **Owners:**
  - The owner is the `skin.skeleton` node when it is not a joint, else the nearest non-bone ancestor of the group's root bones. `skin.skeleton` is never used in skinning maths.
  - When several skeletons resolve to one node, each gets a **synthetic owner**: an identity child of that node, named `<first root bone>.Skeleton`.
- **One bind space:** every importer satisfies `v_world = W(owner) × M_model[i] × B[i] × v`. The convention is column vectors and column-major storage, as glm. The bind identity `M_model_bind × B = I` holds against the bind pose, not the rest pose.
  - **glTF:** kept unchanged when the owner's rest world is the identity, else `B = IBM × W_o` and `v = W_o⁻¹ v`. A skinned mesh node's own transform is ignored. A missing inverse-bind accessor means identity.
  - **FBX:** from ufbx's `bind_to_world` and `geometry_to_bone`. ufbx's axis and unit conversion sits on the root node.
  - **Baking:** positions by the matrix, normals by its inverse transpose (renormalised), tangents by its 3×3 with handedness flipped on a negative determinant. Joint indices are remapped in the same pass.
- **Clips:**
  - glTF keys are kept raw.
  - The importer flips any rotation key whose dot product with the previous key is negative, flipping its tangents with it.
  - FBX clips are baked through ufbx's baking API to linear keys at a documented internal rate.
  - Channels on ordinary nodes are kept but not played (node animation is out of scope).
- **Failure:**
  - The load result is `Succeeded`, `Partial` (with diagnostics) or `Failed`. Each error fails the smallest unit: a skeleton with its skins and meshes, or an incompatible skin with its meshes. An ordinary node with a degenerate transform is warned and kept.
  - Validation covers non-finite TRS, zero or degenerate scales and binds, out-of-order or cyclic parents, and a source node used twice as a joint, besides any non-invertible owner, bind or conversion matrix.
  - No half-built skinning: a failed skin's meshes are omitted, never imported unskinned.
  - The importer sets `expectedSignature` from the skeleton each mesh was baked against.

### FenrirSkeleton: instances and reads (ADR-0031, ADR-0035, ADR-0038, ADR-0040)

- **`SkeletonPose { AssetHandle<Skeleton> skeleton; PoseTiming timing = PoseTiming::Frame; }`** is plain and blittable, on the skeleton entity, one per entity. Switching timing is a field edit.
- **`SkeletonPoses`**, a private FenrirSkeleton scene resource, owns each instance's arrays: base locals, final locals, model matrices and the cached palette, plus for tick-posed instances the last two tick base poses.
- **Reconcile:** each pass creates instances for new components, rebuilds an instance at the rest pose on a handle or asset-revision change, and sweeps dead ones. `Ensure(entity)` does it at once for one entity. There are no hooks.
- **`PoseState(entity)`:**
  - `Ready`;
  - `Unavailable`, for a stale or empty handle: empty reads, `GetBoneWorldMatrix` empty, and the problem logged once;
  - `Missing`.

  On recovery the instance rebuilds, and bone indices and attachments re-resolve.
- **Reads:**
  - Spans: `std::span<const …>` of base and final locals and model matrices, valid until the end of the current system or the next structural change to `SkeletonPoses`.
  - Per bone: `FindBone(entity, name)`, `GetLocal`, `GetModel`, `GetBaseLocal(entity, bone, PoseKind = Render)`, `GetBaseModel(…, PoseKind)` and `GetBoneWorldMatrix(entity, bone, PoseKind = Render) -> std::optional<Mat4>`.
  - `GetBaseModel` is computed lazily from the base locals and cached until they change.
  - `GetBoneWorldMatrix` is `GetWorldMatrix(skeleton entity) × M_model[bone]`, the one defined way to combine the two spaces.
  - **`PoseKind`** is `Render` (the default) or `Simulation` (ADR-0040). On a frame-posed skeleton both return the render pose.
- **Base-pose writes:** `EditBasePose(entity)` marks the instance pose-dirty, even if no value changes. The final pose is written only by Prepare, modifiers and finalisation.
- **Three kinds of change stay apart:** a pose change dirties model matrices and the palette; a move of the skeleton entity affects only world reads and attachments; an asset or revision change rebuilds the instance.
- **`PoseGeneration(entity)`:**
  - a `u64` advanced when the finalised pose changes or the instance is rebuilt;
  - drawn from one counter for the whole `SkeletonPoses`, so a recycled entity never repeats a generation;
  - not advanced by moving the skeleton entity.
- **`GetPalette(entity)`:** main thread only, `M_model × B` built lazily for the current generation and cached with it. A skeleton nothing skins never builds one.

### The pose pipeline and modifiers (ADR-0033, ADR-0039, ADR-0040)

- **One sequential system** in `PostUpdate`, on the active scene only, runs the frame pipeline:
  1. Reconcile;
  2. **`Animate`** (base-pose writers);
  3. Prepare (final ← base);
  4. **`Modify`** (modifiers);
  5. Finalise (the remaining model matrices, parents-first);
  6. Attach;
  7. **`Late`** (read-only for poses).

  `Animate`, `Modify` and `Late` are the public pose stages; the rest are internal. Inactive scenes pause, and resume without catch-up.
- **Stage registration:**
  - `Skeletons::AddModifier(name, order, fn)`, and its `Animate` and `Late` counterparts, add work at an `int` order. Ties go by registration order.
  - Each returns a handle that removes the work, outside the pipeline.
  - Packages use order constants rather than relying on ties. `ModifierOrder::Override` is 1000.
  - Nothing is promised against other `PostUpdate` systems.
- **What each phase may write:**
  - Before the pipeline (`Update` and earlier): playback commands, overrides, weights and `EditBasePose`.
  - `Animate`: base-pose writers only.
  - `Modify`: modifiers only, through `PoseEdit`.
  - `Late`: may write ordinary ECS state, never a pose.
  - `EditBasePose` after the pipeline is seen by the next frame's evaluation. A hand-posed skeleton keeps its retained base pose.
- **`Animate` passes** (settled while writing this spec, refining ADR-0040):
  - Every `Animate` item runs in the frame pass and in each tick pass. A tick pass runs only when the active scene has a tick-posed instance.
  - The item's context gives the pass's `PoseTiming` and its step: `Time::tickRate` in a tick pass, `Time::deltaTime` in the frame pass. It lists the instances of that timing, which are the only ones the item may write.
  - `EditBasePose` on an instance of the other timing, during `Animate`, writes nothing and logs once per call site. Outside the pipeline it works on either timing.
- **Modifiers:**
  - A modifier runs every frame, chooses its instances, and opens `PoseEdit edit = ctx.Edit(entity)`, with `Base()`, `Local(bone)`, `Model(bone)` and `Write(bone, Target)`.
  - An edit is valid only during the call that opened it. It is empty for an instance that is not `Ready`. Opening one marks the instance modified.
  - `Target { PoseSpace space; Transform trs; Channels channels = All; float weight; }`, with the weight clamped to [0, 1].
  - **Each write resolves at once.** A `World` target goes through the inverse skeleton world matrix, read when `Modify` starts, and a `Model` target through the parent's current model matrix. The result is blended in local space (translation and scale lerp, rotation shortest-arc nlerp) for the masked channels.
  - Targets are satisfied when applied, not held. Writes compose sequentially. Model matrices are invalidated and recomputed lazily on read.
  - A missing inverse skips the target with a rate-limited warning naming the entity and bone. Shear is dropped when decomposing.
- **Cost:** clean, unedited instances skip Prepare and Finalise. Moving the skeleton entity changes no model matrix. Parallelism across instances is allowed, not promised.

### Pose override, override weights and the ragdoll handover (ADR-0033, ADR-0038)

- **`PoseOverride`** is an ordinary modifier at `ModifierOrder::Override`. It is the first modifier, not the contract.
  - Entries are persistent `{ bone, Target }` pairs per instance, set through `SetOverride(entity, bone, Target)`, `ClearOverride(entity, bone)` and `ClearOverrides(entity)`.
  - It writes them in bone order, parents first, skipping bones with no entry, so each world target goes through its parent's current model matrix.
  - The last `SetOverride` on a bone wins, whoever made it (documented).
- **Override weights:**
  - one per bone, default 1, gameplay's;
  - the effective weight is `clamp(Target.weight × overrideWeight[bone], 0, 1)`, and 0 skips the entry;
  - `SetOverrideWeight(entity, bone, w, Scope::Bone | Scope::BoneSubtree)`, `GetOverrideWeight` and `ClearOverrideWeight` (back to 1);
  - a subtree assignment writes each bone's value, latest wins, with no multiplying;
  - weights survive the glue's updates and clears;
  - entries and weights are both dropped when the instance is destroyed or rebuilt (documented).
- **No fades in FenrirSkeleton:** game code ramps the weights.
- **Bones with no entry** keep their animated locals.
- **What the physics map's ragdoll spec receives** (already noted on that map): bone names, indices, parents and the subtree query; world-target writes in `Update`; override weights; base-pose reads (`GetBaseLocal`, `GetBaseModel`), valid from `Animate` to the next `Animate`, a frame late on frame-posed skeletons; and tick-time `Simulation` reads on tick-posed ones. The mapping, offsets, bodies, joints, motors, collision groups, where the glue lives and the skeleton entity's follow policy stay on the physics map.

### Bone attachments (ADR-0037)

- **`BoneAttachment { Entity skeleton; BoneName bone; Transform offset; }`** is plain and blittable. The entity is placed at `GetBoneWorldMatrix(skeleton, bone) × offset`, inheriting the full bone transform.
- **`BoneName`** is a fixed 256-byte UTF-8 buffer holding at most 255 bytes. `Set` rejects a longer name with a log and never truncates. An unterminated buffer is diagnosed and never resolved.
- **Resolution** is cached privately per attached entity: the bone index, plus the skeleton entity, name and instance rebuild it was resolved against. Any change re-resolves by engine name before the next placement.
- **`Skeletons::AttachmentState(entity)`** returns `Attached`, `SkeletonMissing`, `SkeletonUnavailable`, `BoneNotFound`, `TransformMissing` or `Unplaceable`. It is never a component field.
- **Placement under any ECS parent:**
  - the target world matrix is taken through the inverse parent world matrix and decomposed to TRS, shear dropped (documented);
  - with the skeleton entity as parent, the fast path is `M_model[bone] × offset`;
  - attachments are placed in a cached topological order of their dependencies (through skeleton entities, parents and their ancestry), ties by entity index;
  - cycles are `Unplaceable`, logged once.
- **Failure:** keep the last `Transform`, report the state, log once per cause, and recover automatically. A missing bone retries only when the name, skeleton or rebuild changes. Attach makes no structural change.
- **Ownership:** while attached, FenrirSkeleton owns the entity's local `Transform`, and gameplay moves the object through `offset`.
- **`Skeletons::Detach(entity, DetachMode, newParent = {})`:**
  - `KeepLocal` removes the component only.
  - `KeepWorld` reparents under `newParent` (the scene root when null), keeping the current evaluated world transform. It is transactional: it validates first, changes nothing on failure, and returns a failure result.
  - Detach is structural, never called inside the pipeline.
- **No socket concept:** a named point is an ordinary node already attached by `SpawnSkeletons`.
- **Physics:** a dynamic body with a `BoneAttachment` is unsupported. A kinematic one on a frame-posed skeleton lags a frame (documented); tick-posed skeletons with glue remove the lag (below).

### Tick-posed skeletons (ADR-0040)

- **Tick pass:** in `PreTick`, on the active scene, FenrirSkeleton runs Reconcile, `Animate` (the tick pass above) and a base finalise over its tick-posed instances. The result is each instance's **simulation pose**: the base pose at tick time, with no modifiers. The previous tick's base locals are kept beside it.
- **Clocks and events:**
  - Clocks advance by `Time::tickRate`, including ticks that `maxTicksPerFrame` drops.
  - Events fire in the tick whose step crosses them.
  - Writes before a `PreTick` are seen by that tick, writes in `Tick` by the next tick, and writes in `Update` by the next frame's first tick.
  - A frame with no tick evaluates nothing and advances no clock.
- **Frame pass:** a tick-posed instance skips `Animate`. Its base pose is the blend of the last two tick base locals at `Time::accumulator / Time::tickRate` (lerp for translation and scale, slerp for rotation), read from FenrirTime. Prepare, `Modify`, Finalise, Attach and `Late` then follow, giving its **render pose**.
- **Modifiers are render-only.** The palette and attachments use the render pose.
- **Seeding:** at spawn, and on switching from `Frame` to `Tick`, both tick poses are seeded with the current base pose. Switching back resumes frame `Animate` from where the tick clock stopped.
- **Bone-following bodies, through glue:** in `Tick`, glue computes `GetBoneWorldMatrix(…, PoseKind::Simulation) × offset` into the body entity's parent space and writes its `Transform`, and physics's `PostTick` moves the kinematic body there. That entity has no `BoneAttachment`. The demo's glue is demo code.
- Ragdoll overrides (ADR-0038) are unchanged on either timing. Powered ragdolls on tick-posed skeletons read motor targets in `Tick` with `PoseKind::Simulation`.

### Spawning (ADR-0031, ADR-0035, ADR-0041)

- **`SpawnNodes`** (foundation "FenrirAssets") now leaves bones out: their entries in `SpawnedNodes::nodes` are null. Synthetic owners are spawned like ordinary nodes.
- **`Skeletons::SpawnSkeletons(scene, model, spawned)`:**
  1. adds `SkeletonPose` only to owners whose skeleton loaded;
  2. calls `Ensure` on each;
  3. then gives every ordinary node below a bone a `BoneAttachment` to its nearest ancestor bone, with its rest transform relative to that bone as the offset.
- **`Skeletons::SpawnModel(scene, model, parent)`** wraps `SpawnNodes` and `SpawnSkeletons` for headless apps.
- **The renderer's `SpawnModel`** runs `SpawnNodes`, then `SpawnSkeletons` through the skinning source, then adds `MeshRenderer`s. Each skinned mesh entity becomes a child of its skeleton's owner with an identity `Transform`, and gets a `SkinnedMesh` naming that owner. With no skinning source, it skips `SpawnSkeletons` and logs once (ADR-0041).

### FenrirAnimation: playback (ADR-0034, ADR-0040)

- **`Animator`** on the skeleton entity, game-owned, written only by game code, holding no time:

  ```cpp
  struct Animator
  {
      AssetHandle<Clip> clip;
      float speed = 1.0f;
      ClipLoop loop = ClipLoop::Loop;
      bool playing = true;
      float fadeSeconds = 0.0f;
      FadeCurve fadeCurve = FadeCurve::Smooth;
  };
  ```
- **`Animators`**, a private scene resource reconciled like `SkeletonPoses`, holds each instance's and layer's state: the `double` cursor, `completedLoops` (`uint64_t`, saturating), `finished`, pending seeks, key and event cursors, fades and snapshots, and the binding, mask and reference caches.
- **Base pose ownership:** while the `Animator` names a clip, FenrirAnimation owns the whole base pose, from rest locals each pass, with channels applied independently. With no `Animator`, or an empty clip, the base pose is left alone. Procedural changes are modifiers.
- **Playback:**
  - The clock advances by the pass's step × `speed` (frame delta, or `tickRate` for tick-posed skeletons). Negative speed reverses, and 0 holds.
  - `Loop` wraps over any number of laps. `Once` clamps at its boundary and holds.
  - `finished` sends one `ClipFinished` per false-to-true transition. It resets on a clip change, a seek, or a reversal away from the boundary, and pausing keeps it.
  - `playing = false` freezes the cursor but keeps sampling.
  - A clip change restarts at 0, or at the duration in reverse.
  - Zero-duration clips hold, never wrap, and finish once.
- **`Animation::Seek(entity, seconds)`** is queued and applied after any clip change, latest wins. It works while paused and before the instance exists.
- **`Animation::Playback(entity) -> PlaybackInfo { state, time, completedLoops, finished, fading, fadeProgress }`.** `state` is `Ready`, `Unavailable` or `Missing`.
- **Sampling:**
  - Raw keys: `Step` holds; `Linear` lerps, with shortest-path slerp for rotation; `CubicSpline` uses glTF's Hermite form, with interval-scaled tangents, rotations normalised after and tangents never normalised.
  - Before the first key its value holds; after the last, the last key's.
  - Per-channel key cursors fall back to a search after a seek, a reversal or a wrap.
- **Bindings:**
  - cached per (clip handle and revision, skeleton handle and revision), shared, numeric only;
  - matched by identity within the clip's own file, else by exact engine name;
  - rebuilt on a revision change and dropped when unused;
  - one diagnostic per channel naming the clip, target, path and reason (no match, ambiguous, ordinary node).
- **Unavailable:**
  - A skeleton that is not `Ready` is neither sampled nor advanced.
  - A stale non-empty clip freezes the cursor and pose and reports `Unavailable`, logged once.
  - On recovery the cursor is clamped (`Once`) or wrapped (`Loop`) into a changed duration, with no catch-up events.
  - The renderer is unaffected by clip availability.
- **Events:**
  - `AnimationEvent { Entity skeleton; Entity layer; AssetHandle<Clip> clip; uint32_t clipRevision; uint32_t index; ClipEventId nameId; float time; uint64_t loop; }` is sent through ADR-0012's queues from `Animate`.
  - Crossings: forward `(prev, cur]` and reverse `[cur, prev)`, in playback order; the starting boundary is included when playback starts; a wrap crosses every lap.
  - Seeks, clip changes, pauses, zero speed and recovery cross nothing.
  - At most `maxEventsPerInstancePerFrame` are sent per instance per frame, the earliest. The rest are counted arithmetically, never iterated, into `AnimationEventsDropped { Entity skeleton; Entity layer; AssetHandle<Clip> clip; uint64_t dropped; }` (saturating), and the drop is logged once per instance.
  - Delivery is best-effort per frame. For tick-posed skeletons it is per tick.
- **Root motion is not extracted**, so the root bone moves relative to its entity.

### FenrirAnimation: blending (ADR-0036)

- **Layers:**
  - `Animator` is the base layer, a full override with no weight, mask or mode, always first.
  - Every other layer is an entity carrying `AnimationLayer { Entity skeleton; int order = 0; float weight = 1; LayerMode mode = Override; AssetHandle<BoneMask> mask; AdditiveReference reference = ClipStart; AssetHandle<Clip> clip; float speed = 1; ClipLoop loop = Loop; bool playing = true; float fadeSeconds = 0; FadeCurve fadeCurve = Smooth; }`, ordered by `order` then entity. Its `skeleton` need not be its ECS parent.
  - `Seek` and `Playback` take the skeleton entity for the base layer, or the layer entity.
  - A layer with no base `Animator` layers over the retained base pose.
- **Composition:**
  - Layers apply in sequence, with no normalisation, at an effective weight of `weight × mask(bone)` clamped to [0, 1].
  - The base layer resolves unanimated channels to rest. Override layers pass unanimated channels through. Additive layers treat them as identity deltas.
  - **Override:** lerp translation and scale, shortest-arc nlerp rotation.
  - **Additive:**
    - translation: `current + (sample − reference) × w`;
    - rotation: `slerp(identity, sample × inverse(reference), w) × current`;
    - scale: the per-component ratio, `lerp(1, ratio, w)`. Only positive ratios are supported: a non-finite value, a `|reference| < 1e-6` or a non-positive ratio gives 1, with a diagnostic once per binding.
- **Reference poses:** `ClipStart` (the clip at 0) or `RestPose`. Each is a complete local pose (rest where the clip is silent), computed at run time and cached per (clip, skeleton, mode) revisions.
- **Crossfades:**
  - A clip change with `fadeSeconds > 0` fades; 0 cuts.
  - The outgoing clip keeps advancing on its own cursor.
  - An interrupted fade freezes a snapshot of the blended pose, with its coverage bits, and fades from it. There is no transition stack.
  - Progress advances by unscaled `deltaTime` (the tick for tick-posed skeletons), freezes while paused, and is clamped to [0, 1]. The weight is `Linear` or `Smooth` (smoothstep) of progress.
  - A `fadeSeconds` change leaves a running fade alone.
  - **Coverage-aware:** per channel, `out` and `in` fall back to the underlying value (rest for the base) where their clip is silent, and additive layers do the same on deltas.
  - **On a layer:** empty → clip fades in; clip → empty fades out, then clears the contribution, keeping the entity.
  - **On the base:** clip → empty keeps the last pose; empty → clip fades from the retained pose.
  - A seek moves only the incoming cursor.
  - Only the current clip reports `finished` and sends `ClipFinished`.
- **Weight and cost:** weight never touches cursors or fades. A zero-weight layer, or a zero-mask bone, is not sampled while its clock and event detection still run. A skipped layer samples on demand for a snapshot or when its weight returns, never reusing stale state.
- **Masks:** bound per skeleton by engine name, cached per (mask, skeleton) revisions. Unmatched or ambiguous names are diagnosed once per binding and select nothing.
- **Events across layers:**
  - A clip's events fire only while its layer's `weight > 0` and the clip is fade-eligible: incoming from its fade start inclusive, outgoing until its fade end exclusive, snapshots never. The mask is ignored. Silent crossings are never replayed.
  - Eligibility is judged at each crossing's frame fraction.
  - Crossings merge chronologically across layers (ties by layer order, then index) into the per-skeleton budget, and overflow is counted per layer.
  - `ClipFinished { Entity skeleton; Entity layer; AssetHandle<Clip> clip; }` fires whatever the weight.
- **Lifecycle:**
  - A layer on a missing, non-skeleton or unready entity contributes nothing and does not advance (`Missing` or `Unavailable`).
  - Changing `skeleton` restarts the layer with no fade.
  - Removing a layer cuts it, and its state is freed at reconcile.
  - Other fields apply at the next evaluation.
- Blend trees, blend spaces, state machines and sync groups are out of scope. Game code builds a state machine by writing these components.

### FenrirRenderer: skinning (ADR-0035, ADR-0041)

- **`SkinnedMesh { Entity skeleton; }`** sits beside `MeshRenderer` on the mesh entity, plain and blittable. It is documented prominently: a skinned item is drawn with `GetWorldMatrix(skeleton)`, and the mesh entity's own `Transform` and ancestors are never applied. `visible` and the material stay on the mesh entity.
- **Skipped or substituted, each logged once as a warning:**
  - skin data but no `SkinnedMesh`;
  - a `SkinnedMesh` whose skeleton is dead, has no `SkeletonPose`, or is not `Ready`;
  - no skinning source registered;
  - a failed compatibility check;
  - a user shader without `SKINNED`, which draws with the fallback material.

  A `SkinnedMesh` on a mesh with no skin data is ignored, and the mesh draws rigidly (logged once).
- **Variants:** the renderer sets the foundation's reserved `SKINNED` key for skinned items. The built-in programs are built with it. Joints and weights take the two vertex locations the foundation left free.
- **Palette buffer:**
  - A std430 storage buffer at set 3, binding 1, ring-buffered per frame in flight, growing by doubling, with no cache across frames.
  - Each draw record gains `paletteOffset`, in bones, and the world determinant sign.
  - Within a frame, uploads are deduplicated by `(scene, skeleton entity, PoseGeneration)`.
  - `paletteOffset + boneCount` is checked against capacity before recording. A failure is an internal error that skips the draw.
- **`FenrirBone { vec4 r0, r1, r2; }`** (48 bytes) holds rows 0–2 of `M_skin`, packed from glm by one `GpuBone` struct with static layout asserts. Debug builds check it against reflection.
- **`Fenrir/Skinning.glsl`** joins the engine includes, with `FenrirSkinPosition`, `FenrirSkinNormal` and `FenrirSkinTangent`. They blend four rows into `M` (upper 3×3 `A`).
  - Zero total weight is the identity in bind space.
  - Normals: `normalize(sign(det A) × cof(A) × n)`. When `|det A| < 1e-6 × c³`, or the result is too short or not finite, it falls back to `normalize(A × n)`, then to `n`.
  - Tangents: `A × t`, through the world 3×3, Gram–Schmidt'd against the final normal, with a perpendicular fallback.
  - Handedness: `t.w × sign(det A) × sign(det W)`.
  - The world normal matrix and sign are shared with rigid draws.
- **Bounds:** `GetWorldBounds` of a skinned mesh unions `M_model[i] × box_i` and the zero-weight box under `W(owner)`, on call. It is conservative, and `nullopt` when not `Ready` or with no source. A transparent skinned item's sort depth is its bounds' centre.
- **Compatibility:** checked when a pairing changes and cached per mesh entity, keyed by the mesh's handle and revision and the skeleton's. It passes when the mesh's `boneCount` ≤ the skeleton's bone count, and the signatures match unless `expectedSignature` is 0.
- **`Stats`** gains `paletteBytesUploaded` (bytes copied into the frame's palette buffer), `distinctPalettesWritten` (after deduplication) and `skinnedDraws`. They reset each frame across every view.
- **Diagnostics are warnings** (ticket 34): these skip cases, and the foundation's magenta missing-material fallback, log as warnings once each, so smoke runs pass. Errors are kept for broken engine contracts: internal renderer failures, invalid buffer access, shader compilation.

### Scripting: the FenrirScripting prerequisite (ADR-0039)

- **Extension stages:**
  - A `ScriptingExtension` declares the stages it hosts by full name.
  - C# joins one with `[StageSystem(name, Order = …)]` on an ordinary `ISystem`, registered by the system generator. Naming a stage no extension declares fails the script's load, with a diagnostic naming the system and the stage.
  - On load, FenrirScripting tells each extension which `(stage, order)` groups the script has. The extension places one C++ item per group in its own work, and removes them when the script is released. Each item makes one call per frame and runs its group's systems in name order.
  - Equal orders run C++ first.
  - Structural changes recorded by stage systems play back when the hosting system returns: for pose stages, after the whole pose pipeline.
  - A throw drops the system's changes and disables it until the next reload.
  - A stage that never runs (package absent, inactive scene) is not an error.
- ADR-0009, ADR-0029 and the README's extension sections are amended for it.

### Scripting: `Fenrir.Skeleton`, `Fenrir.Animation` and `SkinnedMesh` (ADR-0039, ADR-0040)

- **Packaging:**
  - `Skeletons::ScriptingExtension()` lives in `FenrirSkeletonScripting`, with its contract in `packages/FenrirSkeleton/Managed/Fenrir.Skeleton/`.
  - `Animation::ScriptingExtension()` lives in `FenrirAnimationScripting`, with its contract in `packages/FenrirAnimation/Managed/Fenrir.Animation/`.
  - Stages `Fenrir.Skeleton.Animate`, `.Modify` and `.Late`, named in C# by `PoseStages`.
  - `SkinnedMesh` is mirrored in `Fenrir.Renderer`.
  - `AssetHandle<T>` gains the markers `Skeleton`, `Clip` and `BoneMask`.
- **`Fenrir.Skeleton`:**
  - **Mirrors:** `SkeletonPose` (with `Timing`), `BoneAttachment`, `BoneName` (with its rejecting `Set`), `Target`, and the enums `PoseState`, `PoseTiming`, `PoseKind`, `AttachmentState`, `DetachMode`, `PoseSpace`, `Channels` and `Scope`.
  - **Bones** are `int`, −1 for none. `Skeletons.FindBone(entity, name)`. `SkeletonData` queries by handle: `BoneCount`, `Parent`, `IsDescendant`, names into a `Span<char>` (returning the length), and a subtree into a `Span<int>`.
  - **Per-bone reads:** `GetLocal`, `GetBaseLocal`, `GetModel`, `GetBaseModel` and `GetBoneWorldMatrix(entity, bone, out Matrix4x4)`. The base reads and the world read take an optional `PoseKind`. Each returns `false` when not `Ready` or out of range.
  - **Copies,** split by name (settled while writing this spec): `ReadLocals(entity, Span<Transform>)` (final), `ReadBaseLocals(entity, Span<Transform>, PoseKind kind = Render)` and `ReadModel(entity, Span<Matrix4x4>)`, each returning the bones written.
  - `PoseState` and `PoseGeneration`. The palette is not exposed, and there are no spans over native memory.
  - **Base-pose writes:** `SetBaseLocal` and `SetBasePose(entity, ReadOnlySpan<Transform>)`. They are refused with a log in `Modify` and `Late`, and on an instance of the other timing during `Animate`.
  - **Inside `Animate`:** `PoseStages.Timing` and `PoseStages.Step` give the current pass.
  - **In `Modify`:** `Skeletons.Edit(entity)` returns a `ref struct PoseEdit` holding an edit token, with `Base`, `Local`, `Model` and `Write` as native calls. A stale token, or `Edit` outside `Modify`, writes nothing and logs once.
  - **Outside the pipeline:** the override functions and override weights. `AttachmentState`. `Detach` and the headless `Skeletons.SpawnModel` through the change buffer. No `Ensure`.
- **`Fenrir.Animation`:**
  - **Mirrors:** `Animator` and `AnimationLayer`, with `ClipLoop`, `LayerMode`, `AdditiveReference`, `FadeCurve` and `MaskScope`.
  - `Animation.Seek`, and `Animation.Playback(entity, out PlaybackInfo)`.
  - `AnimationEvent`, `ClipFinished` and `AnimationEventsDropped` through `EventReader<T>`.
  - `ClipEventId.Of(name)`, computed in C# by the same FNV-1a.
  - **`Clips`:** `Duration`, `EventCount`, `GetEvent`, names into spans, `FindEvents`, `AddEvent` and `RemoveEvent`.
  - **`BoneMasks`:** `Create`, `Include` and `Clear`.
- **The foundation's no-op rules:** functions return `false`, 0, −1 or an empty result; a debug build warns once per call site; nothing throws.

### AnimationDemo (ticket 34, ADR-0040)

- **App:** `examples/AnimationDemo/` through `fenrir_add_demo_app`, on the foundation's scaffold, with its smoke runs on null everywhere and llvmpipe on Linux CI. It has no smoke-run exceptions; the warning rule above keeps scene 3 passing. Its README gives each scene's purpose, keys, expected behaviour, the tickets it depends on and the hand checks.
- **Assets** in `examples/AnimationDemo/assets/`, with an `ATTRIBUTION.md` (author, licence, source URL):
  - a Quaternius CC0 glTF humanoid with clips from its own rig (idle, walk, run, wave), so no retargeting is assumed;
  - the Khronos `Fox`;
  - one Kenney CC0 animated FBX character.

  The ticket adding each asset checks its exact files and licence first. Fox's model is CC0, and its rig and animation CC-BY 4.0.
- **Scenes, in order:**
  1. *Playback:* `Animator` speed, reverse, `Loop`/`Once`, pause and seek; clip events as world text; a short fast clip hitting the 64-event budget, with `AnimationEventsDropped` on screen.
  2. *Skeleton:* the skinned humanoid with bones through FenrirDebugDraw (parent-to-child lines, axes, names as world text), keys toggling mesh, bones and names; the Fox and the FBX character beside it.
  3. *Skin diagnostics:* one labelled entity per skip case (no `SkinnedMesh`, an `Unavailable` skeleton, a failed bind signature, a shader without `SKINNED` drawn magenta), each label placed even when its draw is skipped.
  4. *Blending:*
     - a crossfade with both curves, and an interruption mid-fade;
     - an upper-body override layer with a subtree mask over a walk;
     - an additive layer with both reference modes;
     - a layer weight ramped to 0 and back with its clock running;
     - events silenced at weight 0.
  5. *Bone attachments:*
     - attachments on the humanoid, an attachment on an attachment, and the failure cases with their `AttachmentState`;
     - a tick-posed character whose kinematic shield, moved by demo glue in `Tick`, pushes dynamic boxes. The app installs FenrirPhysics for this.
  6. *Going limp:* a subtree override going limp and blending back, with no physics.
  7. *Crowd:* a grid of the humanoid, 64 to start, doubling and halving between 1 and 1024, clips and start times staggered by `Seek`, with pause and reset. Screen text shows instances, total bones, frame time and the palette stats. No pass/fail thresholds.
  8. *Playback (C#):* clip control, seeking, playback state and events, plus a crossfade and one masked layer. Blending gets no twin of its own.
  9. *Bone attachments (C#):* creating and editing attachments, bone reads and attachment states.
- A bind-pose preview is not shown.

## Testing Decisions

- **What a good test is:** the foundation's rule. It drives a package through its public interface and asserts on observable results (poses, bone matrices, events, draw lists, pixels, asset data), with `App::RunFrame` at a chosen delta, never on private state.
- **Seam 8 (new), FenrirSkeleton headless,** with no animation, renderer or physics. Cases:
  - **Instances:** a procedural three-bone chain posed through `EditBasePose`, its model matrices and `GetBoneWorldMatrix` under a moved skeleton entity; reconcile create, rebuild and sweep; `Ensure`; `Ready`/`Unavailable`/`Missing` and recovery; span validity; `PoseGeneration` advancing on pose changes and rebuilds but not on moves, and never repeating under a recycled entity; the palette built lazily and only when read.
  - **Pipeline and modifiers:** stage order and `int` order with registration ties; handle removal; write rules per stage; inactive scenes paused; local, model and world targets; channel masks; sequential composition; lazy model matrices; degenerate inverses warned and skipped; shear dropped; clean instances skipped.
  - **`PoseOverride` and ragdoll handover** (ADR-0038's headless test): plain `Transform` entities as fake bodies with a test glue writing world targets in `Update`. Mapped bones match their bodies; unmapped children follow; skipped bones keep their animated pose; a subtree weight of 0.5 blends halfway and a later per-bone weight wins; clearing entries keeps weights; a rebuild drops both; base reads ignore overrides.
  - **Attachments:** placement under the skeleton and under other parents; topological order with an attachment on an attachment and a skeleton under an attached entity; cycles `Unplaceable`; every `AttachmentState`; keep-last-transform and recovery; name changes and rebuilds re-resolving; over-long and unterminated names; transactional `Detach` in both modes.
  - **Tick-posed:** one evaluation per tick, the clock by `tickRate`, none in a tickless frame; the render base pose as the blend at `accumulator / tickRate`; `Simulation` reads in `Tick` matching tick-time sampling, and equal to render reads on frame-posed skeletons; modifiers changing the render pose only; switching timing without a jump; `Animate` items seeing the right timing, step and instances in each pass; `EditBasePose` on the other timing refused; a test glue placing a plain entity on the simulation bone each tick.
  - **Spawning:** `SpawnSkeletons` and `Skeletons::SpawnModel` on fixtures: owners, synthetic owners, attachments of ordinary nodes.
  - **Install:** failing without FenrirAssets; the skinning source registered and removed.
- **Seam 9 (new), FenrirAnimation headless,** on procedural clips, skeletons and masks:
  - **ADR-0034's interval table:** forward from 0, across a wrap, reverse across 0, a seek, a pause, zero duration, 100 laps against the budget.
  - **Playback:** seek and clip-change precedence; completion resets.
  - **Sampling:** `Step`, `Linear` and `CubicSpline` against glTF's formulas, tangent scaling and hemisphere flips.
  - **Assets:** clip loss and recovery with no catch-up; revision rebinding with clamp or wrap; binding diagnostics and cache cleanup.
  - **ADR-0036's composition, additive, crossfade, event and lifecycle cases.**
  - **Tick-posed skeletons** advancing by `tickRate` with events per tick.
  - **Install** failing without FenrirSkeleton.
  - **The headless bone check:** a small checked-in fixture (Khronos `RiggedSimple` or a hand-written rig) played to known times, with bone world transforms compared against analytic values.
- **Seam 4 (existing), FenrirAssets,** extended:
  - `SkeletonData` fields, engine names (duplicates, over-long), `bindSignature` determinism and canonical zero, `IsDescendant` and subtrees, `Add<SkeletonData>` validation;
  - the quantiser (exact sums, remainder distribution, zero weights, malformed input refused), per-bone boxes;
  - clip-event and mask edits bumping revisions;
  - the `SkinningSource` interface compiling without FenrirSkeleton.

  Importer fixtures (ADR-0031's mandatory list):
  - bind-pose reconstruction for every fixture;
  - the numerical owner test against glTF's formula;
  - baked normals and tangents under non-uniform scale and a negative determinant;
  - glTF and FBX fixtures for multiple and unrelated armatures, shared-joint skins, a joint `skin.skeleton`, unweighted leaves, intermediate nodes, non-bone attachments, shared skeletons, an incompatible skin and duplicate names;
  - `Partial` results failing the smallest unit;
  - hemisphere flips and FBX baking.
- **Seam 1 (existing), `RendererProbe` on the null backend,** extended:
  - skinned items carrying the skeleton's world matrix and `paletteOffset`;
  - two meshes and two cameras sharing one palette range;
  - every skip case logging once, including no skinning source and same-count incompatible skeletons;
  - palette ranges within capacity;
  - `GetWorldBounds` following a moved bone and containing CPU-skinned vertices;
  - `Stats`' palette counters;
  - rigid drawing unaffected with no FenrirSkeleton installed;
  - `SpawnModel` with and without the source.
- **Seam 2 (existing), GL pixel probes,** extended:
  - a two-bone quad with one bone rotated 90°;
  - a `GpuBone` with translation, rotation and non-uniform scale agreeing on CPU and GPU;
  - a non-uniformly scaled bone's normal through `builtin:lit`;
  - owners with non-identity transforms.
- **CPU unit tests:** `GpuBone` packing; cofactor normals against the inverse transpose (negative determinants included); near-singular and non-finite fallbacks; tangent handedness under non-uniform and negative scale; rigid and skinned paths agreeing for rigid transforms.
- **Seam 7 (existing), FenrirScripting's harness:**
  - extension stages: groups placed by order among C++ work, an undeclared stage failing the load, structural changes after the pipeline, a throw disabling the system, reload replacing groups;
  - each mirror's layout, and `new` equal to `{}`;
  - a C# `Modify` system between two C++ modifiers by order; a stale `PoseEdit` writing nothing; a C# `Late` system reading this frame's bone;
  - `PoseStages.Timing` in both passes;
  - reads and `ReadLocals`/`ReadBaseLocals`/`ReadModel` agreeing with C++;
  - `ClipEventId.Of` matching C++; events read in C#;
  - missing packages no-oping.
- **Demo apps:** AnimationDemo smoke runs in CTest on null everywhere and on llvmpipe on Linux. Whether a scene looks right is a hand check from its README.
- Prior art: `PhysicsProbe`, the foundation's seams, physics's extension tests and `ScriptingProbe`.

## Out of Scope

- **Richer animation** (map, "Not yet specified"):
  - state machines and graphs, blend trees, blend spaces, sync groups;
  - interruptible transitions beyond a snapshot;
  - another clip as additive reference, and signed-scale additive animation;
  - IK solvers (the modifier interface is in scope; solvers are not);
  - root-motion extraction; morph targets;
  - node animation of entity transforms; ping-pong looping;
  - importer-sourced clip events, and a global time scale.
- **Further pose work:**
  - modifiers on the simulation pose;
  - pose pipelines for inactive scenes;
  - tracking actual writes and dirty subtrees;
  - a dedicated ragdoll modifier;
  - override weights kept across compatible rebuilds.
- **Crowds, data and C#:**
  - crowd features (level of detail, reduced update rates, instanced skinning);
  - ozz or ACL behind a private seam;
  - compatible skeleton reloads that keep the pose; import options (an *import as unskinned* fallback, the FBX sample rate); retargeting;
  - zero-copy pose spans for C#.
- **Rendering:** compute skinning; a bind-pose preview; dual-quaternion or other normal skinning.
- **The physics side of ragdolls and hit boxes:** the bone-to-body mapping and offsets, bodies, joints, motors, collision groups, where the glue lives, the skeleton entity's follow policy, and kinematic and dynamic switching. They stay on the physics map.
- **The editor's virtual bone hierarchy.**

## Further Notes

- **Decision history:**
  - the wayfinder map `.scratch/fenrir-renderer/map.md` and its tickets 07, 24, 26–34 and 62;
  - ADR-0028, ADR-0031, ADR-0033 to ADR-0040, and ADR-0041 (new);
  - the foundation spec `.scratch/fenrir-renderer-foundation/spec.md` (in history up to 28d8f69).
- **Settled while writing this spec** (2026-10-22, by the user):
  - **Owner name:** FenrirSkeleton's owner and facade is `Skeletons`, so the foundation's `AssetHandle<Skeleton>` keeps its short name. Masks follow the rule too: `AssetHandle<BoneMask>` resolves to `BoneMaskData`.
  - **C# pose copies are split by name:** `ReadLocals` (final), `ReadBaseLocals` (base, with an optional `PoseKind`) and `ReadModel`. `PoseKind` keeps only ADR-0040's meaning, `Render` or `Simulation`.
  - **`Animate` passes:** `Animate` items run in the frame pass and in each tick pass, their context giving the pass's timing, step and instances. `EditBasePose` on the other timing during `Animate` is refused. C# reads `PoseStages.Timing` and `PoseStages.Step`.
  - **Build:** FenrirSkeleton and FenrirAnimation are built with `FENRIR_ENABLE_ASSETS`, with no option of their own. Each install fails clearly without what it needs.
  - **ADR-0041:** the renderer does not depend on or require FenrirSkeleton. It reaches live poses through a `SkinningSource` interface in FenrirAssets that FenrirSkeleton registers, and skips skinned draws without one.
  - These go no further than the spec, ADR-0041, its amendment lines in ADR-0028, ADR-0031 and ADR-0035, and the glossary. No ticket 77 was needed.
- **Order for `/to-tickets`:**
  1. FenrirAssets' animation data: `SkeletonData`, `ClipData`, `BoneMaskData`, `MeshData`'s skin section and the `SkinningSource` interface.
  2. The importer reading skins, skeletons and clips.
  3. FenrirSkeleton: `Skeletons::Install` with the skinning source, instances and reads, the frame pipeline and stage registration, `PoseGeneration` and the palette, and spawning.
  4. FenrirAnimation playback and events.
  5. GPU skinning, which creates AnimationDemo with scenes 1–3, its README and smoke runs. The crowd scene (7) and the palette stats come with it or right after.
  6. Blending (scene 4).
  7. `PoseOverride` and override weights (scene 6).
  8. Bone attachments (scene 5's first part).
  9. The FenrirScripting extension-stage prerequisite, which can be built any time before the C# tickets.
  10. The C# extensions (scenes 8 and 9).
  11. Tick-posed skeletons, last, after playback, overrides and attachments work per frame (scene 5's shield).

  Each ticket adds its scene and hand checks as part of done.
- **Risks:**
  - Unspiked: everything here; nothing was prototyped.
  - FBX through ufbx with real rigs, which is why the Kenney character is in the demo.
  - The cofactor normal path on drivers other than the developer's.
  - Event-budget arithmetic across layers and laps.
  - Extension stages, a new FenrirScripting mechanism.
- **Engine changes land outside the new packages:** FenrirAssets (data, importer, the skinning source), FenrirRenderer (skinning), FenrirScripting (extension stages) and the demo scaffold's app list.
- **Vocabulary** follows `GLOSSARY.md`: FenrirSkeleton, FenrirAnimation, bone, bone weight, clip, skeleton entity, skeleton pose, synthetic owner, ordinary node, rest pose, bind pose, skeleton bind space, base pose, final pose, pose modifier, pose target, pose edit, pose pipeline, pose stage, skinning palette, skinning source, pose generation, pose override, override weight, ragdoll glue, skinned mesh, bind signature, bone attachment, attachment state, pose timing, simulation pose, render pose, animator, playback cursor, animation event, animation layer, crossfade, bone mask, additive layer, reference pose, extension stage, scripting extension, plus the foundation's terms.
