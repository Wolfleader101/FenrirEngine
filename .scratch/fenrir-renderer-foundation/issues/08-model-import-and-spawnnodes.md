# 08: Model import through cgltf and ufbx, and SpawnNodes

**What to build:** A glTF, GLB, FBX or OBJ file loads as a `Model` with its meshes, images and materials as sub-assets, and spawns as an entity tree (spec: "FenrirAssets" (`Model`, importers, static content only, `SpawnNodes`), "Materials and lights" (import mapping), "Build" (cgltf and ufbx rows), "Room left for animation", "Testing Decisions" seam 4; ADR-0021, ADR-0031's importer line, ADR-0028). `SpawnModel`, which adds `MeshRenderer`s, is ticket 13.

- **cgltf** and **ufbx** as pinned submodules under `packages/FenrirAssets/libs/`, static and `PRIVATE`, behind the private importer seam with stb_image; built under `FENRIR_ENABLE_ASSETS`. cgltf reads glTF and GLB; ufbx reads FBX and OBJ.
- **`Model`:** every source node (source name, source identity, rest TRS, parent index), pointing at its meshes and materials by sub-asset handle, with room for a node kind. Sub-assets load by `file#Label` like any path.
- **The importer** normalises quaternions, generates missing normals, and substitutes a 1×1 white image with a warning when a material's texture fails.
- **Material mapping:** an unlit file material goes to `builtin:unlit`, the rest to `builtin:lit`, with the standard parameter names (`baseColour`, `baseColourTexture`, `metallic`, `roughness`, `metallicRoughnessTexture`, `normalTexture`, `emissive`, `emissiveTexture`). Alpha mode becomes the blend mode, double-sidedness is kept, occlusion dropped. Every factor the file gives, or the format's own default, is written (glTF keeps metallic 1, roughness 1). Samplers from the file. The ticket decides how a `builtin:` shader is found when the renderer is not installed (tests register a stand-in container under the name).
- **Colour space by first use:** base colour and emissive sRGB, the rest linear; a conflicting later use warned.
- **Static only:** a mesh skinned in its file is left out with one diagnostic naming it; skeletons and clips are not imported.
- **`SpawnNodes(scene, AssetHandle<Model>, parent = {})`** spawns one entity per node with `Name`, `Transform` and its parent link, and returns `SpawnedNodes { Entity root; std::vector<Entity> nodes; }`, indexed by model node.
- Small fixtures (one scene in glTF, GLB, FBX and OBJ forms, and a skinned file) checked in under the package's tests.
- **`PublicHeaders.cpp`** extended for cgltf and ufbx.

**Blocked by:** 07 (Materials on the asset store: overrides over shader defaults).

**Touches:** `packages/FenrirAssets/`, `.gitmodules`

**Status:** ready-for-agent

- [ ] `FenrirAssetsTests` import cases: the glTF, GLB, FBX and OBJ fixtures give the same node tree, meshes and material mapping (blend mode, double-sided, factors, textures and samplers), within the formats' known differences.
- [ ] A missing texture is replaced by a white image with a warning; colour spaces follow first use, and a conflicting use warns.
- [ ] The skinned fixture loads with its skinned mesh left out and one diagnostic naming it.
- [ ] `file#Label` loads a sub-asset and returns the same handle as the model's own reference.
- [ ] `SpawnNodes` gives one entity per node with the right names, local transforms and parents, under a given parent, with `nodes` indexed by model node.
- [ ] No public header names cgltf or ufbx; Clang and MSVC x64 presets build with no warnings from their sources.
