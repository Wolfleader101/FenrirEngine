---
status: accepted
---

# Skeleton poses live in FenrirSkeleton, and models import through cgltf and ufbx into one bind space

ADR-0028 made skeletons bone arrays in a `SkeletonPose` component and left how instances are spawned, owned and read to renderer map ticket 26. Two facts decided it. First, a C#-visible component must be a blittable mirror (`Iris::StructValue`), and ADR-0023 relies on components being plain data, so a component cannot hold the pose arrays. Second, assimp loses what a correct skeleton needs. Its glTF importer copies each skin into per-mesh `aiBone` lists, never reads `skin.skeleton`, and keeps no skin identity. Its FBX importer exposes no `LimbNode` flag and gives only mesh-to-bone offsets, with no separate bind pose. FenrirEngine therefore keeps the component plain, moves the arrays into FenrirSkeleton, and imports through format-specific libraries into one documented bind space. A successfully imported model behaves the same whether it came from glTF or FBX.

## Skeleton instances

- **`SkeletonPose` is plain data.** It is `SkeletonPose { AssetHandle<SkeletonData> skeleton; }`, blittable and mirrored in C#. The entity carrying it, the skeleton entity, identifies one pose instance. Entities sharing a `SkeletonData` have independent poses. *Amends ADR-0028*, whose `SkeletonPose` "holds contiguous arrays": the arrays belong to FenrirSkeleton, and the component names the instance.
- **`SkeletonPoses`, a FenrirSkeleton scene resource, owns the arrays**: the base pose, the final pose, the model-space matrices and the palette. Its storage is private, so it can become pooled or more data-oriented for crowds without changing the component.
- **Reconcile, not hooks.** Before the first pose stage each frame, a pass compares the components with the instances, as physics does with its components (ADR-0011):
  - it creates instances for `SkeletonPose`s added from C++, C# or spawning;
  - it rebuilds an instance at the rest pose when its handle or the asset's revision changes;
  - it drops instances whose entity or component is gone.

  `Ensure(entity)` runs the same step for one entity at once.
- **Instance state.** `PoseState(entity)` is one of:
  - `Ready`: reconciled against a live asset at a known revision.
  - `Unavailable`: the handle is stale or empty. Readers get empty spans, `GetBoneWorldMatrix` returns `nullopt`, the renderer skips that skeleton's skinned draws, and the problem is logged once. Bone attachments keep their last transform but report themselves unresolved.
  - `Missing`: no instance exists yet.

  When the asset becomes available again, or the handle changes, the instance is rebuilt automatically, and bone indices and attachments re-resolve before their next update.
- **Reads and writes.**
  - Readers get `std::span<const …>` for local poses (base and final), model matrices and the palette, plus `FindBone(entity, name)`. A span is valid until the end of the current system or the next structural change to `SkeletonPoses` (reconcile, `Ensure`, a sweep), whichever comes first.
  - The base pose is written through `EditBasePose(entity)`, which marks the instance pose-dirty, even if no value changes. The final pose is written only by modifiers and finalisation (ticket 27).
  - Three kinds of change are kept apart:
    - a pose change dirties model matrices and the palette;
    - a change to the skeleton entity's world transform affects only world queries and attachments;
    - a change of asset or revision rebuilds the instance.

## SkeletonData and the model's node tree

- **The `Model` keeps the whole source tree.** Every node is kept, bones included, with its source name, source identity, rest TRS and parent. Each node is flagged as a node, a synthetic owner, or bone *i* of skeleton *k*. A model lists several skeletons, not one.
- **`SkeletonData` per bone** holds:
  - the source name;
  - a unique **engine name**, which is the source name, suffixed `#2`, `#3`… only on duplicates, with a diagnostic;
  - the model node index;
  - the parent index (lower than the bone's own index, or −1 for a root; several roots are allowed);
  - the **rest** local TRS (the pose as authored);
  - the **inverse bind** (from the skin's bind pose). Rest and bind are distinct, and files often differ between them.

  The name map is derived when the asset is built, never stored. Lookups by engine name are exact, and lookups by source name return the first match. Bone indices are u16 in storage; the practical bone count per draw is the renderer's (ticket 30). Indices are stable for one import of one file, and are not identifiers that persist across arbitrary edits to the source file.
- **Procedural skeletons.** FenrirAssets gains `Add<SkeletonData>`, so tests and procedural rigs need no file.

## Which nodes are bones

A node is a bone if any of these holds:

1. It is a joint of a skin.
2. It lies on the path between a skeleton's joints and its root bone.
3. It is below a bone, a clip in the file animates it, and it carries no mesh, camera or light.
4. The format marks it as a bone: FBX `LimbNode` through ufbx. glTF has no such marker.

Every other node stays an ordinary node, including empty children of bones. `SpawnNodes` spawns it as an entity, and FenrirSkeleton gives it a `BoneAttachment` to its nearest ancestor bone. The offset is its full rest transform relative to that bone. A clip that animates an ordinary node keeps that channel; it is neither dropped nor redirected into the skeleton.

## Skeletons and their owners

- **Grouping by source joint identity, not by name**, in source order: skin index, then joint order, then parents-first. Joint references and vertex bone indices are remapped to that final order.
  - Disjoint skins are separate skeletons.
  - Skins that share joints merge if their binds agree, or if one matrix `C` takes the later skin's normalised vertex space into the first skin's: `B_first[j] × C = B_later[j]` for every joint they share. `C` is checked over all of the later skin's joints and baked into its vertices.
  - Otherwise the later skin is incompatible and fails. It is never merged silently.
- **The owner (skeleton entity)** is the `skin.skeleton` node when that node is not a joint. Otherwise it is the nearest non-bone ancestor of the group's root bones. `skin.skeleton` is never used in skinning maths. When two or more skeletons resolve to one node, each of them gets a **synthetic owner**: an importer-made node, a child of that node with an identity transform, with its identity taken from its skeleton group and its display name `<first root bone>.Skeleton`. One `SkeletonPose` per entity stays a hard rule, and unrelated rigs are never merged to satisfy it.

## One bind space

Every importer produces data satisfying one documented relation, for each bone *i* weighting vertex *v*:

`v_world = W(owner) × M_model[i] × B[i] × v`

- **`v`** is in **skeleton bind space**: the owner's model space with the skeleton in its bind pose.
- **`B[i]`** is the inverse bind. It maps skeleton bind space to bone *i*'s space.
- **`M_model[i]`** maps bone *i*'s space to owner space (ADR-0028).
- **`W(owner)`** is the owner's world matrix.
- **Convention:** matrices multiply column vectors on the right, and are stored column-major, as in glm. Both the convention and the storage are stated, not assumed.
- **Bind identity:** `M_model_bind[i] × B[i] = I`, checked against the bind pose rebuilt from bind data, never against the rest pose, which may differ. At the bind pose a skinned mesh sits exactly where a static child of the owner with the same vertices would.

Per format:

- **glTF (cgltf).** When the owner's rest world `W_o` (in glTF's scene-root space) is the identity, the skin's inverse binds and the vertices are kept unchanged. Otherwise they are converted: `B = IBM × W_o` and `v = W_o⁻¹ × v_gltf`. A skinned mesh node's transform is ignored, as glTF specifies. An omitted inverse-bind accessor means identity matrices, as the spec says.
- **FBX (ufbx).** `B[i] = (W_o⁻¹ × bind_to_world[i])⁻¹`, from ufbx's explicit cluster bind transforms. Vertices become `W_o⁻¹ × bind_to_world[i] × geometry_to_bone[i] × v`, and that product must agree across all of the skin's clusters. ufbx's axis and unit conversion is applied as an adjustment on the root node, so it sits above every owner.
- **Baking.**
  - Positions are transformed by the matrix.
  - Normals are transformed by its inverse transpose and renormalised.
  - Tangents are transformed by its upper 3×3, with handedness flipped when its determinant is negative.

  Joint indices are remapped in the same pass. A non-invertible owner world, bind or conversion matrix is a diagnostic that fails its skeleton or skin.

## Failure and diagnostics

- **The load result** is `Succeeded`, `Partial` (with diagnostics) or `Failed`. A file that cannot be read fails the load. Otherwise each error fails the smallest unit containing it:
  - a skeleton with invalid structure or binds fails, together with its skins and their meshes;
  - an incompatible skin fails, together with its meshes;
  - an ordinary node with a degenerate transform is a warning and is kept.
- **Validation** covers:
  - non-finite TRS values;
  - zero or degenerate scales and binds;
  - parent indices out of order, or cyclic;
  - one source node used twice as a joint of a skeleton.
- **No half-built skinning.** Meshes of a failed skin or skeleton are omitted. They are never imported looking valid with no skin. There are no import options yet; an *import as unskinned* fallback waits for a future import-options struct.

## Spawning

- **`SpawnNodes`.** FenrirAssets' `SpawnNodes(scene, AssetHandle<Model>, parent = {})` spawns the non-bone nodes (synthetic owners included) with `Name`, `Transform` and their hierarchy. It returns `SpawnedNodes { Entity root; std::vector<Entity> nodes; }`, indexed by model node, with null entries for bones.
- **`SpawnSkeletons`.** FenrirSkeleton's `SpawnSkeletons(scene, model, spawned)`:
  - adds `SkeletonPose` only to owners whose skeleton loaded;
  - calls `Ensure` on each;
  - only then adds the `BoneAttachment`s for ordinary nodes below bones.
- **Wrappers.** `Skeleton::SpawnModel(scene, model, parent)` wraps both for headless apps. The renderer's `SpawnModel` calls both and then adds `MeshRenderer`s and skin references (ADR-0023, as amended by ADR-0028).

## Clip channels

Each clip channel keeps its target's source node identity and engine name. A clip binds to skeletons from its own file by identity, which is exact even when names repeat. It binds to any other skeleton by engine name, as ADR-0028 says, with a diagnostic for a missing or ambiguous match. *Refines ADR-0028's name rule.* Retargeting across proportions, rest poses or orientations is future work.

## Importers

The model-import seam (ADR-0021) gets two private backends: **cgltf** for glTF and GLB, and **ufbx** for FBX and OBJ, each linked `PRIVATE`. **assimp is dropped.** New formats join behind the same seam without changing the asset model. *Amends ADR-0021's importer line* (assimp for models) and ticket 21's submodule list.

## Tests

The following tests are mandatory:

- **Headless pose tests:** a procedural three-bone chain posed through `EditBasePose`, its finalisation run directly, and its model matrices and `GetBoneWorldMatrix` checked under a moved skeleton entity.
- **Bind-pose reconstruction:** for every importer fixture, CPU skinning at the rebuilt bind pose gives the unskinned rest geometry.
- **A numerical owner test:** an owner that is translated, rotated and non-uniformly scaled, with a mesh using several bone influences per vertex, checked against glTF's own skinning formula.
- **Baked normals and tangents** under non-uniform scale and a negative determinant.

The glTF and FBX fixtures cover:

- multiple armatures, including unrelated skeletons under one root;
- skins sharing joints;
- a `skin.skeleton` that is itself a joint;
- unweighted leaf bones;
- intermediate nodes;
- non-bone attachments;
- meshes sharing a skeleton;
- an incompatible skin;
- duplicate names.

## Considered options

- **Vectors in the component.** Rejected: C# cannot mirror it, and copying a component copies four arrays.
- **A blittable component holding a pointer or pool offset.** Rejected: C# writes through `Ref<T>` straight into storage, so the pointer or offset is fragile.
- **Keeping assimp and inferring skins, joints and binds.** Rejected: skin identity, `skin.skeleton`, `LimbNode` and the FBX bind pose are lost before the importer sees them.
- **cgltf for glTF with assimp kept for the rest.** Rejected: FBX would still lack an exact bind pose, and assimp's long tail of formats is little use to a game engine.
- **Classifying every descendant of a bone as a bone.** Rejected: glTF lets ordinary nodes and whole mesh subtrees hang from joints, and a socket would vanish from the spawned hierarchy.
- **Merging skeletons that share an owner node.** Rejected in favour of synthetic owners: unrelated rigs would share a pose.
- **Importing a failed skin's meshes unskinned by default.** Rejected: they would look valid and render wrongly.

## Consequences

- ADR-0028's `SkeletonPose` and clip-binding lines are amended as above, as are ADR-0021's importer line and ticket 21's libraries (cgltf and ufbx in place of assimp under `packages/FenrirAssets/libs/`).
- Tickets 28 (clip bindings by identity and name, channels on ordinary nodes), 30 (skipping `Unavailable` skeletons; skinned normals under non-uniform scale in the shader), 31 (attachments reporting themselves unresolved) and 33 (the C# mirror of `SkeletonPose` and span reads) build on this.
- Not yet specified: compatible reloads that keep the current pose (with skeleton `Replace` or hot reload), import options, and retargeting.
