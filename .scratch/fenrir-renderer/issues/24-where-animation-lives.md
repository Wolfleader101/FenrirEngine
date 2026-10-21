# Where does animation live: its own package or inside the renderer?

Type: grilling
Status: resolved
Blocked by: 07, 14

## Question

Is skeletal animation its own package (FenrirAnimation?) or part of FenrirRenderer?

- Which side owns skeletons, poses and clips, and which side skinning (the bone-matrix buffer, skinned meshes)?
- What plain data does it read from and give to physics (the simulated ragdoll pose, the bone-to-body mapping) without either depending on the other?
- Does an app without rendering (a server, a headless test) still animate?
- Which foundation decisions it must wait on before its own spec's tickets can graduate.

## Answer

ADR: `docs/adr/0028-skeletons-are-bone-arrays-in-fenrirskeleton-animated-by-fenriranimation-and-skinned-by-the-renderer.md`. Glossary: FenrirSkeleton, FenrirAnimation, Bone, Skeleton entity, Skeleton pose, Base pose, Final pose, Pose modifier, Skinning palette, Bone attachment, Clip.

- **Seven parts:** `SkeletonData` (the immutable asset, ADR-0021), `SkeletonPose` (per-instance mutable pose), FenrirAnimation (clips into the base pose), pose modifiers (IK, procedural, ragdoll, gameplay), finalisation (final locals, model-space matrices, palette, attachments), FenrirRenderer (consumes the palette), physics integration (glue moving plain transforms).
- **Bones are arrays, not entities:** stable parents-first indices, contiguous pose arrays, no hierarchy walk per bone. Bones as entities (Bevy) was rejected for its cost at scale and being hard to reverse; attachments and editor display are rebuilt on the arrays.
- **Packages:** a new FenrirSkeleton (on FenrirAssets and the ECS) owns `SkeletonPose`, bone lookup, modifiers' entry points, finalisation and `BoneAttachment`, and works without animation. A new FenrirAnimation (on FenrirSkeleton and FenrirAssets) owns clips and blending, our own runtime on glm, not ozz. FenrirRenderer depends on FenrirSkeleton, never FenrirAnimation; none of them on physics or physics on them.
- **No renderer needed:** servers and headless tests spawn, animate and read bones, and are tested so.
- **Poses:** local transforms are canonical; base pose and final pose both kept and readable (powered ragdolls target the base pose); modifiers aim in local, model or world space and finalisation resolves them in order; the first implementation has one weighted per-bone override, documented as initial, not the contract.
- **Clips** name bones by name, resolved to indices once and cached.
- **Order:** named pose stages (animation, modifiers, finalisation), hosted in `PostUpdate` for now; fixed-tick and extra stages may come later.
- **Skinning:** a skinned mesh's entity references the skeleton entity; the renderer uploads its palette and skins in the vertex shader (`SKINNED`, four bone weights), owning buffers and synchronisation; one logical palette per skeleton, no promise of one physical upload; the importer maps mesh bone indices onto the skeleton's order and the skeleton's inverse binds are used.
- **Spawning:** FenrirAssets' facade gets `SpawnNodes` (non-bone nodes as entities, hierarchy kept); FenrirSkeleton adds `SkeletonPose` to each skeleton's owner; `SpawnModel` builds on `SpawnNodes` (ADR-0023 amended). Which entity owns a skeleton in multi-mesh and multi-skeleton models goes to ticket 26.
- **Attachments:** `BoneAttachment` (skeleton entity, bone by name, user-editable offset); finalisation owns the attached entity's `Transform`, converting into its ECS parent's space; failure cases to ticket 31.
- **Editor:** `SkeletonPose` and `SkeletonData` suffice to show bones as virtual hierarchy entries and edit locals; the editor is out of scope.
- **Physics:** the bone-to-body mapping moves to the physics map's ragdoll spec, keyed by bone names; FenrirSkeleton gives names, indices, hierarchy and pose access.
- **Crowds:** the array layout only; level-of-detail, reduced rates and instanced skinning in *Not yet specified*.
- **Graduated:** the animation spec's fog becomes tickets 26 (skeleton instances), 27 (pose stages and modifiers), 28 (clips and playback), 29 (pose blending), 30 (GPU skinning), 31 (bone attachments), 32 (animated and simulated poses), 33 (C#, blocked by 22) and 34 (demo app, blocked by 23). Its spec is `.scratch/fenrir-renderer-animation/spec.md`, written once they close.
