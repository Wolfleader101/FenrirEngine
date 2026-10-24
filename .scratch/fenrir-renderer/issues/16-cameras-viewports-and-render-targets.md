# How are cameras, viewports and render targets modelled?

Type: grilling
Status: resolved
Blocked by: 14

## Question

How does game code say what is drawn from where, and into what?

- A camera component on an entity (its world transform as the view), projection settings, which camera is active; `FenrirCamera` removed or reshaped.
- Several cameras: split screen, picture in picture, a camera drawing into a texture (which an editor's scene view would need).
- Viewports, resolution, aspect ratio and resizing.
- Screen-to-world rays for picking, and where that helper lives.
- Which windows show a scene's debug screen text and on-screen messages when several windows show views of it (ADR-0025 draws them in each such window's overlay).

## Answer

ADR: `docs/adr/0026-cameras-draw-with-reversed-z-into-windows-or-image-assets.md`. Glossary: Camera, Viewport, Render target, Sky. All of it goes into the foundation spec.

- **Camera component:** `Camera` in FenrirRenderer's facade, plain data and blittable, on an entity whose world transform (`GetWorldMatrix`) is the view: looking along local −Z, +Y up, as `DirectionalLight`. Fields: projection, `enabled`, `order`, `viewport`, `target`, `background`, `drawDebug`. No yaw, pitch or front vectors; a controller writes the entity's `Transform`. `FenrirCamera` stays untouched behind `FENRIR_BUILD_LEGACY_DEMOAPP` and is deleted with the old DemoApp; nothing new uses it. The free-fly controller belongs to the demo scaffold (ticket 23).
- **Projection:** `Perspective { verticalFov (radians, default 60°), near (0.1) }` with an infinite far plane, or `Orthographic { height (full, world units), near, far }`. Aspect always from the viewport's pixel size. Depth is reversed-Z in [0,1] (ADR-0026): `SceneDepth` is 32-bit float, cleared to 0, tested greater-or-equal; `Background` draws at depth 0.
- **Which cameras draw:** every enabled `Camera` in the active scene makes one view per frame; no single active camera. Views into the same target draw in `order` (default 0, lower first); ties by entity id, logged once. Split screen is two cameras with viewports; picture in picture is a higher-order camera with a small one. A window with no camera is cleared to black, its overlay still drawn, logged once. Inactive scenes are never drawn (ADR-0023).
- **Viewport:** a normalised rectangle `{x, y, w, h}` of the target, top-left origin (as window and mouse coordinates), default `{0, 0, 1, 1}`, resolved to pixels each frame from the target's pixel size, so resizing needs no game code. Each view gets its own `SceneColor`/`SceneDepth` at its viewport's pixel size; `Output` writes only its rectangle, and a view always covers its rectangle (no depth-only layering). No render scale and no pixel-exact rectangles.
- **Render targets:** `target` is a `WindowId` (main window by default) or an `AssetHandle<Image>` made as a pixel-less render-target `ImageData` (size, RGBA8 sRGB). Materials and the UI overlay sample it like any image; the graph orders its writer first. Resized by `Replace` (revision bump). A stale or empty target image skips the camera, logged once. A view sampling its own target gets the default texture in those draws, logged once.
- **Background:** a `Sky` scene resource beside `AmbientLight`: a solid colour or a skybox from a cubemap `AssetHandle<Image>`; dark grey when absent. Each camera's `background` is `Scene` (default, the `Sky`) or `Colour(c)`. FenrirAssets' `ImageData` must cover cubemaps (or equirectangular images) and render targets: a need for the foundation spec.
- **Picking:** on `Renderer`, computed on call from the camera's current world matrix and its target's current size, so usable in `Update`: `ScreenPointToRay(Entity camera, Vec2) -> optional<Math::Ray>`, `WorldToScreenPoint(Entity camera, Vec3) -> optional<Vec2>` and `CameraAt(WindowId, Vec2) -> Entity` (the highest-order enabled camera whose viewport holds the point). Points are in the target's coordinates: logical window coordinates (as mouse events, ADR-0017) for windows, pixels for images. Empty for a stale entity, no `Camera`, a point outside the viewport, or a world point behind the camera. No mesh picking: the ray goes to physics's `Raycast` or `GetWorldBounds`.
- **Debug output with several views (sharpens ADR-0025):** screen text and on-screen messages draw once in the `Overlay` of each window showing a view of the scene, in logical window coordinates scaled by the window's display scale; messages stack from the window's top-left, not per viewport. Image targets get none. Lines and world text draw in every view's `Debug` stage unless the camera's `drawDebug` (default on) is off.
- **Cameras that cannot draw:** a camera targeting a window other than the main window (ADR-0022) or with a viewport under one pixel is skipped and logged once; a minimised window's cameras are skipped silently.
- **C#:** `Camera`, `Sky` and the three helpers cross through the renderer's scripting extension; how is ticket 22's.
- **Later** (*Not yet specified*): depth-only camera layering, render scale, per-camera culling layers, per-viewport screen text, HDR render targets, drawing a scene that is not active (an editor). No new tickets.
