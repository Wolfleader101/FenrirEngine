---
status: accepted
---

# FenrirRenderer reaches skeleton poses through a skinning source that FenrirSkeleton registers

ADR-0028 made FenrirRenderer depend on FenrirSkeleton, never on FenrirAnimation. ADR-0035 has the renderer read FenrirSkeleton's `PoseState`, `PoseGeneration`, lazily built palette and model matrices, and ADR-0031 has the renderer's `SpawnModel` call `SpawnSkeletons`. Writing the animation spec, the user asked for runtime modularity without more build options. FenrirSkeleton and FenrirAnimation are built whenever FenrirAssets is. Still, a renderer must not need FenrirSkeleton installed, or linked, to draw rigid meshes. Without skeletal services, rigid rendering works and skinned draws are skipped with diagnostics. The renderer therefore reaches live skeleton poses through an interface that FenrirSkeleton implements and registers at install. That interface is declared in FenrirAssets, the package both already depend on.

## The skinning source

- **`SkinningSource` is an abstract interface in FenrirAssets' public headers**, beside `SkeletonData` and `MeshData`'s skin section. It holds no state. It answers per scene and skeleton entity:
  - the instance's `PoseState` (`Ready`, `Unavailable`, `Missing`; ADR-0031). The enum is declared here and used by FenrirSkeleton;
  - the instance's `AssetHandle<Skeleton>`;
  - its `PoseGeneration` (ADR-0035);
  - its skinning palette, built lazily for the current generation, main thread only;
  - its model-space matrices, for skinned bounds.

  Spans follow ADR-0031's validity rule. A call for an entity that is not a `Ready` instance returns its state and empty results.
- **It also spawns skeletons:** `SpawnSkeletons(scene, AssetHandle<Model>, const SpawnedNodes&)`, which is FenrirSkeleton's (ADR-0031), so the renderer's `SpawnModel` needs no FenrirSkeleton symbol.
- **FenrirSkeleton implements it.** `Skeletons::Install` registers its implementation as an app resource, and the owner's teardown removes it. One source per app: a second registration is logged and ignored.
- **The renderer looks it up each `Render`** and in each call that needs it. Install order between the renderer and FenrirSkeleton does not matter.

## Without a skinning source

- Rigid `MeshRenderer`s draw as usual.
- A skinned item (skin data and a `SkinnedMesh`) is skipped, with one warning naming the missing skeleton services. ADR-0035's other skip cases are unchanged.
- `GetWorldBounds` returns `nullopt` for a skinned mesh.
- `SpawnModel` spawns the nodes and adds `MeshRenderer`s and `SkinnedMesh`es pointing at each skeleton's owner, but no `SkeletonPose`, logged once. Game code may add `SkeletonPose`s later.

## Packages and install

- **Dependencies:** FenrirSkeleton on FenrirAssets (and the ECS packages); FenrirAnimation on FenrirSkeleton; FenrirRenderer on FenrirAssets, with no link to FenrirSkeleton or FenrirAnimation. Physics depends on none of them, nor they on it.
- **Built with FenrirAssets.** Both packages are built under `FENRIR_ENABLE_ASSETS`, as FenrirDraw2D is, with no option of their own; they link no library.
- **Install fails clearly.** `Skeletons::Install` fails without FenrirAssets installed, and `Animation::Install` fails without a valid FenrirSkeleton owner. Each logs one diagnostic and returns an owner whose `IsValid()` is `false`, the foundation's install failure shape.

## Considered options

- **The renderer linking FenrirSkeleton (ADR-0028).** Simple, but a hard package dependency, and the renderer would fail to build or run without it. Rejected at the user's request.
- **The interface in FenrirRenderer, implemented by FenrirSkeleton.** FenrirSkeleton would then depend on the renderer, which a server must not need. Rejected.
- **The interface in a new header-only package.** Clean, but a package for one header, when FenrirAssets already holds the data the interface describes. Rejected.
- **App glue joining the two.** Every app would write the same few lines, and forgetting them would skip every skinned draw. Rejected; registration happens at install.
- **A `FENRIR_ENABLE_ANIMATION` option.** Not needed yet, since neither package links a library.

## Consequences

- *Amends ADR-0028:* FenrirRenderer depends on FenrirAssets for skinning data and reaches FenrirSkeleton only through the skinning source, never by linking it.
- *Amends ADR-0031:* the renderer's `SpawnModel` calls `SpawnSkeletons` through the skinning source, and skips it when there is none.
- *Amends ADR-0035:* the renderer reads `PoseState`, `PoseGeneration`, the palette and model matrices through the skinning source, and skips skinned draws when no source is registered.
- FenrirSkeleton's own API is unchanged: `PoseGeneration`, the palette and the reads stay on its facade for every other caller.
