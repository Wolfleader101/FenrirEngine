# How do engines import and own meshes, textures, shaders, skeletons and clips?

Type: research
Status: resolved
Blocked by: none

## Question

How should FenrirRenderer and the animation package get meshes, textures, shaders, skeletons and animation clips, and who should own them once loaded?

- **Importers:** assimp, cgltf, fastgltf and ufbx (and stb_image, KTX or Basis for textures): formats, skeleton and clip support, speed, size, licence, build cost, and how each handles skins, bind poses, node hierarchies and scale.
- **glTF as the interchange format:** what glTF 2.0 carries for skins, joints, inverse bind matrices, animation channels, morph targets and PBR materials; what is commonly missing or wrong in exported files.
- **Ownership:** how engines name loaded assets (typed handles, ref counting, strong and weak handles, generation counters), unload them, and share one mesh between many entities; whether loading is synchronous or asynchronous; whether GPU upload is separate from loading.
- **Where assets live:** a dedicated asset package (Bevy's `AssetServer`, Godot's `ResourceLoader`) against each consumer loading its own; how procedurally made meshes (from code, or from physics data) fit the same handles.

Write findings to a Markdown file on the `research/asset-import` branch (`.scratch/fenrir-renderer/research/asset-import.md`), citing each claim's source and pinning the versions read, and link it here.

## Answer

Findings: `.scratch/fenrir-renderer/research/asset-import.md` on branch `research/asset-import` (commit `12cabfe`), against assimp 6.0.5, cgltf 1.15, fastgltf `cd30d76`, ufbx 0.23.1, stb_image 2.30, KTX-Software `2e690dd`, the glTF spec `7cee0fc`, Bevy 0.20-dev `2614884` and Godot 4.8-beta `6790667`, each claim cited; speed figures are the libraries' own.

- **assimp:** 40+ formats into its own converted scene (the old DemoApp's importer); every importer built unless turned off, many bundled libraries; bones on each mesh matched to nodes by name; clips in ticks (ticks per second sometimes 0); only four glTF extensions.
- **cgltf** (C, one header, MIT) and **fastgltf** (C++20, MIT) return glTF close to as stored, so skinning, weight and quaternion rules are the engine's. fastgltf benchmarks 5–7× faster, but fetches simdjson and simdutf at build time by default, and `main` runs ahead of v0.9.1.
- **ufbx** (C, MIT or public domain, fuzzed): FBX and OBJ, with axis, unit, geometry-transform and scale-inheritance quirks handled by load options; inverse-bind matrices per bone; can bake layered curves to linear keys.
- **Textures:** stb_image decodes PNG and JPEG (glTF's core formats), no mips or compression; libktx adds compressed GPU textures and `KHR_texture_basisu`, larger, mixed licences, its own GL upload (would sit behind the graphics seam).
- **glTF 2.0:** metres, Y-up, right-handed; skins as joint lists with optional inverse-bind matrices; 4 weights per joint set (8 influences need two sets); clips as channels on node transforms or morph weights, in seconds; morph targets; metallic-roughness PBR. Skinned meshes ignore their own node's transform, and clips are not bound to a skeleton.
- **Common export faults:** unnormalised weights (Godot renormalises), transforms on skinned nodes or parents, non-unit quaternions, joints without a common root or with non-joint nodes between, missing tangents, more than four influences.
- **Bevy:** `Assets<T>` per type, entities hold a `Handle` (strong, ref-counted, unloads on last drop; or a weak generational `AssetId`); async loading deduplicated by path with a polled load state; GPU upload a later step, CPU copy optionally freed.
- **Godot:** ref-counted resources cached by path, blocking or threaded loads; GPU objects are server-owned ids with a validation counter; meshes uploaded when a surface is added; source files imported in the editor, imported resources loaded at runtime.
- **Skeletons and clips:** Bevy makes each joint an entity with inverse-bind matrices as a shared asset, clips targeting bones by a hash of their name path (any matching armature plays them); Godot splits `Skeleton3D` (node) from `Skin` (bind poses).
- **Options:** a dedicated asset package or each package loading its own; ref-counted handles or generational ids with explicit unloading; GPU upload as its own stage or at creation.
