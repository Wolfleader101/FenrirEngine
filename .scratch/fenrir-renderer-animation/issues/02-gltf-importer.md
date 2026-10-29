# 02: the glTF importer reads skins, skeletons and clips into one bind space

**What to build:** loading a glTF or GLB file with skinned meshes gives a `Model` whose skeletons, skinned meshes and clips are all there, in one bind space, with every failure confined to its smallest unit (spec: "Import"; ADR-0031, ADR-0034, ADR-0035). This ticket builds the importer's format-independent rules on the cgltf path; ticket 06 adds FBX on top of them. For glTF it replaces the foundation's static-only rule: the "skinned mesh left out" diagnostic goes from the cgltf importer, and the foundation's skinned-fixture test now expects a skinned mesh. The ufbx importer keeps its diagnostic until ticket 06.

- **Bones:** a node is a bone when it is a skin joint, on the path between a skeleton's joints and its root bone, or below a bone and animated by a clip while carrying no mesh, camera or light. Every other node, empty children of bones included, is an ordinary node. (The FBX `LimbNode` rule is ticket 06's.)
- **Skeletons:** skins grouped by source joint identity (skin index, then joint order, then parents-first), joints and vertex indices remapped to that order; disjoint skins are separate skeletons; skins sharing joints merge when their binds agree, or through one checked conversion baked into the later skin's vertices, else the later skin fails.
- **Owners:** the `skin.skeleton` node when it is not a joint, else the nearest non-bone ancestor of the group's root bones; several skeletons on one node each get a synthetic owner, an identity child named `<first root bone>.Skeleton`. `skin.skeleton` never enters skinning maths.
- **One bind space:** `v_world = W(owner) × M_model[i] × B[i] × v` (column vectors, column-major). Kept unchanged when the owner's rest world is the identity, else `B = IBM × W_o` with vertices through `W_o⁻¹`; a skinned mesh node's own transform ignored; a missing inverse-bind accessor is identity. Baking moves positions by the matrix, normals by its inverse transpose (renormalised), tangents by its 3×3 with handedness flipped on a negative determinant, and remaps joints in the same pass. The baking and validation code is shared, so ticket 06 reuses it.
- **Clips:** keys raw, with any rotation key whose dot with the previous is negative flipped along with its tangents; channels on ordinary nodes kept but marked unplayable.
- **Failure:** `Succeeded`, `Partial` (with diagnostics) or `Failed`; each error fails a skeleton with its skins and meshes, or an incompatible skin with its meshes; no skin is ever imported unskinned; an ordinary node with a degenerate transform is warned and kept. Validation covers the spec's list, plus non-invertible owner, bind and conversion matrices. `expectedSignature` is set from the skeleton each mesh was baked against.
- **Spawning nodes:** `SpawnNodes` leaves bone nodes out of `SpawnedNodes::nodes` (null entries) and spawns synthetic owners like ordinary nodes. Until ticket 05, the renderer's `SpawnModel` skips skinned meshes with one warning rather than drawing them unskinned.
- **Fixtures:** the glTF half of ADR-0031's mandatory list: multiple and unrelated armatures, shared-joint skins, a joint `skin.skeleton`, unweighted leaves, intermediate nodes, non-bone attachments, shared skeletons, an incompatible skin and duplicate names, a non-uniformly scaled and negative-determinant owner, and a cubic clip with a hemisphere flip. Small hand-written or generated files are preferred; a third-party file records its licence beside it.

No demo scene: the proof is the Seam 4 importer tests.

**Blocked by:** 01

**Touches:** `packages/FenrirAssets/src/Importers/`, `packages/FenrirAssets/src/ModelLoading.cpp`, `packages/FenrirAssets/src/SpawnNodes.cpp`, `packages/FenrirAssets/include/FenrirAssets/Model.hpp`, `packages/FenrirAssets/include/FenrirAssets/SpawnNodes.hpp`, `packages/FenrirAssets/tests/`, `packages/FenrirRenderer/src/Facade/SpawnModel.cpp`, `packages/FenrirRenderer/Tests/SpawnModel.cpp`

**Status:** ready-for-agent

- [ ] Every glTF fixture reconstructs its bind pose: for each skinned vertex, `M_model_bind × B` is the identity against the bind pose, within tolerance.
- [ ] The numerical owner test matches glTF's formula for a non-identity owner rest world; baked normals and tangents are checked under non-uniform scale and a negative determinant.
- [ ] Each glTF fixture on ADR-0031's list loads with the expected skeleton count, owners (synthetic ones named `<first root bone>.Skeleton`), bone kinds and attachment nodes; the incompatible skin and duplicate-name cases give `Partial` with the named units failed and nothing imported unskinned.
- [ ] The hemisphere-flip case checks key and tangent values; the foundation's "skinned mesh left out" test is replaced by one expecting a skinned mesh with a skin section and `expectedSignature`; FBX files load as before.
- [ ] `SpawnNodes` leaves bones out and spawns synthetic owners; the renderer's `SpawnModel` test shows skinned meshes skipped with one warning and rigid meshes unchanged.
- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
