# How are skeleton instances spawned, owned and read?

Type: grilling
Status: claimed
Blocked by: 24

## Question

ADR-0028 puts each skeleton's runtime pose in a `SkeletonPose` component in FenrirSkeleton, with bones as arrays rather than entities. What exactly is it, and how does it come to exist?

- `SkeletonPose`'s layout: the base and final local poses, model-space matrices and the palette as arrays; how C# sees variable-length arrays on a component (or whether they live elsewhere, keyed by entity); what `GetBoneWorldMatrix` and bone lookup by name look like.
- Which entity owns each skeleton: the node above the skeleton's root bone, the model root, or another rule; models with several meshes on one skeleton, and models with several skeletons.
- `SpawnNodes` in FenrirAssets' facade: which nodes count as bones and are left out, how the hierarchy is preserved, what it returns (root, node → entity map); how `SpawnModel` builds on it (ADR-0023 amended); a headless spawn with no renderer.
- `SkeletonData` as ADR-0021 has it: whether it already carries everything ADR-0028 needs (names, parents-first order, rest locals, inverse binds) and what the importer must guarantee, above all one bind space per skeleton (ADR-0028's *Coordinate spaces*): how assimp reports glTF and FBX mesh-node transforms and bind matrices, and whether the bake goes into the inverse binds or the vertices.
- A skeleton asset that is reloaded, replaced or goes stale under a live `SkeletonPose`; a pose whose bone count no longer matches.
- Testing with no renderer and no animation: posing by hand and reading model-space matrices.
