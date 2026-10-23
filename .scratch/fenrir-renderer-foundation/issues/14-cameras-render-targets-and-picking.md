# 14: Cameras in full: several views, render targets, MSAA and picking

**What to build:** Several cameras draw at once in an order into viewports, perspective or orthographic, into the window or into an image a material samples, with MSAA, and game code can pick from `Update`; RendererDemo's "Cameras and render targets" scene shows it (spec: "Cameras, render targets and sky" (all but the sky), "FenrirRenderer: the frame" (targets, one graph for every view), "RendererConfig" (`msaa`), "Demo apps" (RendererDemo scene 2); ADR-0026, ADR-0019).

- **`Camera`** gains every field: `projection` (`Perspective` or `Orthographic { height, near, far }`, a tagged union in `CollisionShape`'s pattern), `enabled`, `order`, `viewport` (normalised, top-left, default whole), `target` (`Window(WindowId)`, main by default, or `Image(AssetHandle<Image>)`), `background` (`Scene` or `Colour(c)`; ticket 15 makes `Scene` the sky), `drawDebug` (default `true`, read by ticket 17).
- **Which cameras draw:** every enabled camera in the active scene is one view; views into one target by `order`, ties by entity id logged once; each view its own scene targets, `Output` writing only its rectangle. Skipped and logged once: a camera targeting a window other than the main window, a viewport under one pixel, a stale or empty target image. A minimised window's cameras skipped silently.
- **Image targets:** a pixel-less RGBA8 sRGB `ImageData` made with `Add`, resized with `Replace`. Materials sample it like any image, and the graph orders its writer before its readers. A view whose own draws sample its own target gets the default texture there, logged once.
- **MSAA:** `RendererConfig::msaa` samples on `SceneColor` and `SceneDepth`, resolved in `Output`; pipelines keyed by sample count.
- **Picking** on `Renderer`, from the camera's current world matrix and its target's current size: `ScreenPointToRay`, `WorldToScreenPoint`, `CameraAt(WindowId, Vec2)` (highest-order enabled camera whose viewport holds the point). Logical window coordinates for windows, pixels for images. Stale entity, no `Camera`, outside the viewport or behind the camera give empty.
- **The demo scaffold's** scene-click ray now comes from `ScreenPointToRay`.
- **RendererDemo scene 2, "Cameras and render targets":** split screen, an orthographic top-down minimap, and a monitor sampling a third camera's image; keys cycle `order`, the layouts and the projection. Hand checks in the README.

**Blocked by:** 12 (The demo scaffold and an empty RendererDemo).

**Touches:** `packages/FenrirRenderer/`, `examples/DemoScaffold/`, `examples/RendererDemo/`

**Status:** resolved

Hand check: in RendererDemo's "Cameras and render targets" scene (examples/RendererDemo/README.md), the main view fills the window with the orthographic top-down minimap in the top-right corner; the monitor shows the circling third camera's image, upright and moving; L cycles one view, side by side and one over the other, each view filling its part exactly; O sends the minimap under the main views and back; P switches the main views between perspective and orthographic; resizing the window keeps every viewport proportional; box edges are smooth (4x MSAA).

- [x] Null-backend tests: two cameras draw in `order` with their viewport rectangles; ties log once; an image-target view is ordered before the view sampling it; self-sampling gets the default texture, logged once; each skip case is skipped and logged once; disabled cameras draw nothing.
- [x] Picking tests (no GPU): `ScreenPointToRay` at the viewport centre is the camera's −Z; `WorldToScreenPoint` inverts it; `CameraAt` returns the highest-order camera under a point; every empty case is empty; orthographic rays are parallel.
- [x] With `msaa = 4` the scene targets have 4 samples and `Output` resolves.
- [x] GL pixel probes: an image target written by one camera and sampled by a material reads back the expected colour; an MSAA run renders with no GL errors.
- [x] `FENRIR_DEMO_SMOKE` runs the scene with its keys on null and GL with no errors.
- [ ] Scene 2 by hand: split screen halves, the minimap, the monitor, and the order, layout and projection keys look right; resizing the window keeps viewports proportional (checked by hand).
