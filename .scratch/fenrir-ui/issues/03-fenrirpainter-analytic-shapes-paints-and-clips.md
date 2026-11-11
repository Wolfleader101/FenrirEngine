# 03: FenrirPainter: analytic shapes, paints and clips in the fat vertex

**What to build:** a painter that draws anti-aliased rectangles, rounded rectangles, borders, shadows, circles, arcs, rings, gradients and images into a `DrawList2D`, each shape one quad evaluated in the fragment shader (spec: "FenrirPainter"; ADR-0060).

- **The package** depends on FenrirDraw2D and FenrirMath only (text is ticket 06); it builds its own shader asset `builtin:painter` with `fenrir_add_shaders` and registers it with the asset store when installed, as ADR-0027 allows a UI to bring its shaders.
- **`Painter`**: a value over a `DrawList2D`, a root scale and a blend space; UI units in, logical units out, optional pixel snapping; keeps nothing between frames.
- **Draws**: rectangle, rounded rectangle with per-corner radii, border with per-side widths and colours, shadow (offset, blur, spread, inset or outset), circle, arc, ring, image into a rectangle with a sampler and nine-slice insets. **Paint** per call: solid, linear gradient with stops and spread, radial gradient with a focal point, image pattern, each with a transform; colours sRGB straight alpha in, premultiplied in the vertex; more than two stops through a stop texture row.
- **State**: save/restore of transform (translate, scale, rotate, concat) and clip (rectangle or rounded rectangle under the current transform); the hardware scissor is the clip stack's intersected bounds and the per-pixel clip handles radii and rotation.
- **The fat vertex** (position, uv, colour, shape kind, shape rectangle, radii, border widths and colours, shadow parameters, paint kind and transform and extents and second colour, image slot, clip rectangle and radii and transform) and the painter's pipeline description (`builtin:painter`, `PremultipliedAlpha`, sRGB input, the root's blend space). **Batching**: same pipeline, textures and scissor extend one command; 16-bit index rollover starts a new command with a base vertex; 32-bit indices are an option.
- A sample: RendererDemo (or a tiny new example if cleaner) submits a painted list to the overlay so the shapes can be seen; its README says so.

**Blocked by:** none

**Touches:** `packages/FenrirPainter/`, `packages/CMakeLists.txt`, `README.md`, `examples/RendererDemo/`

**Status:** resolved

- [x] Draw-list tests: each draw appends one quad with the expected shape data; a gradient's stops and transform land in the vertex; a nine-slice image is nine quads; the clip stack's bounds become the scissor and its radii the vertex clip; same-state draws share one command and a texture or scissor change starts another; index rollover at 65535 vertices.
- [x] Null-backend test: the painter's pipeline description makes one device pipeline, cached by value, and the shader's variants compile for both blend spaces.
- [x] GL test (skipped without a display): a rounded rectangle, a radial gradient and a bordered box drawn to an image target and read back match expected pixels at their centres and edges within a tolerance.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
