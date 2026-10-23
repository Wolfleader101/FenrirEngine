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

`backend` (`OpenGL`, or `Null`); `vsync` (`Off`, `On` default, `Adaptive`; changeable with `Renderer::SetVsync`, which returns the mode in effect, an unsupported one falling back to `On`, logged); `msaa` (samples per pixel of every view's `SceneColor` and `SceneDepth`, resolved in `Output`: 1 (off, the default), 2, 4 or 8; another count uses the largest of those below it, logged); `debugValidation` (on in Debug builds: GL's debug messages reach the app's logger); `framesInFlight` (2). There are no tuning settings and no native escape hatch.

## Main-thread rule and the Render phase

- Everything the renderer does runs on the main thread, where the GL context is current: its one system is sequential, in the `Render` phase. Call `Renderer`'s functions from the main thread too, outside parallel systems.
- **`Render`-phase rule:** the renderer reads the scene in `Render`, after `PostUpdate`, so it sees the poses interpolated in `AfterTicks` and everything C++ and C# wrote in `Update` and `PostUpdate`, whatever order packages were installed in. Writing scene state in `Render` is unsupported.

## Each frame

All in `Render`, on the main thread (ADR-0019):

1. Read the main window's pixel size. A minimised or zero-sized window builds, submits and presents nothing, but the frame index still advances.
2. Read the active scene: every visible `MeshRenderer` (with `GetWorldMatrix` once each), every enabled `Camera` as a view, the first `DirectionalLight` (more are warned about once) and the `AmbientLight`. Inactive scenes are never read, and nothing is kept between frames.
3. Prepare: upload each mesh, material and image that is new or has a new revision (an image with every mip level, made at upload), and make each render target image's texture, keyed by asset handle and revision, and free the GPU side of each handle that went stale. Pipelines are made the first time a draw needs them (keyed by shader variant, blend mode, culling, vertex streams and sample count) and kept while their shader lives.
4. Build one render graph for every view: per view `Opaque` (one draw per `Opaque` or `Masked` item, sorted by pipeline, material and mesh, writing reversed-Z depth cleared to 0) → `Background` (the camera's colour, or a dark grey at depth 0 until the sky arrives) → `Transparent` (when there are `AlphaBlend` or `Additive` items: depth tested without writing it, back to front by the view-space depth of each item's world origin) → `Output` (the linear RGBA16F `SceneColor` resolved, clamped and encoded into the view's viewport of its sRGB target). A view whose draws sample an image another view draws runs after it. A window no view draws into is cleared to black, logged once.
5. Record each pass and submit it; then present.

## Components and resources

- `MeshRenderer { mesh, material, visible = true }`: one mesh, one material, one draw. `visible` hides only its own entity. An empty or stale mesh is skipped (a stale one logged once per handle); an empty or stale material draws with the fallback material, flat magenta and unlit, logged once per handle. A missing `Transform` is the identity.
- `Camera`: a view from its entity along local -Z, +Y up, with reversed-Z depth (ADR-0026). Every enabled camera in the active scene draws one view a frame; there is no active camera.
  - `projection`: `Perspective { verticalFov = 60 degrees in radians, near = 0.1 }`, with an infinite far plane, or `Orthographic { height (full, world units) = 10, near = 0.1, far = 1000 }`. The aspect comes from the viewport's pixel size. A tagged union, as FenrirPhysics' `CollisionShape` is: assign a kind (`camera.projection = Fenrir::Orthographic{20.0f}`) and read it with `Is<T>()` and `As<T>()`.
  - `enabled` (true) and `order` (0): views into one target draw lowest order first, ties by entity id, logged once.
  - `viewport`: `{ x, y, width, height }` as fractions of the target from its top-left corner, the whole target by default. Each view has its own scene targets at its viewport's pixel size, and `Output` writes only its viewport; the first view into a target clears it to black.
  - `target`: `WindowTarget { window }` (the main window, the default) or `ImageTarget { image }`, a render target: an `ImageData` with a size, RGBA8, sRGB, flat, and no pixels, made with the store's `Add` and resized with `Replace`. A material texture parameter naming it samples it, and the view drawing it runs first. A view whose own draws sample its target gets the parameter's default texture there, logged once, as does one whose sampling would make views wait on each other in a cycle (broken in the views' order).
  - `background`: `SceneBackground` (the default: a dark grey until the sky) or `ColourBackground { colour }`.
  - `drawDebug` (true): read by the debug drawing feature.

  Not drawn, logged once: a camera targeting a window other than the main window, a viewport under one pixel, or a target image that is empty, stale or not a render target. A minimised window draws nothing.
- `DirectionalLight { colour, intensity }` shines along its entity's world -Z. `AmbientLight { colour, intensity }` is a scene resource; a scene without one is lit by a dim grey. Intensities are plain multipliers.

## Materials and built-in shaders

Materials are FenrirAssets' `MaterialData`, edited through the asset store (ADR-0024). The renderer lays a material's set 2 out from its shader container's reflection: the `MaterialParams` block is the shader's default block image with the material's overrides written over it, and each texture parameter binds its image (uploaded with its mips, sRGB or linear as the image is; a render target image binds its texture) with the parameter's sampler. One sampler is made per distinct `SamplerDescription`. A texture left unset, or naming an image that cannot be sampled as a 2D texture (a cubemap, logged once), binds the renderer's default 1x1 `white`, `black` or `normal` texture, as its parameter default says, instead of adding a variant key. Sets 0 (frame), 1 (view) and 3 (draw) are declared in C++ as `Fenrir/Frame.glsl`, `View.glsl` and `Draw.glsl` declare them, and in debug builds a shader whose reflection disagrees fails its pipeline with a diagnostic and draws as the fallback material. A vertex stream a shader reads but the mesh lacks binds a constant (normal +Y, UV 0, colour white), logged once per mesh.

- **Blend modes:** `Opaque` and `Masked` draw in `Opaque`, writing depth; `AlphaBlend` (straight alpha) and `Additive` (colour added, alpha kept) in `Transparent`, without writing it. Depth is always tested, greater-or-equal for reversed-Z, and `doubleSided` turns culling off. Transparent items are sorted by their origins, not per triangle, and there is no order-independent transparency, so intersecting or nested transparent items can sort wrongly.
- **Variants:** the renderer picks the keys. A `Masked` material draws with its shader's `ALPHA_MASK` variant if the shader has the key, and its `alphaCutoff` (`SetAlphaCutoff`) is written into that variant's `alphaCutoff` block member, over any parameter override. `SKINNED` is reserved for the animation spec, and a user shader's own keys stay off.
- `builtin:unlit`: `baseColour` (white) x `baseColourTexture` (white) x the vertex colour.
- `builtin:lit`: metallic-roughness GGX, one directional light and constant ambient, written as linear HDR, with `baseColour` (white) x `baseColourTexture` (white), `metallic` (0) and `roughness` (0.5) x the blue and green of `metallicRoughnessTexture` (white), `normalTexture` (the flat normal; a tangent-space map whose green points up the image, its tangent frame from screen-space derivatives of the position and UV0, so no tangent stream is needed), and `emissive` (opaque black) x `emissiveTexture` (white, so an emissive colour shows with no texture). Its BRDF is scaled by pi, so a white light of intensity 1 on a white rough face gives about 1. A double-sided face's back is lit as its own side.
- **User shaders:** a full vertex and fragment program on the engine includes, with its own `MaterialParams` block and textures in set 2, built by `fenrir_add_shaders` (embedded, or a container file read into `ShaderData`) and added to the store as a `Shader`. Lighting is the shader's choice: the light and ambient are in set 0 (`Fenrir/Frame.glsl`).

The built-ins are built by `fenrir_add_shaders` from `Shaders/Builtin/` and embedded as byte arrays, so the renderer needs no files beside it.

## Spawning models

`SpawnModel(store, scene, model, parent = {})` (`FenrirRenderer/SpawnModel.hpp`) spawns an imported `Model` as drawn entities and returns its root: the store's `SpawnNodes` (an entity per node, under a root named after the file), then a `MeshRenderer` per mesh of each node, the first on the node's entity and each further one on a child entity named after the node (`Body mesh 2`). It needs no `Renderer` installed.

## Read-back

- `GetWorldBounds(entity)`: the box around the entity's mesh in world space, from the mesh's local bounds and the world matrix now; empty without a `MeshRenderer` or with a mesh that does not resolve.
- `Stats()`: the last drawn frame's `draws` (one per item per camera), `passes` run, and `uploads` (meshes, images and material blocks).

## Picking

Computed on call from the camera's world matrix and its target's size now, so usable in `Update` (ADR-0026). Points are logical window coordinates from the top-left for a window (with no window, the null backend's 1280 x 720) and pixels for an image.

- `ScreenPointToRay(camera, point)`: the ray through the point, from a perspective camera's position, or from the point in an orthographic camera's plane along its -Z.
- `WorldToScreenPoint(camera, point)`: where a world point appears in the camera's target.
- `CameraAt(window, point)`: the highest-order enabled camera whose viewport holds the point (of equal orders, the highest entity id: the one drawn last); a null `Entity` if none.

A stale entity, one with no `Camera`, a camera that draws nothing, a point outside the viewport, or a point behind the camera gives empty. There is no mesh picking.

## Layers

Three targets, three layers (ADR-0019): `FenrirRenderer`, the ECS facade (`include/`, `src/Facade/`), the only one with public headers and the only one that links FenrirWindow, `PRIVATE`; `FenrirRendererCore`, the renderer core (`src/Core/`), with no FenrirApp, FenrirECS, FenrirScene or FenrirWindow dependency; and the graphics device with its backends (`src/Device/`, `src/Backend/`), where only `src/Backend/OpenGL/` includes GL. The core and the facade are tested on the null backend through `RendererProbe` (`FenrirRendererTests`), and the OpenGL backend and the scene through GL pixel probes (`FenrirRendererGlTests`, CTest label `gl`, skipped without a display or an OpenGL 4.6 context).
