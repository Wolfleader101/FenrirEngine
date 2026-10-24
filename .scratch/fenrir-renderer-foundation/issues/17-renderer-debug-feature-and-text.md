# 17: The renderer draws debug draw: lines, solids, world text and screen text

**What to build:** Everything recorded in a scene's `DebugDraw` appears on screen: shapes in each view's `Debug` stage with screen-space thickness and both depth modes, world text facing the camera, and screen text and on-screen messages on top of each window's overlay, in an embedded SDF font; the demo scaffold's F1 help panel and RendererDemo's "Debug draw" scene use it (spec: "FenrirDebugDraw" (text, drawing, font), "FenrirRenderer: the frame" (`Debug` and `Overlay` stages), "Cameras" (`drawDebug`), "Build" (the font, stb_truetype), "Demo apps" (scaffold help panel, RendererDemo scene 4), "Testing Decisions" seam 1 (debug batches); ADR-0025, ADR-0026).

- **The debug feature** in the renderer core: uploads the tessellated batches into per-frame ring buffers and draws them in each view's `Debug` stage, expanding lines to screen-space quads in the vertex shader (the built-in debug lines and triangles shaders). Depth-tested items test against the view's `SceneDepth`; on-top items do not. Solids alpha-blend, depth tested without write, unsorted. A camera with `drawDebug = false` draws none of it, world text included.
- **World text** at a world point, constant pixel size, facing the screen, never occluded, skipped behind the camera, in the view's `Debug` stage.
- **Screen text and messages** draw once in the `Overlay` stage of each window showing a view of the scene, in logical coordinates scaled by the content scale, as a fixed top layer (ticket 19's 2D layers go beneath it). Image targets get none.
- **Font:** one OFL monospace TTF checked in under FenrirRenderer's resources with its licence, embedded as a byte array, rasterised at start-up into one SDF atlas with stb_truetype (through FenrirRenderer's own `PRIVATE` include of the stb submodule). Covers printable ASCII and Latin-1, `?` for any other codepoint. The built-in SDF text shader.
- **`DemoScaffold::Install`** installs FenrirDebugDraw; the F1 help panel (app, scene, the scene's keys) draws as screen text.
- **RendererDemo scene 4, "Debug draw":** every shape wire and solid, depth tested and on top, each duration, groups toggled by keys, `relativeTo` an orbiting entity, world text, screen text and keyed messages. Hand checks in the README.

**Blocked by:** 14 (Cameras in full: several views, render targets, MSAA and picking); 16 (FenrirDebugDraw: debug shapes, text, durations and groups, headless).

**Touches:** `packages/FenrirRenderer/`, `examples/DemoScaffold/`, `examples/RendererDemo/`

**Status:** resolved

Hand check: RendererDemo's "Debug draw" scene, as its README's hand checks list: every shape wire and solid, depth tested (hidden by the wall) and on top (through it); each duration (every frame, T's three seconds, F's forever until X); the groups 1, 2 and 3 hide and show; the axes, box, circle and label `relativeTo` the orbiting sphere follow it; world text faces the screen at a constant size, never hidden and gone behind the camera; screen text and the keyed messages stay put; F1's help panel now shows on screen in every scene; and text stays crisp at a content scale other than 1.

- [x] Null-backend tests: debug batches reach each view's `Debug` stage with the line and triangle pipelines; on-top items use the no-depth-test pipeline; `drawDebug = false` removes them from that view only; screen text and messages reach each drawn window's `Overlay` once, after everything else; an image-target view gets no screen text.
- [x] World text behind the camera is skipped.
- [x] The SDF atlas covers ASCII and Latin-1, and an unmapped codepoint draws `?`.
- [x] GL pixel probe: a thick on-top line covers a pixel in front of an opaque quad; a depth-tested one behind the quad does not.
- [x] `FENRIR_DEMO_SMOKE` runs the scene with its keys on null and GL with no errors.
- [ ] Scene 4 by hand: every shape, both depth modes, durations, groups, the orbiting `relativeTo` item, world text, screen text, keyed messages and the F1 panel look right, and text stays crisp at a content scale other than 1 (checked by hand).
