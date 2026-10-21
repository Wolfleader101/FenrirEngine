---
status: accepted
---

# Skeletons are bone arrays in FenrirSkeleton, animated by FenrirAnimation and skinned by the renderer

FenrirEngine wants skeletal animation that grows into blending, layers and masks, IK, procedural posing, ragdolls and large numbers of animated characters. Two families of engines split here (ticket 07). Bevy and Unity's GameObjects make every bone an entity or `Transform`. Godot, Unreal, EMotionFX, ozz and Jolt keep a skeleton's bones as arrays in one object, and Latios Kinemation offers both in Unity's ECS, recommending the array form for crowds. FenrirEngine takes the array form for runtime state and keeps the usability of entity bones (attachments, editor display) on top of it. Skeleton state, animation, rendering and physics stay separate packages with no dependency between animation, the renderer and physics.

## The parts

Seven parts, each with one job:

1. **`SkeletonData`**: the immutable skeleton asset FenrirAssets already holds (ADR-0021). Bones in a stable, parents-first order, each with a name, a parent index, a rest local transform and an inverse bind matrix. A **bone** is named by its index in this order, and found by name.
2. **`SkeletonPose`**: per-instance, mutable pose data in a component on one entity, the **skeleton entity**. It holds contiguous arrays indexed by bone: the base pose's local transforms, the final pose's local transforms, model-space matrices and the skinning palette.
3. **FenrirAnimation**: samples and blends clips into the base pose's local transforms. It is the only part that knows clips.
4. **Pose modifiers**: IK, procedural posing, ragdoll blending and gameplay adjustments, applied in order after the base pose to produce the final pose.
5. **Finalisation**: resolves modifiers into final local transforms, computes model-space matrices parents-first and the skinning palette (model-space × inverse bind), and places bone attachments.
6. **FenrirRenderer**: consumes the finalised palette to skin meshes on the GPU.
7. **Physics integration**: app or demo glue, or the physics map's ragdoll spec, which maps bones to and from bodies through plain transforms.

## The packages

- **FenrirSkeleton** is a new package depending on FenrirAssets, FenrirECS, FenrirScene, FenrirApp and FenrirMath. It owns `SkeletonPose`, bone lookup by name, `GetBoneWorldMatrix`, pose modifiers' entry points, finalisation and bone attachments. A skeleton works without FenrirAnimation: posed by code, by a ragdoll or by an editor.
- **FenrirAnimation** is a new package depending on FenrirSkeleton and FenrirAssets. It owns clip playback and blending and writes only the base pose. It has no GPU code and links no library; its runtime is written on glm, not ozz-animation.
- **FenrirRenderer** depends on FenrirSkeleton, never on FenrirAnimation. Neither FenrirSkeleton nor FenrirAnimation depends on FenrirRenderer or FenrirPhysics, and FenrirPhysics depends on none of them.
- **No renderer needed.** A server or a headless test spawns skeletal models, animates them and reads bone positions (hit boxes, attachments) with FenrirRenderer absent, and the packages are tested that way.

## Poses

- **Local transforms are canonical.** Both the base pose and the final pose are stored as local transforms; model-space matrices and the palette are derived from the final pose by finalisation.
- **Base pose and final pose are distinct and both readable.** The base pose is what animation produced; the final pose is after every modifier. Powered ragdolls drive their motors towards the base pose, not towards their own simulated output.
- **Modifiers supply targets in local, model or world space** through FenrirSkeleton's interface (an IK solver aims in model or world space; a ragdoll produces world-space body transforms). Finalisation resolves them into final local transforms, in order.
- **The first implementation has one modifier:** a weighted per-bone override, enough for a ragdoll going limp or blending back. It is an initial implementation, not the contract for all pose modification; ordered modifiers (layers and masks, IK, procedural, ragdoll, gameplay) replace or generalise it as those features arrive.
- **Clips name bones by name**, resolved to indices once per clip and skeleton and cached, so a clip plays on any skeleton whose bone names match.

## Coordinate spaces

The skeleton's pose is not part of the ECS transform hierarchy, and the two meet at one place only: the skeleton entity's world matrix.

- **Local:** a bone relative to its parent bone; the root bone relative to the skeleton entity.
- **Model space:** a bone relative to the skeleton entity, `M_model[i] = M_model[parent(i)] × M_local[i]`, the root bone's being its local. `SkeletonPose` stores these; they never include the skeleton entity's own `Transform` or its ancestors.
- **World:** `GetBoneWorldMatrix(skeletonEntity, bone) = GetWorldMatrix(skeletonEntity) × M_model[bone]`. FenrirSkeleton provides it as the one defined way to combine the two; readers do not combine them by hand.
- **Skinning:** `M_skin[i] = M_model[i] × M_inverseBind[i]`, which maps a vertex from the mesh's bind space to skeleton model space. The renderer draws a skinned mesh with the skeleton entity's world matrix, not the mesh entity's, so a skinned mesh's own `Transform` does not move it (as glTF specifies).
- **One bind space per skeleton, made by the importer.** Imported files may bind meshes in their own node's space, or put non-identity transforms on mesh nodes (common in FBX). The importer bakes any mesh-to-skeleton bind transform into the inverse binds, or into the mesh's vertices, so that at run time the two formulas above hold for every mesh on a skeleton with no extra matrix.
- **Modifier targets** given in world space are taken into model space through the inverse of the skeleton entity's world matrix, and from model space into local space through the parent bone's model-space matrix.

## Order in the frame

- **Explicit pose stages, hosted in `PostUpdate` for now.** FenrirSkeleton defines the order: animation writes the base pose, modifiers run, finalisation runs last. The first implementation runs all three once per frame in `PostUpdate`, after game code in `Update` and physics interpolation in `AfterTicks`, before `Render`. Gameplay in tick phases sees the previous frame's final pose.
- **`PostUpdate` is not permanent.** Animation in fixed ticks, IK between animation and skinning, physics-driven and procedural stages and gameplay synchronisation with animation events may add stages or move them; the stages are named so that can happen without changing who writes what.

## Skinning

- **A skinned mesh's entity references the skeleton entity.** The renderer reads that skeleton's finalised palette in `Render`, uploads it and skins in the vertex shader under ADR-0024's `SKINNED` variant, with four bone weights per vertex (ADR-0021's importer limit).
- **One logical palette per skeleton instance**, shared by every mesh skinned to it. How many physical uploads that becomes, and the buffers, caching and synchronisation behind it, are the renderer's and not promised.
- **Bone indices and inverse binds agree.** The importer maps every skinned mesh's bone indices onto its skeleton's order, and the inverse binds the palette uses are the skeleton's, in the skeleton's one bind space (see *Coordinate spaces*), so a mesh and its skeleton cannot disagree.

## Spawning, attachments and the editor

- **Bones are not entities.** FenrirAssets' facade gains `SpawnNodes(scene, AssetHandle<Model>, parent)`, which spawns the model's non-bone nodes as entities with `Name`, `Transform` and parent links, preserving the hierarchy (attachment points and other transforms included), and returns the root. FenrirSkeleton adds a `SkeletonPose` to each skeleton's owning entity. FenrirRenderer's `SpawnModel` is built on `SpawnNodes` and adds the skeletons, `MeshRenderer`s and skin references (amending ADR-0023). A headless app calls `SpawnNodes` and FenrirSkeleton without the renderer.
- **Bone attachments are components.** A `BoneAttachment` on an entity names a skeleton entity, a bone by name (resolved to an index and cached) and an offset, the user-editable attachment transform. Finalisation computes where the entity goes, `GetBoneWorldMatrix(skeleton, bone) × offset`, and owns its resulting `Transform`, converting that into the entity's local transform under its ECS parent (which reduces to `M_model[bone] × offset` when the parent is the skeleton entity). Hand-written values in that `Transform` are overwritten.
- **The editor sees bones without entities.** `SkeletonPose` and `SkeletonData` are enough for an editor to show bones as virtual hierarchy entries and edit their local transforms. The editor itself is out of this map's scope.

## Physics

- **Plain transforms only.** Glue reads body poses and writes them as a modifier's targets, and reads the base pose to drive powered ragdolls. Physics never sees a skeleton type.
- **The bone-to-body mapping is the physics map's.** A ragdoll description keyed by bone names (body shape, offset, joint to the parent body) belongs to the physics map's ragdoll spec; FenrirSkeleton gives names, indices, hierarchy and pose access.

## Considered options

- **Bones as entities (Bevy).** The cleanest seam, since `Transform` would be the only shared data and parenting would give attachments for free. Rejected: a hundred entities per character, a hierarchy walk per bone per frame and archetype churn on spawn cost most where the engine wants to grow (crowds, IK, layers), and the decision is hard to reverse. Attachments and editor display are rebuilt on the arrays instead.
- **Animation inside FenrirRenderer.** Rejected: a server would need a renderer to move hit boxes, and the renderer would know clips.
- **The pose in FenrirAnimation, the renderer depending on it.** Rejected: a skeleton posed only by a ragdoll or by code would need the animation package, and the renderer would compile against clips.
- **The pose in FenrirAssets' facade, or in FenrirECS beside `Transform`.** Rejected: the first puts runtime state in the package of plain asset data; the second cannot hold an `AssetHandle<SkeletonData>` without making the core depend on FenrirAssets.
- **ozz-animation behind a private seam.** Deferred: it leaks SoA math, a fixed bone order capped at 1024, its own format and ratio-based time, and brings no playback state, GPU skinning or ragdoll support. It or ACL can come in privately if clip memory or sampling speed shows in a profile.
- **One universal override slot.** Rejected as the long-term contract: layers, IK and ragdolls compose in order. The first implementation keeps one weighted override and says so.
- **Designing the animation graph and the ragdoll system now.** Rejected: this decision fixes only the skeleton representation, the base/final pose boundary and the coordinate spaces. Graphs, IK and ragdoll behaviour are designed when they are built, on top of those.

## Consequences

- The animation spec's tickets graduate (renderer map tickets 26–34).
- ADR-0023's `SpawnModel` line is amended to build on `SpawnNodes`.
- The physics map's ragdoll spec owns the bone-to-body mapping, and the renderer map's destination no longer lists it.
- Not yet specified: fixed-tick animation, crowd features (level-of-detail updates, reduced update rates, instanced skinning), ozz or ACL behind a private seam, and the editor's bone display.
