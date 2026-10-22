# 15: The sky: a colour or a cubemap per scene, overridable per camera

**What to build:** Each scene's background is data: a `Sky` scene resource of a colour or a cubemap, drawn in the `Background` stage, with a camera able to override it with a colour; RendererDemo's "Sky" scene shows it (spec: "Cameras, render targets and sky" (`Sky`, `background`), "FenrirRenderer: the frame" (`Background` after opaque at depth 0), "FenrirAssets" (cubemap images), "Demo apps" (RendererDemo scene 3); ADR-0026).

- **`Sky`:** a scene resource beside `AmbientLight`, a tagged union of `Colour(c)` or `Cubemap(AssetHandle<Image>)`; dark grey when absent.
- **The background feature:** the `Background` pass draws at depth 0 after `Opaque`, filling only pixels nothing drew; the colour form as a flat fill, the cubemap form sampled by view direction. A camera's `background = Colour(c)` overrides the scene's sky for its view. A stale or empty cubemap falls back to the colour default, logged once.
- **Cubemap loading:** six face images loaded into one `ImageData` cubemap (the ticket picks the authoring form: six files or a labelled set), with a skybox cubemap added to `examples/assets/`.
- **RendererDemo scene 3, "Sky":** colour, cubemap, and a camera overriding it with a colour; keys switch them. Hand checks in the README.

**Blocked by:** 14 (Cameras in full: several views, render targets, MSAA and picking).

**Touches:** `packages/FenrirRenderer/`, `packages/FenrirAssets/`, `examples/RendererDemo/`, `examples/assets/`

**Status:** ready-for-agent

- [ ] Null-backend tests: with no `Sky` the background pass uses dark grey; a `Colour` sky and a `Cubemap` sky use their pipelines and bindings; a camera's `Colour` background overrides the scene's for its view only.
- [ ] GL pixel probes: an empty pixel reads the sky colour; a pixel under an opaque quad reads the quad, not the sky.
- [ ] `FENRIR_DEMO_SMOKE` runs the scene with its keys on null and GL with no errors.
- [ ] Scene 3 by hand: the cubemap surrounds the camera with no seams or flips, and the override camera shows its colour (checked by hand).
