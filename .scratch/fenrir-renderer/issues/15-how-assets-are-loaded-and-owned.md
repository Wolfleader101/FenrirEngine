# How are meshes, textures, shaders, skeletons and clips loaded and owned?

Type: grilling
Status: resolved
Blocked by: 06, 08

## Question

How does game code get a mesh, texture, shader, skeleton or animation clip, and who owns it?

- One asset package (FenrirAssets?) or each package loading its own? Which importer, behind which seam?
- How components name assets (typed handles), how assets are shared and unloaded, and how a mesh made in code (or from physics data) uses the same handles.
- Synchronous or asynchronous loading in the foundation; when the GPU upload happens.
- Which formats the foundation reads (glTF? OBJ? images?), and where paths are resolved from.

## Answer

ADR: `docs/adr/0021-fenrirassets-owns-plain-data-assets-behind-generational-handles.md`. Glossary: FenrirAssets, Asset, Asset handle, Asset owner, Asset root, Model, Sub-asset.

- **Home:** a new FenrirAssets package holds the plain-data asset types (`MeshData`, `ImageData`, `MaterialData`, `SkeletonData`, `ClipData`, `ShaderData`, `Model`), their storage and handles, the importers and path resolution, with no GPU code. FenrirRenderer and animation depend on it, not on each other; physics never sees it. `Assets::Install(App&, AssetsConfig)` returns the store's owner; FenrirAssets depends on FenrirScene for scene-owned assets.
- **Handles and lifetime:** `AssetHandle<T>` is a plain `{index, generation}`. Each load or `Add` is owned by the app or a scene; an asset is freed when its last owner ends or on `Unload`. The same path loads once and returns the same handle. A stale or invalid handle in a component is skipped and logged once.
- **Loading:** synchronous, main thread only. `Load` returns the handle or a diagnostic (path, reason, importer message); a missing label fails the same way. A material whose texture fails gets a 1×1 white texture and a warning; no magenta fallbacks.
- **Models:** `Load<Model>` gives the node tree (names, local transforms, parent indices) pointing at sub-asset handles, plus the file's clips; each sub-asset also loads alone by label (`hero.fbx#Run`). Spawning a model as entities is ticket 14's.
- **Importers:** behind a private, compile-time seam (as ADR-0010): assimp for models, for its breadth of formats, and stb_image for PNG, JPEG and HDR, both `PRIVATE`. The importer converts ticks to seconds, keeps the strongest four weights and renormalises, normalises quaternions, generates missing normals, and rebuilds skeletons parents-first from bone names.
- **Data shapes:** `MeshData` is optional attribute streams (positions, normals, tangents, UV0, UV1, colours, joints, weights), 32-bit indices and submeshes (index range plus material slot); `ImageData` is width, height, `RGBA8`/`RGBA16F`/`RGBA32F` pixels and a colour space set by use, with no mips (the renderer makes them); `MaterialData` is what the file says (base colour, metallic, roughness, normal, emissive, unlit, alpha mode), mapped to renderer materials by ticket 17; `SkeletonData` is joints parents-first (name, parent, bind-pose local TRS, inverse-bind matrix); `ClipData` is a duration and channels targeting joints by name, keyed in seconds.
- **Procedural meshes:** `Add<Mesh>(MeshData, owner)` gives an ordinary handle, no path, no deduplication; `Replace` swaps data and bumps a revision. Physics data reaches it through app glue.
- **GPU:** FenrirRenderer uploads in a prepare step on the GPU thread, keyed by handle and revision, and frees on a stale generation; FenrirAssets keeps the CPU copy.
- **Shaders:** `ShaderData` is a compiled shader container's bytes (ADR-0018), opaque to FenrirAssets and parsed by FenrirRenderer; built-in containers stay embedded and are registered under `builtin:` names at install.
- **Paths:** relative to `AssetsConfig`'s asset root, default `assets/` beside the executable, copied there by each demo's build; absolute paths as given; no virtual file system.
- **Later:** `LoadAsync`, a public loader registry, a cgltf path for glTF, KTX2 and compressed textures, a GPU-only flag (*Not yet specified*). No new tickets: ticket 21 takes assimp and stb_image into the build, ticket 17 maps `MaterialData`.
