---
status: accepted
---

# Skinned meshes name their skeleton through SkinnedMesh and read a per-frame palette buffer

ADR-0028 makes FenrirRenderer skin meshes in the vertex shader from FenrirSkeleton's finalised palette, under ADR-0024's `SKINNED` variant, and ADR-0031 fixes one bind space, `v_world = W(owner) × M_model[i] × B[i] × v`. This decision covers the rest:
- how a mesh names its skeleton;
- how the palette reaches the GPU;
- the data formats;
- normals, bounds and compatibility checks;
- what fails and how.

The rule throughout is that a wrong configuration is visible and skipped, never drawn as plausible but wrong geometry. The separation holds:
- FenrirAssets owns mesh and skeleton definitions;
- FenrirSkeleton owns per-instance poses and palettes;
- FenrirRenderer owns GPU palette storage, variants, draw submission and bounds;
- FenrirAnimation and pose modifiers know nothing of rendering.

## The skin reference

- **`SkinnedMesh { Entity skeleton; }`**: a plain, blittable component beside `MeshRenderer` on the mesh entity, naming the skeleton entity (the entity carrying `SkeletonPose`). `MeshRenderer` is unchanged, and rigid meshes carry nothing extra. C# mirrors it (ticket 33). The renderer's `SpawnModel` adds it, pointing at the skeleton's owner or synthetic owner.
- **What is drawn.**
  - A mesh with skin data (joints and weights) and no `SkinnedMesh` is skipped, logged once per entity. Its vertices are in skeleton bind space, so drawing it with its own entity's matrix would be wrong. A bind-pose preview through the owner is a future debug view.
  - A `SkinnedMesh` on a mesh with no skin data is ignored, and the mesh draws rigidly as usual (logged once).
  - A `SkinnedMesh` whose entity is dead, has no `SkeletonPose`, or whose `PoseState` is not `Ready` (ADR-0031) skips the draw, logged once per entity.
  - A pairing that fails the compatibility check (below) skips the draw, logged once.
- **The world matrix is the skeleton's.** A skinned item's world matrix is `GetWorldMatrix(skeleton entity)`. The mesh entity's own `Transform` and its ancestors are never applied: the importer already put the vertices in skeleton bind space. Ordinary `MeshRenderer`s behave differently, so this is documented prominently on `SkinnedMesh`. The mesh entity still owns `visible` and the material. `SpawnModel` makes skinned mesh entities children of the owner with an identity transform, so the hierarchy reads naturally without adding anything to skinning.
- **Sort keys** are unchanged (stage, pipeline, material, mesh). A transparent skinned item's depth is taken from the centre of its skinned bounds.

## The palette on the GPU

- **One storage buffer in the draw set.** Set 3 (ADR-0024), binding 1, beside the per-item matrices. Each draw item's record gains `paletteOffset`, in bones; the shader reads `bones[paletteOffset + jointIndex]`.
- **Ring-buffered per frame in flight** (ADR-0019). Each frame writes its own slot, which grows by doubling when a frame needs more. Every frame writes the palettes again: there is no cache across frames.
- **Deduplicated within a frame** by `(scene, skeleton entity, PoseGeneration)`. All meshes and views drawing one skeleton at one pose share one range. The key allows a second upload if the pose changes before a later submission, although ADR-0019 forbids writing scene state in `Render`. How many physical uploads this becomes is still not promised (ADR-0028).
- **Range validation.** Before a draw is recorded, `paletteOffset + boneCount` is checked against the slot's allocated capacity. A failure is an internal error that skips the draw; it is never an out-of-range read.
- **FenrirSkeleton stays GPU-free.** It provides:
  - **`PoseGeneration(entity)`:** a `u64` that finalisation advances when the finalised pose changes or the instance is rebuilt. It is drawn from a counter shared by the whole `SkeletonPoses` resource, so an instance recreated under a recycled entity id never repeats an earlier generation. Moving the skeleton entity's `Transform` does not advance it.
  - **The palette:** built lazily for the current generation and cached with the generation it was computed for. Reading it never changes the generation. Skeletons nothing skins never build one.
- *Amended by ADR-0041:* the renderer reads `PoseState`, `PoseGeneration`, the palette and model matrices through FenrirAssets' `SkinningSource`, which FenrirSkeleton registers, and never links FenrirSkeleton. With no source registered, skinned draws are skipped with one warning.

## Formats

- **Palette entries** are `struct FenrirBone { vec4 r0, r1, r2; }` in std430, 48 bytes. `r0`–`r2` are rows 0–2 of the column-vector affine `M_skin = M_model × B`; in glm terms `r_k = (m[0][k], m[1][k], m[2][k], m[3][k])`, with an implicit last row `(0, 0, 0, 1)`. C++ packs them from glm's column-major `mat4` on upload into one `GpuBone` struct, with static layout assertions. `Fenrir/Skinning.glsl` declares the buffer and applies the rows as `p' = vec3(dot(r0, p), dot(r1, p), dot(r2, p))` with `p.w = 1` (`0` for directions). Debug builds check the C++ and GLSL layouts against reflection (ADR-0018).
- **Vertex skin data** is four `u16` joint indices and four `unorm16` weights per vertex, at fixed locations after the standard attributes in `Fenrir/Vertex.glsl`. `MeshData` stores skin data in exactly this format, so CPU skinning, bounds and tests see what the shader sees. The skin section also records `boneCount` (the highest joint index + 1) and `expectedSignature` (below).
- **Quantisation**, done by FenrirAssets in the importer, `Add<Mesh>` and `Replace`, so every stored mesh satisfies it:
  - A vertex with positive total weight is normalised, then quantised to sum to exactly 65535. The rounding remainder is spread one unit at a time over the weights with the largest fractional parts, so no single weight underflows or overflows.
  - A vertex with zero total weight keeps all-zero weights, with a warning at import.
  - A joint index ≥ `boneCount`, or a negative or non-finite weight, fails the skin at import (ADR-0031's smallest unit) and is refused by `Add` and `Replace`.
- **No bone cap in the shader.** u16 indices allow 65,535 bones; the buffer and the range check are the only bounds.

## The vertex shader

`Fenrir/Skinning.glsl` provides `FenrirSkinPosition`, `FenrirSkinNormal` and `FenrirSkinTangent`, active under `SKINNED`. The built-in programs use them. They blend the four row-matrices by weight into one affine `M` with upper 3×3 `A`.

- **Zero weights.** If the weights sum to 0, `M` is the identity in skeleton bind space: the vertex rides rigidly with the owner at its bind position, `v_world = W(owner) × v`. The quantiser guarantees the sum is exactly 0 or 1.
- **Normals.** With `s = sign(det A)`, `n' = normalize(s × cof(A) × n)`. Because `cof(A) = det(A) × A⁻ᵀ`, this has the inverse-transpose's direction, under a negative determinant too. It is correct under non-uniform scale and needs no inverse.
- **Degenerate blends.** `A` is near-singular when `|det A| < 1e-6 × c³`, where `c` is its largest column length, or when `n'` has length under `1e-6` or is not finite. The normal then falls back to `normalize(A × n)`, and if that is also degenerate or not finite, to the bind normal `n`. No branch produces NaN.
- **Tangents.** `t' = A × t.xyz`, taken through the world 3×3 and Gram–Schmidt'd against the final world normal. If the result is near zero or parallel to the normal, it falls back to a unit vector perpendicular to the normal, built from the world axis least aligned with it. Handedness is `w' = t.w × s × sign(det W₃ₓ₃)`, and the bitangent is `cross(n, t) × w'`.
- **The world step is shared.** The world normal matrix and the world determinant sign come from set 3's per-item data, computed the same way for rigid and skinned items.
- **User shaders.** A user shader whose container has no `SKINNED` key, drawing a skinned item, draws with the fallback magenta material, logged once, never unskinned. More elaborate normal skinning (dual quaternions and the like) waits for a demonstrated need.

## Bounds

- **Per-bone boxes on `MeshData`.** FenrirAssets computes one box per bone in that bone's space (`B[i] × v`, over the vertices whose quantised weight for bone *i* is positive), plus one box for zero-weight vertices in skeleton bind space. They are rebuilt with every import, `Add` and `Replace`, so they follow the mesh's revision.
  - *Amended while building the animation spec's ticket 01 (2026-10-29):* a mesh holds no inverse binds, which are its skeleton's, so FenrirAssets keeps each bone's box in skeleton bind space (over `v`, not `B[i] × v`). `GetWorldBounds` moves each by the bone's palette matrix (`M_model[i] × B[i]`) instead of by `M_model[i]`, which bounds the same vertices and stays conservative.
- **`GetWorldBounds` for a skinned mesh** unions `M_model[i] × box_i` over the bones, plus the zero-weight box, then applies `W(owner)`. It is computed on call from FenrirSkeleton's current model matrices (ADR-0023's on-call rule) and returns `nullopt` when the skeleton is not `Ready`. It is conservative for linear blend skinning with non-negative normalised weights, not necessarily tight. The public API stays the same if better bounds replace it, and later culling uses the same boxes.

## Compatibility

- **`SkeletonData::bindSignature`** is a deterministic 64-bit FNV-1a hash of a canonical byte encoding, in this order, all little-endian:
  - the signature format version, a `u32`, starting at 1;
  - the bone count, a `u32`;
  - per bone, in index order: the parent index as an `i32`, the engine name as a `u32` byte length followed by its UTF-8 bytes, and the 16 inverse-bind floats in column-major order as IEEE-754 bit patterns, with `-0` canonicalised to `+0`. Non-finite values cannot occur, because ADR-0031's validation fails them.

  A hash that comes out as 0 is replaced by 1, because 0 is reserved for "unchecked". Changing the encoding bumps the version. It is a compatibility check, not proof against collisions.
- **`MeshData`'s `expectedSignature`** is set by the importer from the skeleton the mesh was baked against. Procedural meshes default to 0, unchecked, and may copy a skeleton's signature to opt in.
- **Checked when a pairing changes**, not on every draw. The renderer caches a verdict per mesh entity, keyed by mesh handle and revision and skeleton asset handle and revision. A pairing is compatible when:
  - the mesh's `boneCount` ≤ the skeleton's bone count, which is always checked;
  - the signatures are equal, unless `expectedSignature` is 0.

  The same rig exported twice with drifted binds does not match; that is intended, because the bind spaces differ.

## Tests

- **Null backend, through `RendererProbe`:**
  - A skinned item carries the skeleton's world matrix and a `paletteOffset`.
  - Two meshes on one skeleton share one palette range.
  - Each skip case logs once: no `SkinnedMesh`, a dead or `Unavailable` skeleton, a failed count or signature check, a shader without `SKINNED`.
  - Palette ranges stay within capacity.
  - `GetWorldBounds` follows a moved bone and contains CPU-skinned vertex positions.
  - Two skeletons with matching bone counts but different mappings are rejected.
- **GL read-back:**
  - A two-bone quad with one bone rotated 90°, checked by pixel probes.
  - A `GpuBone` holding translation, rotation and non-uniform scale, on which CPU and GPU transforms agree.
  - A non-uniformly scaled bone's normal, checked through `builtin:lit`.
  - Owners with non-identity transforms.
- **CPU unit tests:**
  - The bind-pose identity against the canonical equation.
  - `GpuBone` packing.
  - The quantiser, covering exact sums, remainder distribution, zero weights, and malformed indices and weights.
  - Signature determinism and canonical zero.
  - Cofactor normals against the inverse-transpose reference for well-conditioned matrices, negative determinants included.
  - Near-singular and non-finite fallbacks.
  - Tangent re-orthogonalisation and handedness under non-uniform scale and negative scale.
  - Rigid transforms giving the same normals and tangents through the skinned and rigid paths.

## Considered options

- **A skeleton field on `MeshRenderer`.** Rejected: every rigid mesh would carry an empty link, and the C# mirror would change shape.
- **Drawing skinned meshes without `SkinnedMesh` rigidly.** Rejected: bind-space vertices under the mesh entity's matrix render plausibly and wrongly.
- **A uniform block per skeleton.** Rejected: about 256 bones at most, and one bind per skeleton.
- **`mat4` palette entries.** Rejected: skinning matrices are affine, and three rows save a quarter of the bandwidth.
- **`u8` indices or `float32` weights.** Rejected: the first caps skeletons at 256 bones, the second doubles the size for no visible gain.
- **A bone-count check only.** Rejected: two skeletons with one count can map indices differently.
- **A structure-only signature** (count, parents, names). Rejected: re-exports with drifted binds would pass and skin wrongly.
- **`inverse(transpose(A))` for normals.** Rejected: slower, and with `s` applied the cofactor has the same direction.
- **Bind-pose bounds under the skeleton's world matrix.** Rejected: wrong as soon as the mesh animates.
- **Compute skinning now.** Deferred (below).

## Consequences

- ADR-0021's `MeshData` gains a skin section in the GPU format (`u16x4` indices, `unorm16x4` weights, `boneCount`, `expectedSignature`, per-bone boxes), and its "at most four influences, renormalised" becomes the exact quantisation above. `SkeletonData` (ADR-0031) gains `bindSignature`.
- ADR-0023's and ADR-0028's "skin references" are `SkinnedMesh`.
- ADR-0024's set 3 gains the palette buffer at binding 1, and its per-item record gains `paletteOffset` and the world determinant sign. `Fenrir/Skinning.glsl` joins the engine includes.
- FenrirSkeleton gains `PoseGeneration` and the lazily built palette (ticket 27's finalisation).
- Not yet specified: compute skinning into per-frame skinned vertex buffers, for when shadows, motion vectors, ray tracing or repeated passes draw a skinned mesh several times; it reads the same palette buffer. Also a bind-pose preview debug view.
- *Amended by ADR-0050:* compute skinning is specified: each skinned mesh is skinned once a frame into a per-frame vertex buffer, and `SKINNED` stays for user shaders and as the fallback.
