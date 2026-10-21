---
status: accepted
---

# FenrirAssets owns plain-data assets behind generational handles

Meshes, textures, shaders, skeletons and clips come from files, and one model file (glTF, FBX) holds most of them together. FenrirRenderer needs meshes, textures and shaders; the animation package needs skeletons and clips; an app with no renderer (a server, a headless test) may still need skeletons and clips. So one package, **FenrirAssets**, loads and owns them all as plain CPU-side data, and each consumer turns what it needs into its own form.

## The decision

- **A dedicated package holding plain data.** FenrirAssets defines the asset types (`MeshData`, `ImageData`, `MaterialData`, `SkeletonData`, `ClipData`, `ShaderData`, `Model`) as structs of value types, plus their storage, handles, importers and path resolution. It has no GPU code and depends on neither FenrirRenderer nor animation; they depend on it. Physics never sees it: glue in the app builds a `MeshData` from physics's plain spans. `Assets::Install(App&, AssetsConfig)` returns the owner of the store, as `Physics::Install` does.
- **Typed generational handles, owned by the app or a scene.** `AssetHandle<T>` is a plain `{index, generation}`: trivially copyable, stored in components as plain data, passed to C# as a value, keeping nothing alive. Each load or `Add` names an owner, the app or a scene; an asset is freed when its last owner ends (a scene ending releases what it owned) or when `Unload` is called, which bumps the slot's generation. Loading a path already loaded returns the same handle and adds the owner. A stale or invalid handle in a component is skipped and logged once per handle.
- **Synchronous, main thread only.** `Load<T>(path, owner)` blocks and returns a result holding the handle or a diagnostic (path, reason, importer message). Because consumers already skip handles that do not resolve, an asynchronous `LoadAsync` can be added later without changing callers.
- **Models and labelled sub-assets.** `Load<Model>` returns the file's node tree (names, local transforms, parent indices) pointing at sub-asset handles (meshes, materials, skeleton) and listing its clips. Each sub-asset also loads alone by label (`hero.fbx#Run`), sharing the parsed file and its deduplication. Spawning a model as entities belongs to the ECS facade, not FenrirAssets.
- **Importers behind a private, compile-time seam.** *Amended by ADR-0031:* the model importer is cgltf for glTF and GLB and ufbx for FBX and OBJ, and assimp is dropped; skeletons are grouped by source joint identity and normalised into one bind space. As ADR-0010 does for physics: private model and image importer interfaces with one implementation each, assimp for models (its many formats) and stb_image for images, linked `PRIVATE`; no public header names either. The model importer normalises what assimp hands back: clip times in seconds, at most four joint influences per vertex (the strongest, renormalised), unit quaternions, missing normals generated, skeletons rebuilt parents-first from bone names. A cgltf path for glTF can replace assimp there without touching callers.
- **Procedural meshes share the handles.** `Add<Mesh>(MeshData, owner)` returns an ordinary handle with no path and no deduplication; `Replace(handle, MeshData)` swaps the data and bumps a revision number seen by every user of the handle.
- **GPU upload is the renderer's, as a separate stage.** FenrirRenderer keeps its GPU resources keyed by asset handle and revision, uploads in a prepare step on the GPU thread whatever a drawn component references that is new or changed, and frees the GPU side when the handle goes stale. FenrirAssets keeps the CPU copy (bounds, physics colliders, re-uploads).
- **Shaders are compiled shader containers** (ADR-0018). `ShaderData` holds a container's bytes, opaque to FenrirAssets; FenrirRenderer parses them. Its built-in containers stay embedded in its binary and are registered with `Add` under `builtin:` names at install.
- **One asset root.** Relative paths resolve against `AssetsConfig`'s root, by default `assets/` beside the executable (demo builds copy theirs there); absolute paths are taken as given. No virtual file system, mounts or packing.

## Considered options

- **Each package loads its own files.** Rejected: one model file feeds both renderer and animation, so either both parse it or one hands the other's data across.
- **FenrirAssets as storage only, types defined by renderer and animation.** Rejected: the one loader that reads a model file would then depend on both.
- **Ref-counted strong handles** (Bevy's `Handle`, Godot's `Ref`), freeing an asset when its last handle drops. Rejected: components stop being plain data, copies touch a count, C# fields must hold counts across the bridge, and lifetime becomes invisible (a forgotten handle leaks, an early drop reloads). Owners per load keep most of the convenience with none of that.
- **Asynchronous loading from the start.** Deferred: worth it for streaming, but the foundation's demos load at start-up.
- **Upload inside `Load`.** Rejected: FenrirAssets would call the renderer.
- **glTF only through cgltf.** Rejected for now in favour of assimp's breadth; the seam keeps it open.

## When to revisit

If assimp's skins or clips prove too loose for animation (add a cgltf path behind the seam), if load hitches show (add `LoadAsync`), or if keeping CPU copies costs too much memory (a per-asset GPU-only flag).
