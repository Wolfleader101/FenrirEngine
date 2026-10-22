# 24: The Fenrir.Assets scripting extension

**What to build:** C# loads assets, builds meshes, images and render targets, and creates, clones, edits and reads materials by name exactly as C++ does (spec: "Scripting: the extensions" (`Fenrir.Assets`, no-op rules), "FenrirAssets" (material edits), "Testing Decisions" seam 7 (material reads matching C++); ADR-0029, ADR-0021, ticket 41).

- **Layout:** contract in `packages/FenrirAssets/Managed/Fenrir.Assets/`, native functions in a static `AssetsScripting` library; builds with `FENRIR_ENABLE_SCRIPTING` and `FENRIR_ENABLE_ASSETS`; `Assets::ScriptingExtension()`.
- **`AssetHandle<T>`:** blittable `{ Index, Generation }` with a phantom `T` (`Mesh`, `Material`, `Image`, `Model`, `Shader`).
- **`Assets`:** `Load<T>(path, owner = AssetOwner.Scene)` taking a path, `file#Label` or a `builtin:` name (failure returns an invalid handle and logs); `Unload`; `IsValid`; `AddMesh(positions, normals, uvs, indices, owner)` and `ReplaceMesh` from spans, computing bounds; `AddRenderTarget(width, height, owner)` and `ResizeRenderTarget`; `AddImage(width, height, rgba8, owner)` and `ReplaceImage`.
- **`Materials`:** `Create`, `Clone`; `SetFloat`, `SetVector2/3/4`, `SetColour`, `SetTexture`, `SetBlendMode`, `SetAlphaCutoff`, `SetDoubleSided`; `bool TryGet…(…, out v)` for the effective value, `bool TryGetOverride…(…, out v)`, `IsSet`, `Reset`. **`Shaders`:** `GetParameterCount(shader)` and `GetParameter(shader, index, out ShaderParameter, Span<char> name)`.
- **No-op rules** as the other extensions: package absent or stale handle → nothing, `false`, zero or an invalid handle; one debug warning per call site; nothing throws.

**Blocked by:** 08 (Model import through cgltf and ufbx, and SpawnNodes); 21 (FenrirScripting: native resources and the change buffer on the call context); 22 (The Fenrir.Math shared assembly).

**Touches:** `packages/FenrirAssets/Managed/`, `packages/FenrirAssets/Scripting/`, `CMakeLists.txt`, `FenrirScripting.sln`

**Status:** ready-for-agent

- [ ] Extension tests: a C# system loads a mesh by path and by `file#Label`, adds a procedural mesh with computed bounds, and replaces it (revision bumped, seen from C++).
- [ ] Material edits from C# give the same `TryGet`, `TryGetOverride`, `IsSet` and `Reset` results as the same edits from C++ on a fixture shader; `Clone` copies overrides only.
- [ ] `Shaders.GetParameter` lists every parameter with its type, default and name.
- [ ] `AddRenderTarget` makes a pixel-less RGBA8 sRGB image that `ResizeRenderTarget` resizes.
- [ ] A missing package, a stale handle and a failed load each no-op with the documented return; `AssetHandle<T>` matches the C++ layout.
