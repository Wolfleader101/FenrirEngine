# 19: FenrirDraw2D and the window overlay

**What to build:** Any UI library can draw over the game: it fills plain-data `DrawList2D`s with its own shaders, vertex layouts, textures and scissors in logical coordinates and submits them to the window overlay in layers, beneath debug screen text, blended linearly with an explicit gamma path (spec: "FenrirDraw2D and the window overlay", "Shaders" (`2D.glsl`, `builtin:2d`), "Testing Decisions" seams 1 and 2 (overlay); ADR-0027). The ImGui overlay and RendererDemo's "Overlay" scene are ticket 20.

- **FenrirDraw2D** (`packages/FenrirDraw2D`): plain data, no GPU code, depending on FenrirAssets and FenrirMath only, built with `FENRIR_ENABLE_ASSETS`.
- **`DrawList2D`:** vertex and index bytes; a table of 2D pipeline descriptions (shader handle, vertex layout with stride and per-attribute location, format and offset, 16- or 32-bit indices, blend mode `AlphaBlend`/`PremultipliedAlpha`/`Additive`/`Opaque`, input colour space `Linear`/`sRGB`, blend space `Linear` default or `Gamma`), holding no renderer handle; ordered commands (pipeline index, up to 4 texture slots with a sampler, linear clamp by default, an optional scissor, index offset and count, base vertex, up to 128 parameter bytes). A command over a limit is skipped and logged once. Local logical coordinates: top-left, Y down.
- **The renderer's 2D feature:** copies the bytes into per-frame ring buffers, realises pipeline descriptions lazily cached by value, resolves texture slots as materials do. 2D shaders take set 0 from the target (`Fenrir/2D.glsl`: `localToClip`, pixel size, content scale) and set 1 per command (parameter block at binding 0 laid out from reflection, then textures).
- **`Overlay`:** an app resource of the facade, `Submit(WindowId, int layer, DrawList2D)`, any time before `Render`, cleared after it. Layers lowest first, then submission order, then command order; debug screen text stays the fixed top layer. Orthographic from the window's logical size, no depth or culling; scissors scaled by the content scale, rounded outwards and clamped. A submission to a window that is not drawn is dropped and logged once.
- **`builtin:2d`:** textured and vertex-coloured on the ImGui vertex layout (position `vec2`, UV `vec2`, colour RGBA8), decoding sRGB inputs and premultiplying in linear when the blend mode does.
- **Colour:** linear blending with the target encoding on write; colour and alpha factors per blend mode as ADR-0027's table gives; `Gamma` only on an 8-bit sRGB target, elsewhere a logged error drawing as `Linear`.

**Blocked by:** 17 (The renderer draws debug draw: lines, solids, world text and screen text).

**Touches:** `packages/FenrirDraw2D/`, `packages/FenrirRenderer/`, `CMakeLists.txt`

**Status:** resolved

- [x] Null-backend tests: two layers draw lowest first, then submission and command order, all before debug screen text; scissors resolve to the expected pixel rectangles at content scale 1 and 1.5; pipeline descriptions equal by value share one pipeline; an over-limit command is skipped and logged once; a submission to an undrawn window is dropped and logged once.
- [x] GL pixel probes: straight and premultiplied quads over opaque and non-opaque destinations give the ADR's results within tolerance; layering order; a scissor at content scale other than 1 clips the right pixels; `Gamma` on a non-sRGB target logs and draws as `Linear`.
- [x] FenrirDraw2D has no GPU or renderer dependency; Clang and MSVC x64 presets build with no new warnings.
