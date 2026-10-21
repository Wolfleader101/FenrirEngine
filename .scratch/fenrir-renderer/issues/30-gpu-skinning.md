# How does the renderer skin meshes?

Type: grilling
Status: open
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
