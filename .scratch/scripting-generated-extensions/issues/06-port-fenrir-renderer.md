# 06: Port `Fenrir.Renderer` onto the generated pipeline

**What to build:** The renderer's components, tagged unions, resources, shared structs, native declarations and registration are generated; `Renderer` is a hand-written facade over typed natives (spec: "Each port"; ADR-0043, ADR-0044).

- **Selection** (`packages/FenrirRenderer/api/FenrirRenderer.api.toml`, header `Components.hpp`): `MeshRenderer`, `Camera`, `DirectionalLight` as `component`; `AmbientLight` and `Sky` as `scene-resource`; `RendererStats` as `shared-struct`. `Projection`, `RenderTarget`, `CameraBackground` and `Sky` are tagged unions in describe-api's shape already (one enum tag, one anonymous union, default kind tag 0); their kinds (`Perspective`, `Orthographic`, `WindowTarget`, `ImageTarget`, `SceneBackground`, `ColourBackground`, `ColourSky`, `CubemapSky`), `Viewport` and the tag enums are described through them. `SceneBackground` is an empty kind, mirrored as the generator writes an empty struct.
- **Binding** (`Scripting/api/FenrirRenderer.binding.toml`): `[uses]` the math, input (`WindowId` in `WindowTarget`) and assets (`AssetHandle<T>`: its `[bound-types]` entries come through assets' binding, or are repeated here if `[uses]` does not carry bound types; the ticket settles which with ticket 02's result) bindings; `[names] "Fenrir::Projection" = "CameraProjection"`; `[nouns]` `"projection"`, `"render target"`, `"background"`, `"sky"`; `[natives] sources = ["../src/RendererScripting.cpp"]`, class `RendererNative`; `[registration]` (components, the two resources, the shared struct); `[tests]`.
- **Natives:** `CameraAt` takes `WindowId`; `SpawnModel` takes `AssetHandle<Model>` packed as `std::uint64_t` (as assets' natives do) instead of two `std::uint32_t`s, with the facade packing; `GetWorldBounds` writes a `Math::AABB`, now `Bounds` through math's binding; outputs marked `@param[out]`; `Entity` parameters as the wrapper they are. The warn-once record moves to a `RendererScripting` state class.
- **Facade:** `Renderer` rewritten over `RendererNative`: `SpawnModel` overloads and the handle packing kept; everything else as it was.
- **Deleted:** hand-written `Components.cs`, `Resources.cs` (behaviour stays as partial parts), `RendererNative`'s declarations, `RendererMappings.hpp`'s mappings, the `extension.components`/`.resources`/`.functions` lists.

**Blocked by:** 04 (Port `Fenrir.Input` and `Fenrir.Window`); 05 (Port `Fenrir.Assets`)

**Touches:** `packages/FenrirRenderer/api/`, `packages/FenrirRenderer/Managed/`, `packages/FenrirRenderer/Scripting/`

**Status:** resolved

- [x] `fenrir-describe-api` and `fenrir-generate-bindings` leave the tree clean; no `IRIS_FIELD`, `[NativeFunction]` or `NativeFunction::Make` remains under the package.
- [x] The existing renderer extension tests pass unchanged in behaviour (drawing, picking, `SpawnModel`, shapes, no-op rules); the generated defaults test proves `new Camera()`, `new Sky()` and `new AmbientLight()` equal their C++ defaults; the natives test runs.
- [x] `camera.Projection = new Orthographic { Height = 5 }`, `Resources.Get<Sky>()`, `Renderer.SpawnModel(model)` and `Renderer.GetWorldBounds(entity, out Bounds b)` read exactly as before from a script.

## Comments

**Asset handles are repeated in `[bound-types]` (settled).** `uses` carries a binding's mirrors, never its bound types
(ticket 02), so the renderer's binding names `AssetHandle<Mesh>`, `<Material>` and `<Image>` itself, as assets' does. The
three are selected as `bound-type` in FenrirRenderer's description, and their hand-written mappings come from
`FenrirAssetsScripting/AssetMappings.hpp`, which this binding's `[cpp] includes` names and assets' generated mappings
include.

**`Ray` and `AABB` are selected in the renderer's description, with the math description's role** (as debug draw's
does). The natives fill them through pointers, so the natives binding needs them described here; they stay the math
binding's `Ray` and `Bounds`, and this binding neither mirrors nor registers them. `Vec2` and `Vec3` are bound types for
the same reason.

**Two defaults moved into literals** (`FenrirRenderer/include/FenrirRenderer/Components.hpp`, outside the Touches line).
`ColourBackground::colour = Colour::Black` and `DirectionalLight::colour = Colour::White` named static members, which
describe-api does not record. They are now `{0, 0, 0, 1}` and `{1, 1, 1, 1}`, the same values, with the named colours in
their docs.

**Each native writes its output's default first.** Before, `ScreenPointToRay`, `WorldToScreenPoint` and
`GetWorldBounds` left their output untouched when they did nothing. The generated natives test calls a native with
nothing to act on and requires each output to hold its default, as physics's natives do. C# was unaffected, since an
`out` argument is set to default before the call anyway.

**Entities cross as an alias, as physics's do.** `Fenrir::RendererScript::EntityId` (a `std::uint32_t`, in
`src/RendererScriptingState.hpp`) is `[natives] types`-mapped to `Entity`. Iris.Bindings maps an alias for parameters
only, so `CameraAt` and `SpawnModel` return `uint` from `RendererNative`, and the facade wraps it (`new Entity(...)`).
Their `@notrunning Fenrir::Entity::Null` keeps C#'s `Entity.None` when no Scripting runs.

**`SpawnModel` takes the packed handle** (`std::uint64_t`, generation high, index low) as assets' natives do. The
facade packs it itself, since `AssetHandle<T>.Bits` is internal to `Fenrir.Assets`.

**The empty `SceneBackground` is mirrored as the empty struct the generator writes.** The old `SceneBackgroundLayout`
stand-in, whose comment said Iris cannot map an empty struct, is gone with the rest of `RendererMappings.hpp`: the
generated mapping has no fields, and the fixture load and the defaults test (`Camera` holds it in its background) pass.

**Deleted:** `Components.cs` and `Resources.cs` (the mirrors had no behaviour of their own; the SDK's tagged-union
generator writes the unions' members), `RendererStats` from `Renderer.cs`, `RendererNative`'s declarations,
`RendererMappings.hpp` (its one includer, the `Shapes.cpp` test, includes the generated mappings), and the
`extension.functions`, `.components` and `.resources` lists. The warn-once record moved to the `RendererScripting` state.

**Outside the Touches line**, besides `Components.hpp`: the `fenrir_describe_api` line in `FenrirRenderer/CMakeLists.txt`,
where the package describes itself. The spec-closing step, which 07 handed to this ticket, is the commit after this
one: the `GLOSSARY.md` note, the root and FenrirScripting READMEs' "still written by hand" lines, and deleting this
directory.
