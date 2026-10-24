# How are skeleton instances spawned, owned and read?

Type: grilling
Status: resolved
Blocked by: 24

## Question

ADR-0028 puts each skeleton's runtime pose in a `SkeletonPose` component in FenrirSkeleton, with bones as arrays rather than entities. What exactly is it, and how does it come to exist?

- `SkeletonPose`'s layout: the base and final local poses, model-space matrices and the palette as arrays; how C# sees variable-length arrays on a component (or whether they live elsewhere, keyed by entity); what `GetBoneWorldMatrix` and bone lookup by name look like.
- Which entity owns each skeleton: the node above the skeleton's root bone, the model root, or another rule; models with several meshes on one skeleton, and models with several skeletons.
- `SpawnNodes` in FenrirAssets' facade: which nodes count as bones and are left out, how the hierarchy is preserved, what it returns (root, node → entity map); how `SpawnModel` builds on it (ADR-0023 amended); a headless spawn with no renderer.
- `SkeletonData` as ADR-0021 has it: whether it already carries everything ADR-0028 needs (names, parents-first order, rest locals, inverse binds) and what the importer must guarantee, above all one bind space per skeleton (ADR-0028's *Coordinate spaces*): how assimp reports glTF and FBX mesh-node transforms and bind matrices, and whether the bake goes into the inverse binds or the vertices.
- A skeleton asset that is reloaded, replaced or goes stale under a live `SkeletonPose`; a pose whose bone count no longer matches.
- Testing with no renderer and no animation: posing by hand and reading model-space matrices.

## Answer

ADR: `docs/adr/0031-skeleton-poses-live-in-fenrirskeleton-and-models-import-through-cgltf-and-ufbx-into-one-bind-space.md` (amends ADR-0021's importer line, ADR-0028's `SkeletonPose` and clip-binding lines, and ticket 21's libraries). Glossary: Synthetic owner, Ordinary node, Rest pose, Bind pose, Skeleton bind space; Skeleton pose, Base pose and Model revised.

- **Facts found:** a C#-visible component must be a blittable mirror; physics keeps variable-length state in private maps diffed each tick; C# reads arrays through span-filling functions. assimp's glTF importer copies each skin into per-mesh `aiBone`s (every joint, zero-weight dummies), drops `skin.skeleton` and skin identity; its FBX importer hides `LimbNode` (only `IsNull` survives) and gives only mesh-to-bone offsets, no bind pose.
- **Layout:** `SkeletonPose { AssetHandle<SkeletonData> }`, plain and C#-mirrorable; the arrays (base, final, model, palette) in FenrirSkeleton's private `SkeletonPoses` scene resource, reconciled each frame before the first pose stage (create, rebuild on handle or revision change, sweep), `Ensure(entity)` at once; states `Ready`/`Unavailable`/`Missing`; const spans valid within the current system and until a structural change; `EditBasePose` marks pose-dirty; pose, entity-transform and asset changes kept apart.
- **Importers:** cgltf (glTF/GLB) and ufbx (FBX/OBJ) behind ADR-0021's seam; assimp dropped.
- **Source identity:** `Model` keeps every node (bones too) with source name, identity, rest TRS, parent and a flag (node, synthetic owner, bone *i* of skeleton *k*); `SkeletonData` per bone: source name, unique engine name, node index, parent, rest TRS, inverse bind; name map derived, u16 indices, stable per import, not across arbitrary source edits; `Add<SkeletonData>` for procedural rigs.
- **Bones:** skin joints, nodes between joints and the root bone, animated mesh-free nodes below bones, FBX `LimbNode`s; all else ordinary nodes, spawned and given a `BoneAttachment` to their nearest ancestor bone; animated ordinary nodes keep their channels.
- **Skeletons and owners:** skins grouped by source joint identity; overlapping skins merge only when binds agree or one checked conversion bakes the later skin into the first's space, else that skin fails; owner = non-joint `skin.skeleton`, else nearest non-bone ancestor of the roots; colliding skeletons each get a synthetic owner.
- **One bind space:** `v_world = W(owner) × M_model[i] × B[i] × v`, column vectors, column-major; `M_model_bind × B = I` against the bind pose, rest kept apart; glTF kept unchanged unless the owner's rest world is not identity (then `B = IBM × W_o`, `v = W_o⁻¹ v`); FBX from ufbx's `bind_to_world` and `geometry_to_bone`; baking transforms positions, normals (inverse transpose) and tangents (handedness on negative determinant) and remaps joint indices in one pass.
- **Failures:** load result `Succeeded`/`Partial`/`Failed`; smallest unit fails (skeleton with its skins and meshes, an incompatible skin with its meshes); degenerate ordinary nodes warned; no half-skinned meshes; no import options yet.
- **Spawning:** `SpawnNodes` → `SpawnedNodes { root, nodes }` (null for bones); `SpawnSkeletons` adds `SkeletonPose` to loaded skeletons' owners, `Ensure`s, then adds attachments; `Skeleton::SpawnModel` (headless) and the renderer's `SpawnModel` wrap them.
- **Clips:** channels keep source identity and engine name; own-file skeletons by identity, others by engine name with diagnostics.
- **Tests:** headless hand-posed chain; mandatory bind-pose reconstruction per fixture; a numerical test with a translated, rotated, non-uniformly scaled owner and multi-influence vertices against glTF's formula; baked normals and tangents under non-uniform scale and negative determinants; glTF and FBX fixtures for multiple and unrelated armatures, shared-joint skins, a joint `skin.skeleton`, unweighted leaves, intermediate nodes, attachments, shared skeletons, an incompatible skin and duplicate names.
- **Graduated:** nothing new; notes appended to tickets 21, 28, 30, 31 and 33. Compatible reloads, import options and retargeting go to *Not yet specified*.
