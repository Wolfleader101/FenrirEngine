# 01: FenrirAssets holds skeletons, clips, bone masks, skin sections and the skinning source interface

**What to build:** FenrirAssets can hold everything the animation packages read, built in code and checked by the Seam 4 tests, before any importer or runtime exists (spec: "FenrirAssets: skeletons, clips, masks and skins", "Packages, build and install" for `SkinningSource`; ADR-0031, ADR-0034, ADR-0035, ADR-0036, ADR-0041).

- **`SkeletonData`** (`AssetHandle<Skeleton>`): per-bone source name, unique engine name (`#2`, `#3`… suffixes on duplicates, shortened at a UTF-8 boundary then suffixed over 255 bytes, each with a diagnostic), model node index, parents-first parent index (−1 for a root, several roots allowed), rest local TRS and inverse bind; u16 indices in storage; the name map derived at build (exact by engine name, first match by source name); `bindSignature` as ADR-0035's versioned FNV-1a 64 with 0 reserved for "unchecked" and `-0` canonicalised to `+0`; the queries (bone count, parent, find by either name, `IsDescendant`, subtree iteration). `Add<SkeletonData>` validates as the importer will: non-finite TRS, zero or degenerate scales and binds, out-of-order or cyclic parents.
- **`MeshData`'s skin section**: `u16x4` joints and `unorm16x4` weights per vertex as the shader reads them, `boneCount`, `expectedSignature`, one box per bone in bone space plus the zero-weight box in bind space, rebuilt on `Add` and `Replace`. The quantiser normalises weights to sum exactly to 65535 giving the remainder a unit at a time to the largest fractional parts; all-zero weights stay zero with a warning; a joint index ≥ `boneCount`, or a negative or non-finite weight, is refused.
- **`ClipData`** (`AssetHandle<Clip>`): channels (target identity and engine name, path, `Step`/`Linear`/`CubicSpline`, authored times and values, cubic tangents), duration as the last key's time, events `{ time, name }` sorted by time with equal times in authored order, `ClipEventId` as the 32-bit FNV-1a of the name, and store edits (add event, remove by index) bumping the revision, with reads for duration, events and indices by name.
- **`BoneMaskData`** (`AssetHandle<BoneMask>`): ordered `{ boneName, weight, scope }` entries with `Bone` or `Subtree` scope, later entries overriding, unlisted bones 0; `Include` and `Clear` bump the revision.
- **`Model`**: each node record gains its kind (ordinary, synthetic owner, or bone *i* of skeleton *k*), and the model lists skeletons and clips as labelled sub-assets.
- **`SkinningSource`**: the abstract, stateless interface in FenrirAssets' public headers with `PoseState` (`Ready`, `Unavailable`, `Missing`), the per-scene-and-entity answers (skeleton handle, `PoseGeneration`, palette, model matrices) and `SpawnSkeletons(scene, model, spawned)`. Nothing implements it yet; `PublicHeaders.cpp` proves it compiles without FenrirSkeleton.
- **GLOSSARY.md** gains any of the spec's vocabulary these types introduce that it lacks (bone, clip, bone mask, bind signature, skinning source, skeleton bind space…).

No C#: the `Fenrir.Assets` handle markers come with ticket 13. The importer still leaves skinned meshes out (ticket 02 changes that for glTF, ticket 06 for FBX). No demo scene: this ticket's proof is the tests.

**Blocked by:** None (can start immediately)

**Touches:** `packages/FenrirAssets/include/FenrirAssets/`, `packages/FenrirAssets/src/`, `packages/FenrirAssets/tests/`, `GLOSSARY.md`

**Status:** resolved

- [x] Seam 4 tests cover `SkeletonData` fields, engine names (duplicates, over-long), `bindSignature` determinism and canonical zero, `IsDescendant` and subtrees, `Add<SkeletonData>` validation, the quantiser (exact sums, remainder distribution, zero weights, malformed input refused), per-bone boxes, and clip-event and mask edits bumping revisions.
- [x] `PublicHeaders.cpp` includes the `SkinningSource` header and the FenrirAssets tests build with no FenrirSkeleton target in the tree.
- [x] The existing FenrirAssets and renderer tests still pass: static models load as before.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-29. The per-bone boxes are kept in skeleton bind space, not bone space, because a mesh holds no inverse binds; ADR-0035 gains an amendment line, and ticket 05's `GetWorldBounds` moves each box by the bone's palette matrix. `MeshSkin::boneCount` left 0 is derived from the highest index; set, every index must be below it. The `Fenrir.Assets` handle markers stay for ticket 13; only the generated `AssetKind` enum gains the three kinds, as the API description check requires.
