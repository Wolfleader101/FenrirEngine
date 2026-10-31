# FenrirRenderer

Draws the active scene's meshes through its cameras, every frame, over a private graphics device with an OpenGL 4.6 backend and a null backend (ADR-0016, ADR-0019, ADR-0022, ADR-0023, ADR-0024, ADR-0026, ADR-0047, ADR-0048). Game code draws the ECS way: a `MeshRenderer` on an entity, a `Camera` on another, a `DirectionalLight` and the `Sky` scene resource, which is also the scene's ambient light. No public header names GL, SDL or the shader compiler's libraries. Built with `FENRIR_ENABLE_RENDERER` (on by default).

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
- **Install order:** FenrirWindow (for any backend but `Null`), then FenrirAssets, then the Renderer, all before the first frame. `Install` requests the main window's graphics surface (an OpenGL 4.6 core context, a debug one with `debugValidation`, and an sRGB backbuffer with no depth, stencil or MSAA), so the window is made then. It registers the built-in shaders in the asset store: `builtin:unlit`, `builtin:lit`, `builtin:fallback`, `builtin:background` (with a `CUBEMAP` variant for a cubemap `Sky`), `builtin:output`, the debug drawing programs `builtin:debug-lines`, `builtin:debug-triangles` and `builtin:debug-text`, `builtin:2d` for the overlay's 2D draw lists, `builtin:panorama`, which draws a panorama sky into its cube texture, and the sky's filtering programs `builtin:sky-irradiance`, `builtin:sky-specular` and `builtin:brdf-lut`. It adds the `Overlay` app resource, removed when the owner ends.
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
2. Read the active scene: every visible `MeshRenderer` (with `GetWorldMatrix` once each), every enabled `Camera` as a view, the first `DirectionalLight` (more are warned about once), the `Sky` and any `SkyLight`, and the scene's `DebugDraw`, tessellated, when FenrirDebugDraw is installed. Inactive scenes are never read, and nothing is kept between frames.
3. Prepare: upload each mesh, material and image that is new or has a new revision (an image with every mip level, made at upload; the `Sky`'s cubemap as a cube texture, each face with its mips, or its panorama as a 2D texture with its mips and an RGBA16F cube texture for it), and make each render target image's texture, keyed by asset handle and revision, and free the GPU side of each handle that went stale. Pipelines are made the first time a draw needs them (keyed by shader variant, blend mode, culling, vertex streams and sample count) and kept while their shader lives.
4. Build one render graph for every view: first, in the `Prepare` stage, one `Panorama` pass per face and mip level of a panorama prepared since the last frame, each drawing that level from the panorama itself (four points a texel, from the panorama's mip as coarse as the level), after which the panorama's own texture is freed; in the first frame, `BrdfLut`, the split sum's 128 x 128 RG16F BRDF lookup table; and when the sky's source changes (a new cube texture, or a colour sky for a cube one), `SkyIrradiance`, its L2 spherical harmonics projected from its cube, and one `SkySpecular` pass per face and mip level of its prefiltered GGX cube (faces at most 128 wide, one roughness step a mip level), never for a new `intensity` or `rotation`; then per view `Opaque` (one draw per `Opaque` or `Masked` item, sorted by pipeline, material and mesh, writing reversed-Z depth cleared to 0) → `Background` (at depth 0, filling only what nothing drew: the camera's own colour, or the scene's `Sky`, a colour, or a cube texture sampled by the direction each pixel looks along, turned by the sky's `rotation` and scaled by its `intensity`) → `Transparent` (when there are `AlphaBlend` or `Additive` items: depth tested without writing it, back to front by the view-space depth of each item's world origin) → `Debug` (when the scene has debug drawing and the camera's `drawDebug` is on) → `Output` (the linear RGBA16F `SceneColor` resolved, scaled by the camera's `exposure`, mapped into [0, 1] by its `tonemapper` and encoded into the view's viewport of its sRGB target); then, once for the window, `Overlay 2D` (the overlay's 2D draw lists, when there are any) and `Overlay` (debug screen text and messages). A view whose draws sample an image another view draws runs after it. A window no view draws into is cleared to black, logged once.
5. Record each pass and submit it; then present.

## Components and resources

- `MeshRenderer { mesh, material, visible = true }`: one mesh, one material, one draw. `visible` hides only its own entity. An empty or stale mesh is skipped (a stale one logged once per handle); an empty or stale material draws with the fallback material, flat magenta and unlit, logged once per handle as a warning. A missing `Transform` is the identity.
- `SkinnedMesh { skeleton }`, beside a `MeshRenderer` whose mesh has skin data: skins it on the GPU from the pose of `skeleton`, the entity carrying FenrirSkeleton's `SkeletonPose` (see "Skinning"). **A skinned item is drawn with the skeleton entity's world matrix: the mesh entity's own `Transform` and its ancestors are never applied.** `visible` and the material stay on the mesh entity.
- `Camera`: a view from its entity along local -Z, +Y up, with reversed-Z depth (ADR-0026). Every enabled camera in the active scene draws one view a frame; there is no active camera.
  - `projection`: `Perspective { verticalFov = 60 degrees in radians, near = 0.1 }`, with an infinite far plane, or `Orthographic { height (full, world units) = 10, near = 0.1, far = 1000 }`. The aspect comes from the viewport's pixel size. A tagged union, as FenrirPhysics' `CollisionShape` is: assign a kind (`camera.projection = Fenrir::Orthographic{20.0f}`) and read it with `Is<T>()` and `As<T>()`.
  - `enabled` (true) and `order` (0): views into one target draw lowest order first, ties by entity id, logged once.
  - `viewport`: `{ x, y, width, height }` as fractions of the target from its top-left corner, the whole target by default. Each view has its own scene targets at its viewport's pixel size, and `Output` writes only its viewport; the first view into a target clears it to black.
  - `target`: `WindowTarget { window }` (the main window, the default) or `ImageTarget { image }`, a render target: an `ImageData` with a size, RGBA8, sRGB, flat, and no pixels, made with the store's `Add` and resized with `Replace`. A material texture parameter naming it samples it, and the view drawing it runs first. A view whose own draws sample its target gets the parameter's default texture there, logged once, as does one whose sampling would make views wait on each other in a cycle (broken in the views' order).
  - `background`: `SceneBackground` (the default: the scene's `Sky`) or `ColourBackground { colour }`, which overrides the `Sky` for this camera's view only.
  - `drawDebug` (true): whether the view draws the scene's debug shapes and world text (see "Debug drawing").
  - `exposure` (0): EV stops; `Output` multiplies the view's linear colour by 2^`exposure`, so each +1 doubles it. There is no auto-exposure.
  - `tonemapper` (`Tonemapper::AgX`): how `Output` maps the exposed HDR colour into [0, 1] (ADR-0049). `AgX` desaturates bright colours towards white and keeps detail several stops over 1; `PbrNeutral` (Khronos PBR Neutral) shows colours up to about 0.76 as they are and compresses only the highlights, so base colours read true; `Aces` is Stephen Hill's fit of ACES, contrasty and saturated; `None` clamps each channel, the old `Output`, for exact colours (an unlit colour shows as its own value only under `None`). Under the others even a colour of 1 is darker than white: AgX maps 1 to 0.59 and mid grey 0.18 to 0.21.

  Not drawn, logged once: a camera targeting a window other than the main window, a viewport under one pixel, or a target image that is empty, stale or not a render target. A minimised window draws nothing.
- `DirectionalLight { colour, intensity }` shines along its entity's world -Z. Intensities are plain multipliers.
- `Sky` is a scene resource, the scene's ambient light as well as its backdrop, a tagged union like `projection` (ADR-0047): `ColourSky { colour }` (the default, a dark grey, which a scene without a `Sky` gets), `CubemapSky { cubemap, intensity, rotation }`, an `ImageData` of shape `Cubemap` with pixels, such as one loaded from a `.cubemap` file, or `PanoramaSky { image, intensity, rotation }`, a panorama: a flat image with pixels twice as wide as high, usually a linear HDR `.hdr` (RGBA32F). The cubemap is sampled by world direction with its faces laid out as GL and D3D lay out a cubemap's (+X, -X, +Y, -Y, +Z, -Z, each face's top row up), seamlessly across its edges. A panorama's centre looks along -Z, its right half turns towards +X and its top row looks up; the renderer draws it on the GPU into an RGBA16F cube texture with faces half as wide as the image is high and every mip level, once per image revision (values past the half float's 65504 are clamped), and then draws that like a cubemap. `intensity` (1 by default) multiplies what the sky draws; `rotation` (radians, 0 by default) turns it about world +Y, counter-clockwise seen from above. A cubemap that is empty, stale or not a cubemap with pixels, or a panorama that is empty, stale, or not a flat image with pixels twice as wide as high, draws the default dark grey, logged once per handle. Every view is lit by the scene's `Sky`, whatever its camera's background: a `ColourSky` lights with its colour from every side, and a cubemap or panorama sky through image-based lighting (diffuse from its spherical harmonics, specular from its prefiltered cube), scaled by its `intensity` and turned by its `rotation`.
- `SkyLight { sky }` is an optional scene resource that lights the scene in its `Sky`'s place, leaving the `Sky` drawn (ADR-0047): a sky of any kind, read as the `Sky` is, used only for its light. A skybox chosen for how it looks can light the scene with a neutral `ColourSky` or a softer panorama instead of its own colours. As a camera's `ColourBackground` changes what is drawn and not the light, a `SkyLight` changes the light and not what is drawn; either way one sky lights the scene.

## Materials and built-in shaders

Materials are FenrirAssets' `MaterialData`, edited through the asset store (ADR-0024). The renderer lays a material's set 2 out from its shader container's reflection: the `MaterialParams` block is the shader's default block image with the material's overrides written over it, and each texture parameter binds its image (uploaded with its mips, sRGB or linear as the image is; a render target image binds its texture) with the parameter's sampler. One sampler is made per distinct `SamplerDescription`. A texture left unset, or naming an image that cannot be sampled as a 2D texture (a cubemap, logged once), binds the renderer's default 1x1 `white`, `black` or `normal` texture, as its parameter default says, instead of adding a variant key. Sets 0 (frame), 1 (view) and 3 (draw) are declared in C++ as `Fenrir/Frame.glsl`, `View.glsl` and `Draw.glsl` declare them, and in debug builds a shader whose reflection disagrees fails its pipeline with a diagnostic and draws as the fallback material. A vertex stream a shader reads but the mesh lacks binds a constant (normal +Y, UV 0, colour white), logged once per mesh. Tangents are optional and not logged: a mesh without them binds the zero tangent, whose handedness 0 tells a shader it has none.

- **Blend modes:** `Opaque` and `Masked` draw in `Opaque`, writing depth; `AlphaBlend` (straight alpha) and `Additive` (colour added, alpha kept) in `Transparent`, without writing it. Depth is always tested, greater-or-equal for reversed-Z, and `doubleSided` turns culling off. Transparent items are sorted by their origins, not per triangle, and there is no order-independent transparency, so intersecting or nested transparent items can sort wrongly.
- **Variants:** the renderer picks the keys. A `Masked` material draws with its shader's `ALPHA_MASK` variant if the shader has the key, and its `alphaCutoff` (`SetAlphaCutoff`) is written into that variant's `alphaCutoff` block member, over any parameter override. A skinned draw uses the `SKINNED` variant (see "Skinning"). A user shader's own keys stay off.
- `builtin:unlit`: `baseColour` (white) x `baseColourTexture` (white) x the vertex colour.
- `builtin:lit`: metallic-roughness GGX through `Fenrir/Lighting.glsl` (the directional light and the `Sky`'s image-based lighting), written as linear HDR, with `baseColour` (white) x `baseColourTexture` (white), `metallic` (0) and `roughness` (0.5) x the blue and green of `metallicRoughnessTexture` (white), `normalTexture` (the flat normal; a tangent-space map whose green points up the image, in the mesh's tangent stream's frame when it has one (glTF's MikkTSpace tangents, FBX's tangents and binormals; ADR-0048), and otherwise in a frame from screen-space derivatives of the position and UV0), and `emissive` (opaque black) x `emissiveTexture` (white, so an emissive colour shows with no texture). Its BRDF is scaled by pi, so a white light of intensity 1 on a white rough face gives about 1, and so does a white sky of intensity 1 all round it. A double-sided face's back is lit as its own side.
- **User shaders:** a full vertex and fragment program on the engine includes, with its own `MaterialParams` block and textures in set 2, built by `fenrir_add_shaders` (embedded, or a container file read into `ShaderData`) and added to the store as a `Shader`. Lighting is the shader's choice: it may fill a `FenrirSurface` (base colour, alpha, world normal and position, metallic, roughness, occlusion, emissive) and call `FenrirLitColour` from `Fenrir/Lighting.glsl` (ADR-0048), which reads the light and the sky's spherical harmonics, prefiltered cube and BRDF lookup table from set 0 (`Fenrir/Frame.glsl`).

The built-ins are built by `fenrir_add_shaders` from `Shaders/Builtin/` and embedded as byte arrays, so the renderer needs no files beside it.

## Debug drawing

The renderer draws the active scene's `DebugDraw` (FenrirDebugDraw, ADR-0025) when FenrirDebugDraw is installed; without it, nothing. Each frame it takes the `DebugDraw`'s tessellated batches and uploads them into per-frame ring buffers.

- **Shapes and world text** draw in each view's `Debug` pass, into its `SceneColor` after `Transparent`, unless the camera's `drawDebug` is off. Depth-tested shapes test the view's `SceneDepth` without writing it; on-top shapes skip the test and draw after them. Lines are expanded in the vertex shader into quads their thickness in pixels across, square-capped, so a zero-length line (a point) is a square dot. Solid triangles alpha-blend, unsorted. World text is centred on its point, at its size in pixels (logical pixels, scaled by the content scale, in a view of the window), facing the screen, never hidden, and not drawn behind the camera.
- **Screen text and on-screen messages** draw once, in the window's `Overlay` pass after every view's `Output`, as the overlay's top layer, when a view of the scene draws into the window; views into images get none. Positions and sizes are logical units, scaled by the window's content scale. Messages stack from 8 units in from the window's top-left, newest first.
- **The font** is JetBrains Mono Regular (`Resources/Fonts/`, SIL Open Font License 1.1, `OFL.txt`), embedded at build time and rasterised at start-up with stb_truetype into one single-channel signed-distance-field atlas that serves every size. It covers printable ASCII and Latin-1; any other character, or a byte that is not UTF-8, draws `?`. Text has a thin dark outline so it reads over light and dark alike.

## The window overlay

Any UI draws over the game through the `Overlay` app resource (`FenrirRenderer/Overlay.hpp`, ADR-0027), filling FenrirDraw2D's plain-data `DrawList2D`s with its own shaders, vertex layouts, textures and scissors:

```cpp
Fenrir::DrawList2D list;
Fenrir::PipelineDescription2D pipeline;
pipeline.shader = store.Load<Fenrir::Shader>("builtin:2d", Fenrir::AssetOwner::App()).handle;
pipeline.vertexLayout = Fenrir::Vertex2DLayout(); // the ImGui layout: position, UV, RGBA8 colour
const std::uint32_t index = list.AddPipeline(pipeline);
// ... AppendVertices, AppendIndices, then a Command2D per draw naming `index`, its indices, textures and scissor
app.GetResource<Fenrir::Overlay>().Submit(Fenrir::Window::MainWindow(), 0, std::move(list));
```

- **When:** `Submit(window, layer, list)` any time before `Render`, from any system; the renderer draws the submissions that frame and clears them after it. A window with no submissions draws no UI, and one no camera draws into still gets its overlay over the black.
- **Order:** layers lowest first, then submission order, then each list's command order, in the window's `Overlay 2D` pass after every view's `Output`. Debug screen text and messages draw above every layer.
- **The target:** local coordinates are logical units from the window's top-left, Y down (the mouse's units), mapped to clip space over the window's logical size, its pixel size over its content scale. No depth and no culling. A scissor is scaled by the content scale, rounded outwards to whole pixels and clamped to the window.
- **Pipelines:** each 2D pipeline description becomes a device pipeline the first time it is drawn, cached by value while its shader lives, so equal descriptions share one. Blend modes take ADR-0027's colour and alpha factors: `AlphaBlend` (straight), `PremultipliedAlpha`, `Additive` (colour added, the window's alpha kept) and `Opaque`. Blending is linear and the sRGB backbuffer encodes on write; the `Gamma` blend space turns encoding off so values pass through and blend in sRGB, as stock ImGui assumes, and is allowed only on an 8-bit sRGB target (elsewhere it is logged as an error and draws as `Linear`).
- **Shaders:** 2D shaders take set 0 from the target (`Fenrir/2D.glsl`: `localToClip`, pixel size, content scale) and set 1 per command, laid out from the shader's reflection: a `MaterialParams` block at binding 0 (its parameter defaults with the command's up to 128 parameter bytes written over its start), then up to 4 textures, slot by slot in binding order. A texture slot's image is prepared as a material's is (a render target image binds its texture); a missing or stale one binds the texture's parameter default. The renderer picks the variant from the description: a shader with the keys gets `INPUT_SRGB` for sRGB vertex colours, `PREMULTIPLY` for `PremultipliedAlpha` and `GAMMA` for the gamma path.
- **`builtin:2d`:** the vertex colour times the slot-0 texture (white with none), on the ImGui layout. It decodes sRGB vertex colours, treats colours and texels as straight alpha, and premultiplies in linear for `PremultipliedAlpha`. On the gamma path it works in sRGB-encoded values: sRGB colours as given, linear ones encoded, texels (decoded on sampling) encoded again.
- **Skipped, each reason logged once:** a command with more than 4 texture slots or 128 parameter bytes, or a pipeline index or index range outside its list; a list whose pipeline names a stale shader or one that is not a 2D shader; and a submission to a window the renderer does not draw (only the main window, for now), logged once per window.

## Skinning

Skinned meshes (ADR-0035) are drawn on the GPU from their skeleton's palette. The renderer never links FenrirSkeleton (ADR-0041): it reaches skeleton poses through FenrirAssets' `SkinningSource`, which `Skeletons::Install` registers as the app resource `SkinningSourceRegistration`, and looks it up each `Render`, so install order does not matter. With no source, rigid meshes draw as usual and skinned ones are skipped.

- **What is drawn:** a `MeshRenderer` whose mesh has skin data and which has a `SkinnedMesh` naming a `Ready` skeleton entity whose skeleton the mesh was baked for: the mesh's `boneCount` is at most the skeleton's bone count, and its `expectedSignature` is the skeleton's bind signature unless it is 0. The verdict is cached per mesh entity, by the mesh's and the skeleton's handles and revisions.
- **Skipped, each logged once as a warning:** skin data but no `SkinnedMesh`; a `SkinnedMesh` whose skeleton entity is dead, has no `SkeletonPose` or is not `Ready`; no skinning source (once for the app); a failed compatibility check. A user shader without the `SKINNED` key draws a skinned mesh with the fallback material's `SKINNED` variant, magenta, never unskinned (once per shader). A `SkinnedMesh` on a mesh with no skin data is ignored, and the mesh draws rigidly (once).
- **The palette:** each frame the skinned draws' palettes are written into a storage buffer at set 3, binding 1, as `struct FenrirBone { vec4 r0, r1, r2; }` (rows 0-2 of each bone's skinning matrix, 48 bytes), ring-buffered per frame in flight and grown by doubling; nothing is kept across frames. One range is written per skeleton entity and pose generation in a frame, shared by every mesh and camera drawing it. Each draw record (set 3, binding 0) carries its `paletteOffset`, in bones, and the sign of its world matrix's determinant. A range past what was written is an internal error, and the draw is skipped. Debug builds check both records' layouts against the shader's reflection.
- **Shaders:** `Fenrir/Skinning.glsl` declares the palette and the joints and weights at vertex locations 6 and 7 (four `u16` indices and four `unorm16` weights, as `MeshData` stores them), and gives `FenrirSkinPosition`, `FenrirSkinNormal` and `FenrirSkinTangent`, each taking the draw record and returning world space: normals by the cofactor of the blended 3x3 (the inverse transpose's direction) with fallbacks for near-singular blends, tangents Gram-Schmidt'd against the normal with handedness `t.w x sign(det A) x sign(det W)`. The built-in programs are built with `SKINNED`; a user shader that should skin does the same (`KEYS SKINNED`) and calls them under `#ifdef SKINNED`.

## Spawning models

`SpawnModel(app, scene, model, parent = {})` (`FenrirRenderer/SpawnModel.hpp`) spawns an imported `Model` as drawn entities and returns its root: the app's store's `SpawnNodes` (an entity per ordinary node, under a root named after the file), then `SpawnSkeletons` through the skinning source (a `SkeletonPose` on each skeleton's owner, and bone attachments), then a `MeshRenderer` per mesh of each node. A rigid mesh's first `MeshRenderer` goes on the node's entity and each further one on a child entity named after the node (`Body mesh 2`); a skinned mesh gets an entity of its own, named after its node, a child of its skeleton's owner at the identity, with a `SkinnedMesh` naming the owner. With no skinning source it skips `SpawnSkeletons`, logged once per call. It needs no `Renderer` installed.

## Read-back

- `GetWorldBounds(entity)`: the box around the entity's mesh in world space, from the mesh's local bounds and the world matrix now; empty without a `MeshRenderer` or with a mesh that does not resolve. A skinned mesh's comes from its skeleton's pose now: each bone's box moved by its palette matrix, with the unweighted vertices' box, under the skeleton entity's world matrix; conservative, and empty without a `SkinnedMesh`, a skinning source or a `Ready` skeleton. A transparent skinned item is sorted by its bounds' centre.
- `Stats()`: the last drawn frame's `draws` (one per item per camera), `passes` run, and `uploads` (meshes, images and material blocks).

## Picking

Computed on call from the camera's world matrix and its target's size now, so usable in `Update` (ADR-0026). Points are logical window coordinates from the top-left for a window (with no window, the null backend's 1280 x 720) and pixels for an image.

- `ScreenPointToRay(camera, point)`: the ray through the point, from a perspective camera's position, or from the point in an orthographic camera's plane along its -Z.
- `WorldToScreenPoint(camera, point)`: where a world point appears in the camera's target.
- `CameraAt(window, point)`: the highest-order enabled camera whose viewport holds the point (of equal orders, the highest entity id: the one drawn last); a null `Entity` if none.

A stale entity, one with no `Camera`, a camera that draws nothing, a point outside the viewport, or a point behind the camera gives empty. There is no mesh picking.

## The app's renderer

Code that does not hold the owner reaches the renderer installed in an `App` through statics of the same names that take the `App` first: `Renderer::IsInstalled(app)`, `GetWorldBounds(app, entity)`, `Stats(app)`, `ScreenPointToRay(app, camera, point)`, `WorldToScreenPoint(app, camera, point)` and `CameraAt(app, window, point)`. Each answers as the owner's function while the owner `Install` made lives and is valid, and as an invalid owner otherwise. The scripting extension uses them.

## From C#

With `FENRIR_ENABLE_SCRIPTING` on, `FenrirRendererScripting` (`Scripting/`) gives C# the `Fenrir.Renderer` contract (`Managed/Fenrir.Renderer/`, ADR-0029): add `Renderer::ScriptingExtension()` (`FenrirRendererScripting/RendererScripting.hpp`) to `ScriptingConfig::extensions` after Input's and Assets's, whose contracts it builds on, and call `fenrir_deploy_renderer_scripting(<target>)` beside their deploy functions. It needs no `Renderer` installed.

- **Components:** `MeshRenderer`, `Camera` and `DirectionalLight`, writable mirrors of the C++ ones: the same fields in PascalCase, and `new` is C++'s `{}`. `Camera`'s unions are `[TaggedUnion]` structs: `CameraProjection` (`Perspective` or `Orthographic`), `RenderTarget` (`WindowTarget` or `ImageTarget`) and `CameraBackground` (`SceneBackground` or `ColourBackground`); `WindowTarget.Window` is a `Fenrir.Input.WindowId`.
- **Resources:** `Sky` (`ColourSky` or `CubemapSky`, a union too), a writable scene-resource mirror: `Resources.Get<Sky>()`. `AmbientLight` is deprecated and does nothing: the `Sky` is the scene's ambient light; it stays, hand-written, so scripts keep compiling, until the lighting spec's C# ticket removes it.
- **`Renderer`** (write `Fenrir.Renderer.Renderer` inside a `Fenrir.*` namespace): `ScreenPointToRay(camera, point, out ray)`, `WorldToScreenPoint(camera, point, out screenPoint)` and `CameraAt(window, point)`; `GetWorldBounds(entity, out bounds)` and `Stats(out stats)`; and `SpawnModel(model)` or `SpawnModel(model, parent)`, which records the tree C++'s `SpawnModel` makes through the change buffer and returns its root, alive at once so the same system can give it components.
- **No-op rules:** with no renderer (or, for `SpawnModel`, no asset store), an entity that is not alive or has no `Camera`, or an empty or stale model, a function does nothing and returns `false`, zero or `Entity.None`; nothing throws, and a debug build warns once per native call site and reason.

## Layers

Three targets, three layers (ADR-0019): `FenrirRenderer`, the ECS facade (`include/`, `src/Facade/`), the only one with public headers and the only one that links FenrirWindow, `PRIVATE`; `FenrirRendererCore`, the renderer core (`src/Core/`), with no FenrirApp, FenrirECS, FenrirScene or FenrirWindow dependency; and the graphics device with its backends (`src/Device/`, `src/Backend/`), where only `src/Backend/OpenGL/` includes GL. The core and the facade are tested on the null backend through `RendererProbe` (`FenrirRendererTests`), and the OpenGL backend and the scene through GL pixel probes and golden images (`FenrirRendererGlTests`, CTest label `gl`, skipped without a display or an OpenGL 4.6 context).

## Golden images

Where a frame cannot be pinned by a few exact pixels, a GL test compares the whole of it with a small committed PNG in `Tests/OpenGL/goldens/` (ADR-0049): `RendererTest::RequireGolden` (`Tests/GoldenImages.hpp`) converts both into CIELAB and counts the pixels whose CIEDE2000 difference is over 2, about what a viewer can just see; up to 16 may differ, so another driver's rounding or an edge rasterised a pixel over passes and a changed colour fails. A failing test writes the frame and a difference image (the golden dimmed, each differing pixel magenta) into `golden-failures/` in the tests' build directory and says where. Goldens are written only on request: run the GL tests with `FENRIR_UPDATE_GOLDENS=1`, look at each image that changed, and commit them with the change that meant to alter them:

```sh
FENRIR_UPDATE_GOLDENS=1 out/build/msvc-x64-debug/packages/FenrirRenderer/Tests/FenrirRendererGlTests.exe "[golden]"
```

The goldens were made on an NVIDIA GeForce RTX 5080 (driver 617.14). A GPU that fails them by a few pixels along edges wants a looser `GoldenTolerance` for that test, not new goldens, which would fail on the first GPU instead. The tonemapping goldens draw one lit scene (three spheres under a sun bright enough to clip, and an orange ramp from a quarter to eight times its colour) through each tonemapper, and through AgX at -2 and +2 EV.
