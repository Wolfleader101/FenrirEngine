# FenrirDraw2D

2D draw data as plain data (ADR-0027): what a UI library, FenrirPainter's painter or any language fills, and FenrirRenderer draws. It has no GPU code and knows no renderer, UI or language. Header-only, it depends on FenrirAssets (asset handles, `SamplerDescription`, `ColourSpace`) and FenrirMath only, and is built with `FENRIR_ENABLE_ASSETS`.

## DrawList2D

`FenrirDraw2D/DrawList2D.hpp`. One batch of 2D drawing:

- **`vertices` and `indices`:** bytes, appended with `AppendVertices<T>` and `AppendIndices<T>`, which return the first appended element's number.
- **`pipelines`:** a table of 2D pipeline descriptions (`PipelineDescription2D`), each a plain value with no renderer handle: a 2D shader (`AssetHandle<Shader>`, such as `builtin:2d`), a vertex layout (`stride`, and per attribute its `location`, `VertexFormat2D` and `offset`), an index type (`UInt16` default, or `UInt32`), a blend mode (`AlphaBlend` default, `PremultipliedAlpha`, `Additive`, `Opaque`), an input colour space (`Srgb` default, or `Linear`) and a blend space (`Linear` default, or `Gamma`). `AddPipeline` reuses an equal one.
- **`commands`,** drawn in order, each a `Command2D`: a tagged union (a `std::variant`) of a draw and the begin and end of an offscreen group and of a mask's reach. A default command is a draw.
  - **`DrawCommand2D`:** a pipeline index, texture slots (an image and a sampler, linear clamp by default), an optional scissor (`Rect2D`), an index offset and count, a base vertex, and parameter bytes.
  - **`BeginGroup2D`** (or `BeginGroup(bounds)`): opens an offscreen group (ADR-0060), whose commands draw into an image of their own: its `bounds` in local units, and its pixel `format`, `Rgba8Premultiplied`, the only one.
  - **`EndGroup2D`** (or `EndGroup(composite)`): closes the innermost group and says how its image is composited over what is under it: an `opacity` (1 by default), a `GroupBlend2D` blend (`Normal`, drawn over, by default, or `Additive`), a `GroupFilter2D` filter (`None` by default, `Blur` with a `radius`, `DropShadow` with a `radius`, `offset` and `shadowColour`, or `BackdropBlur` with a `radius`: what is under the group's bounds blurred and drawn back, then the group over it, even when the group is empty; a radius is a Gaussian's standard deviation in local units), or, with `mask`, not drawn but masking the commands that follow up to the next `EndMask2D`.
  - **`EndMask2D`:** ends a mask's reach.

  Groups nest `MaxGroupDepth2D` (8) deep; the renderer logs a deeper one once and draws its commands flat into the group around it. A list whose groups and masks do not balance (an end with nothing open, or something left open) is logged once and skipped whole. The renderer realises each composite (`packages/FenrirRenderer/README.md`); a command a mask reaches is masked only if its shader has a `MASK` variant.

Coordinates, vertices and scissors alike, are local and logical: units from the top-left, Y down, the units of FenrirInput's mouse. A draw list never says where it is shown.

**Limits** of this version, checked by the renderer, which skips a command over one and logs it once: `MaxTextureSlots2D` (4) and `MaxParameterBytes2D` (128), with 16- and 32-bit indices.

`Vertex2D` is the ImGui vertex layout `builtin:2d` reads (position `vec2`, UV `vec2`, colour RGBA8 with red in the lowest byte), and `Vertex2DLayout()` describes it.

## Drawing it

FenrirRenderer's `Overlay` app resource takes lists for a window's overlay layers, and its `ImageTargets2D` app resource takes lists for image assets: see the "The window overlay" and "The image 2D target and offscreen groups" sections of `packages/FenrirRenderer/README.md`.

## Tests

`FenrirDraw2DTests` (with `FENRIR_BUILD_TESTS`) checks the data's defaults and helpers (groups' among them), and fails the build if the header reaches GL, SDL, FenrirWindow, FenrirRenderer or the shader compiler.
