# FenrirRenderer

Draws the active scene's meshes through its cameras, every frame, over a private graphics device with an OpenGL 4.6 backend and a null backend (ADR-0016, ADR-0019, ADR-0022, ADR-0023, ADR-0024, ADR-0026). Game code draws the ECS way: a `MeshRenderer` on an entity, a `Camera` on another, a `DirectionalLight` and the `AmbientLight` scene resource. No public header names GL, SDL or the shader compiler's libraries. Built with `FENRIR_ENABLE_RENDERER` (on by default).

```cpp
#include "FenrirAssets/Assets.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirWindow/Window.hpp"

Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());
Fenrir::Window window = Fenrir::Window::Install(app);       // first: the renderer asks it for the window's surface
Fenrir::Assets assets = Fenrir::Assets::Install(app);
Fenrir::Renderer renderer = Fenrir::Renderer::Install(app); // OpenGL by default; check renderer.IsValid()

Fenrir::AssetStore& store = assets.Store();
auto mesh = store.Add<Fenrir::Mesh>(MakeCube(), Fenrir::AssetOwner::App());
auto lit = store.Load<Fenrir::Shader>("builtin:lit", Fenrir::AssetOwner::App()).handle;
auto material = store.CreateMaterial(lit, Fenrir::AssetOwner::App());
store.SetColour(material, "baseColour", Fenrir::Colour::FromSrgb(0xC08040FF));

Fenrir::EntityList& entities = app.GetActiveScene().GetEntityList();
entities.CreateEntity().AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material});
Fenrir::Entity camera = entities.CreateEntity();
camera.GetComponent<Fenrir::Transform>().pos = {0.0f, 0.0f, 5.0f}; // looking along -Z
camera.AddComponent<Fenrir::Camera>();
entities.CreateEntity().AddComponent<Fenrir::DirectionalLight>();     // shining along its -Z
app.Run();
```

## Install and order

- `Renderer::Install(App&, RendererConfig)` returns the move-only `Renderer` owner of the renderer core and the graphics device, and adds the sequential `Render` system.
- **Install order:** FenrirWindow (for any backend but `Null`), then FenrirAssets, then the Renderer, all before the first frame. `Install` requests the main window's graphics surface (an OpenGL 4.6 core context, a debug one with `debugValidation`, and an sRGB backbuffer with no depth, stencil or MSAA), so the window is made then. It registers the built-in shaders in the asset store: `builtin:unlit`, `builtin:lit`, `builtin:fallback`, `builtin:background` and `builtin:output`.
- **Teardown in reverse:** destroy the `Renderer` before the `Window`, the `Assets` and the `App`; declaring the owners in install order does this. It waits for the GPU, frees every GPU resource and releases the window's surface.
- **Failure:** without an `AssetStore`, without a valid `Window` (any backend but `Null`), or when the backend cannot start (no OpenGL 4.6 core context, with SDL's message), `Install` logs one error and returns an owner whose `IsValid()` is false. It adds no systems, and its calls do nothing. It never falls back to the null backend.
- The `Null` backend needs no window or GPU: it keeps every book a backend keeps and draws nothing, for tests and headless apps. With a window it follows the window's size, else a fixed 1280 x 720.

## RendererConfig

`backend` (`OpenGL`, or `Null`); `vsync` (`Off`, `On` default, `Adaptive`; changeable with `Renderer::SetVsync`, which returns the mode in effect, an unsupported one falling back to `On`, logged); `msaa` (samples for the scene targets, default 1; kept, drawn with from renderer ticket 14); `debugValidation` (on in Debug builds: GL's debug messages reach the app's logger); `framesInFlight` (2). There are no tuning settings and no native escape hatch.

## Main-thread rule and the Render phase

- Everything the renderer does runs on the main thread, where the GL context is current: its one system is sequential, in the `Render` phase. Call `Renderer`'s functions from the main thread too, outside parallel systems.
- **`Render`-phase rule:** the renderer reads the scene in `Render`, after `PostUpdate`, so it sees the poses interpolated in `AfterTicks` and everything C++ and C# wrote in `Update` and `PostUpdate`, whatever order packages were installed in. Writing scene state in `Render` is unsupported.

## Each frame

All in `Render`, on the main thread (ADR-0019):

1. Read the main window's pixel size. A minimised or zero-sized window builds, submits and presents nothing, but the frame index still advances.
2. Read the active scene: every visible `MeshRenderer` (with `GetWorldMatrix` once each), every enabled `Camera`, the first `DirectionalLight` (more are warned about once) and the `AmbientLight`. Inactive scenes are never read, and nothing is kept between frames.
3. Prepare: upload each mesh and material that is new or has a new revision, keyed by asset handle and revision, and free the GPU side of each handle that went stale. Pipelines are made the first time a draw needs them and kept while their shader lives.
4. Build the render graph: per camera `Opaque` (one draw per item, sorted by pipeline, material and mesh; reversed-Z depth cleared to 0) → `Background` (a dark grey at depth 0 until the sky arrives) → `Output` (the linear RGBA16F `SceneColor` clamped and encoded into the sRGB backbuffer). With no camera the window is cleared to black, logged once.
5. Record each pass and submit it; then present.

## Components and resources

- `MeshRenderer { mesh, material, visible = true }`: one mesh, one material, one draw. `visible` hides only its own entity. An empty or stale mesh is skipped (a stale one logged once per handle); an empty or stale material draws with the fallback material, flat magenta and unlit, logged once per handle. A missing `Transform` is the identity.
- `Camera { Perspective projection { verticalFov = 60 degrees in radians, near = 0.1 }, enabled = true }`: a view from its entity along local -Z, +Y up, into the whole main window, with an infinite far plane and reversed-Z depth; the aspect comes from the window's pixel size. Renderer ticket 14 adds orthographic projections, `order`, viewports, image targets and the rest.
- `DirectionalLight { colour, intensity }` shines along its entity's world -Z. `AmbientLight { colour, intensity }` is a scene resource; a scene without one is lit by a dim grey. Intensities are plain multipliers.

## Materials and built-in shaders

Materials are FenrirAssets' `MaterialData`, edited through the asset store (ADR-0024). The renderer lays a material's set 2 out from its shader container's reflection: the `MaterialParams` block is the shader's default block image with the material's overrides written over it, and each texture binds the renderer's default 1x1 `white`, `black` or `normal` texture, as its parameter default says (material textures are bound from renderer ticket 13). Sets 0 (frame), 1 (view) and 3 (draw) are declared in C++ as `Fenrir/Frame.glsl`, `View.glsl` and `Draw.glsl` declare them, and in debug builds a shader whose reflection disagrees fails its pipeline with a diagnostic and draws as the fallback material. A vertex stream a shader reads but the mesh lacks binds a constant (normal +Y, UV 0, colour white), logged once per mesh.

- `builtin:unlit`: `baseColour` (white) times the vertex colour.
- `builtin:lit`: metallic-roughness GGX, one directional light and constant ambient, written as linear HDR, with `baseColour` (white), `emissive` (opaque black), `metallic` (0) and `roughness` (0.5). Its BRDF is scaled by pi, so a white light of intensity 1 on a white rough face gives about 1.

The built-ins are built by `fenrir_add_shaders` from `Shaders/Builtin/` and embedded as byte arrays, so the renderer needs no files beside it.

## Read-back

- `GetWorldBounds(entity)`: the box around the entity's mesh in world space, from the mesh's local bounds and the world matrix now; empty without a `MeshRenderer` or with a mesh that does not resolve.
- `Stats()`: the last drawn frame's `draws` (one per item per camera), `passes` run, and `uploads` (meshes and material blocks).

## Layers

Three targets, three layers (ADR-0019): `FenrirRenderer`, the ECS facade (`include/`, `src/Facade/`), the only one with public headers and the only one that links FenrirWindow, `PRIVATE`; `FenrirRendererCore`, the renderer core (`src/Core/`), with no FenrirApp, FenrirECS, FenrirScene or FenrirWindow dependency; and the graphics device with its backends (`src/Device/`, `src/Backend/`), where only `src/Backend/OpenGL/` includes GL. The core and the facade are tested on the null backend through `RendererProbe` (`FenrirRendererTests`), and the OpenGL backend and the scene through GL pixel probes (`FenrirRendererGlTests`, CTest label `gl`, skipped without a display or an OpenGL 4.6 context).
