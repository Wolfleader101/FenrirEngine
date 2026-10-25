# 06: Port `Fenrir.Renderer` onto the generated pipeline

**What to build:** The renderer's components, tagged unions, resources, shared structs, native declarations and registration are generated; `Renderer` is a hand-written facade over typed natives (spec: "Each port"; ADR-0043, ADR-0044).

- **Selection** (`packages/FenrirRenderer/api/FenrirRenderer.api.toml`, header `Components.hpp`): `MeshRenderer`, `Camera`, `DirectionalLight` as `component`; `AmbientLight` and `Sky` as `scene-resource`; `RendererStats` as `shared-struct`. `Projection`, `RenderTarget`, `CameraBackground` and `Sky` are tagged unions in describe-api's shape already (one enum tag, one anonymous union, default kind tag 0); their kinds (`Perspective`, `Orthographic`, `WindowTarget`, `ImageTarget`, `SceneBackground`, `ColourBackground`, `ColourSky`, `CubemapSky`), `Viewport` and the tag enums are described through them. `SceneBackground` is an empty kind, mirrored as the generator writes an empty struct.
- **Binding** (`Scripting/api/FenrirRenderer.binding.toml`): `[uses]` the math, input (`WindowId` in `WindowTarget`) and assets (`AssetHandle<T>`: its `[bound-types]` entries come through assets' binding, or are repeated here if `[uses]` does not carry bound types; the ticket settles which with ticket 02's result) bindings; `[names] "Fenrir::Projection" = "CameraProjection"`; `[nouns]` `"projection"`, `"render target"`, `"background"`, `"sky"`; `[natives] sources = ["../src/RendererScripting.cpp"]`, class `RendererNative`; `[registration]` (components, the two resources, the shared struct); `[tests]`.
- **Natives:** `CameraAt` takes `WindowId`; `SpawnModel` takes `AssetHandle<Model>` packed as `std::uint64_t` (as assets' natives do) instead of two `std::uint32_t`s, with the facade packing; `GetWorldBounds` writes a `Math::AABB`, now `Bounds` through math's binding; outputs marked `@param[out]`; `Entity` parameters as the wrapper they are. The warn-once record moves to a `RendererScripting` state class.
- **Facade:** `Renderer` rewritten over `RendererNative`: `SpawnModel` overloads and the handle packing kept; everything else as it was.
- **Deleted:** hand-written `Components.cs`, `Resources.cs` (behaviour stays as partial parts), `RendererNative`'s declarations, `RendererMappings.hpp`'s mappings, the `extension.components`/`.resources`/`.functions` lists.

**Blocked by:** 04 (Port `Fenrir.Input` and `Fenrir.Window`); 05 (Port `Fenrir.Assets`)

**Touches:** `packages/FenrirRenderer/api/`, `packages/FenrirRenderer/Managed/`, `packages/FenrirRenderer/Scripting/`

**Status:** ready-for-agent

- [ ] `fenrir-describe-api` and `fenrir-generate-bindings` leave the tree clean; no `IRIS_FIELD`, `[NativeFunction]` or `NativeFunction::Make` remains under the package.
- [ ] The existing renderer extension tests pass unchanged in behaviour (drawing, picking, `SpawnModel`, shapes, no-op rules); the generated defaults test proves `new Camera()`, `new Sky()` and `new AmbientLight()` equal their C++ defaults; the natives test runs.
- [ ] `camera.Projection = new Orthographic { Height = 5 }`, `Resources.Get<Sky>()`, `Renderer.SpawnModel(model)` and `Renderer.GetWorldBounds(entity, out Bounds b)` read exactly as before from a script.
