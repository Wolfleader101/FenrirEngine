# FenrirDraw2D

2D draw data as plain data (ADR-0027): what a UI library, a future painter or any language fills, and FenrirRenderer draws. It has no GPU code and knows no renderer, UI or language. Header-only, it depends on FenrirAssets (asset handles, `SamplerDescription`, `ColourSpace`) and FenrirMath only, and is built with `FENRIR_ENABLE_ASSETS`.

## DrawList2D

`FenrirDraw2D/DrawList2D.hpp`. One batch of 2D drawing:

- **`vertices` and `indices`:** bytes, appended with `AppendVertices<T>` and `AppendIndices<T>`, which return the first appended element's number.
- **`pipelines`:** a table of 2D pipeline descriptions (`PipelineDescription2D`), each a plain value with no renderer handle: a 2D shader (`AssetHandle<Shader>`, such as `builtin:2d`), a vertex layout (`stride`, and per attribute its `location`, `VertexFormat2D` and `offset`), an index type (`UInt16` default, or `UInt32`), a blend mode (`AlphaBlend` default, `PremultipliedAlpha`, `Additive`, `Opaque`), an input colour space (`Srgb` default, or `Linear`) and a blend space (`Linear` default, or `Gamma`). `AddPipeline` reuses an equal one.
- **`commands`,** drawn in order: a pipeline index, texture slots (an image and a sampler, linear clamp by default), an optional scissor (`Rect2D`), an index offset and count, a base vertex, and parameter bytes.

Coordinates, vertices and scissors alike, are local and logical: units from the top-left, Y down, the units of FenrirInput's mouse. A draw list never says where it is shown.

**Limits** of this version, checked by the renderer, which skips a command over one and logs it once: `MaxTextureSlots2D` (4) and `MaxParameterBytes2D` (128), with 16- and 32-bit indices.

`Vertex2D` is the ImGui vertex layout `builtin:2d` reads (position `vec2`, UV `vec2`, colour RGBA8 with red in the lowest byte), and `Vertex2DLayout()` describes it.

## Drawing it

FenrirRenderer's `Overlay` app resource takes lists for a window's overlay layers: see the "The window overlay" section of `packages/FenrirRenderer/README.md`.

## Tests

`FenrirDraw2DTests` (with `FENRIR_BUILD_TESTS`) checks the data's defaults and helpers, and fails the build if the header reaches GL, SDL, FenrirWindow, FenrirRenderer or the shader compiler.
