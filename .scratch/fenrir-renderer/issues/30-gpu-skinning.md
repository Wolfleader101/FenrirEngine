# How does the renderer skin meshes?

Type: grilling
Status: resolved
Blocked by: 26

## Question

FenrirRenderer skins a mesh from its skeleton's finalised palette in the vertex shader (ADR-0028), under ADR-0024's `SKINNED` variant. What does that take?

- The skin reference: a field on `MeshRenderer` or its own component naming the skeleton entity; what happens when it is missing, stale, or names an entity with no `SkeletonPose`.
- The palette's upload: which set and binding (ADR-0024's draw set, or its own storage buffer), per-frame ring buffers (ADR-0019), sharing one logical palette among meshes on one skeleton without promising one physical upload.
- Limits: bones per skeleton the shader takes, four bone weights per vertex, vertex attribute layout for indices and weights.
- The draw's world matrix: the skeleton entity's, not the mesh entity's (ADR-0028); how the draw list and sort keys take it, and what a skinned mesh entity's own `Transform` means (ignored, logged, or refused).
- Bounds for skinned meshes: what `GetWorldBounds` returns, and what later culling needs.
- Keeping importer and skeleton consistent: mesh bone indices mapped onto the skeleton's order and inverse binds taken from the skeleton; how a mismatch is caught.
- A compute-skinning path (needed when several passes draw one skinned mesh, such as shadows): listed or designed.
- Tests on the null backend (the palette in the draw list) and on GL (a skinned quad read back).

## Answer

ADR: `docs/adr/0035-skinned-meshes-name-their-skeleton-through-skinnedmesh-and-read-a-per-frame-palette-buffer.md` (refines ADR-0021's `MeshData` and ADR-0031's `SkeletonData`, names ADR-0023's and ADR-0028's skin reference, extends ADR-0024's set 3). Glossary: Skinned mesh, Pose generation, Bind signature; Skinning palette revised.

- **Skin reference:** `SkinnedMesh { Entity skeleton; }` beside `MeshRenderer`, added by `SpawnModel`. The following are all skipped and logged once:
  - a mesh with skin data but no `SkinnedMesh`;
  - a dead, `SkeletonPose`-less or not-`Ready` skeleton;
  - a failed compatibility check.

  A `SkinnedMesh` on a rigid mesh is ignored. The world matrix is the skeleton entity's, and the mesh entity's `Transform` is never applied, which is documented prominently. `visible` and the material stay on the mesh entity. Sort keys are unchanged, and transparent depth comes from the skinned bounds.
- **Palette upload:**
  - A std430 storage buffer at set 3, binding 1, ring-buffered per frame in flight. No cache across frames; it grows by doubling.
  - Per-item `paletteOffset`, checked against capacity before recording.
  - Deduplicated within a frame by `(scene, skeleton entity, PoseGeneration)`.
  - FenrirSkeleton's `PoseGeneration` advances on a finalised pose change or a rebuild, from a counter shared by the whole resource, so recycled entities never collide. The skeleton entity's `Transform` does not move it. The palette is built lazily per generation.
- **Formats:**
  - `FenrirBone { vec4 r0, r1, r2; }`: rows of the affine `M_skin`, packed from glm by one `GpuBone` struct, with layout asserts and reflection checks.
  - `u16x4` joints and `unorm16x4` weights, stored in this form on `MeshData` with `boneCount`.
  - Weights are quantised to sum exactly to 65535 (remainder by largest fraction) or kept all-zero. Zero-weight vertices skin by identity in bind space.
  - Malformed indices or weights fail the skin, or are refused by `Add` and `Replace`.
  - No shader bone cap.
- **Normals:**
  - `normalize(sign(det A) × cof(A) × n)`, with scale-aware near-singular and non-finite fallbacks to `A × n`, then to the bind normal.
  - Tangents: `A × t`, then Gram–Schmidt against the final normal, with a perpendicular fallback.
  - Handedness: `t.w × sign(det A) × sign(det W)`.
  - The world normal matrix and sign are shared with rigid draws.
  - A user shader without `SKINNED` draws with the fallback material.
- **Bounds:** per-bone boxes in bone space (positive quantised weights) plus a zero-weight box, stored on `MeshData`. `GetWorldBounds` unions `M_model[i] × box_i` under `W(owner)` on call. It is conservative, not tight, and gives `nullopt` when not `Ready`.
- **Compatibility:**
  - `SkeletonData::bindSignature` is FNV-1a 64 over a versioned, canonical little-endian encoding of count, parents, engine names and inverse-bind bits (`-0` → `+0`). 0 is reserved for "unchecked".
  - `MeshData::expectedSignature` is set by the importer and is 0 for unchecked procedural meshes.
  - The signature and `boneCount` are checked when a pairing changes, and the verdict is cached.
- **Compute skinning:** deferred to *Not yet specified*. It would read the same palette buffer. The bind-pose preview is deferred too.
- **Tests:**
  - Null-backend probe tests: palette ranges and sharing, every skip case, bounds against CPU skinning, same-count incompatible skeletons.
  - GL read-backs: a two-bone quad, `GpuBone` CPU/GPU agreement, a lit normal under non-uniform scale, moved owners.
  - CPU unit tests: bind identity, packing, quantiser, signature, cofactor against the inverse transpose including negative determinants, singular and non-finite fallbacks, tangent handedness, rigid-against-skinned consistency.
- **Graduated:** nothing new. Comments appended to tickets 27 (`PoseGeneration`, lazy palette), 33 (C# `SkinnedMesh`) and 34 (bind-pose preview).

## Comments

- From ticket 26 (ADR-0031): the importer guarantees one bind space (`v_world = W(owner) × M_model × B × v`), remapped bone indices and baked vertices; skinned draws of a skeleton whose `PoseState` is not `Ready` are skipped; normals under blended skinning matrices with non-uniform scale need the shader's own handling and a test.
