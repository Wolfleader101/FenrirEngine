# 30: Screen-space reflections: the trace and the lighting include's blend

**What to build:** Reflections of whatever is on screen, glowing geometry included, in every material that lights through `Fenrir/Lighting.glsl` (ADR-0056). Requested by the user while reviewing ticket 20 (the neon city), 2026-11-02.

- **`ReflectionTrace`**, a fullscreen fragment pass after `HiZ` (ticket 29): per pixel with depth, the mirror ray about the prepass normal, marched through the pyramid (Uludag's Hi-Z tracing, GPU Pro 5) for at most 64 iterations, a hit within the camera's `thickness`. It writes an RGBA16F hit target: the hit reprojected into the previous frame's UV, its confidence, and the ray's length. Confidence as ADR-0056 lists it: hit, edge and distance fades, facing, history validity, and disocclusion against last frame's pyramid.
- **`Fenrir/Lighting.glsl`** samples `fenrirSceneHistory` at the hit's UV, at the mip that spreads the hit over GGX's lobe for the surface's roughness and the ray's length, and blends it over the sky's prefiltered specular by the confidence times the roughness fade and the camera's intensity, through the same single-scattering share and specular occlusion. Rect lights' specular is scaled by 1 minus the confidence unless their `visibleSurface` is false; point and spot lights are untouched.
- **The view set gains** `fenrirReflectionHits` and `fenrirSceneHistory`, with 1 × 1 stand-ins (zero confidence) in every pass but `Opaque` and for views with reflections off; `ViewData` gains `reflectionInfo`. The C++ layout and its debug-build reflection check follow.
- **`Camera::reflections`** gains `intensity`, `maxDistance`, `thickness` and `roughnessCutoff` (defaults 1, 50, 0.5, 0.6); **`RectLight`** gains `visibleSurface` (true). FenrirRenderer's C# binding is regenerated for them, and the scripting round trip covers them.
- `packages/FenrirRenderer/README.md` says what the reflections catch and what they cannot: off-screen objects, one frame of lag, moving objects trailing.

**Blocked by:** 19, 29

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/README.md`

**Status:** resolved

- [x] CPU unit tests of the trace's maths, mirrored in C++ as Core/LightClusters mirrors its shader: the reflected ray's screen-space end points, a Hi-Z march over a known pyramid finding a known hit and missing past `maxDistance`, the edge fade, the reprojection into the previous frame's UV, and the roughness-to-mip choice.
- [x] GL pixel probes: a mirror floor in front of an emissive quad shows the quad's colour at the reflected pixel; with the quad behind the camera the floor shows only the sky; a rough floor past the cutoff matches it with reflections off; a rect light with a drawn board does not reflect twice, and one with `visibleSurface` false keeps its specular.
- [x] A golden image: a mirror floor under emissive strips and a rect light, reflections on.
- [x] Null `RendererProbe`: the trace pass follows HiZ, and Opaque binds the hit target and history.

Hand check: in the showroom, the polished floor shows the neon strip's and the helmets' reflections, sharp where the floor is smooth and blurred where rough, and they fade at the screen's edge rather than cutting off.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

Built 2026-11-04, stacked on ticket 29's `claude/lighting-29-ssr-history-201297` (#299, not yet merged). The hand check
is left to ticket 31, which turns reflections on in the showroom and the neon city and gives LightingDemo the toggle;
so is the 1.5 ms measurement (RenderDoc is installed on the developer's machine). Decisions to review:

- **The render graph gains `PassBuilder::SamplePrevious`.** `Opaque` reads last frame's history and this frame's
  `SceneHistory` passes write it afterwards, in `PostProcess`. The graph read every read before a resource's first
  writer as a read of the finished resource (so a view sampling another camera's image target runs after it), which put
  `Opaque` after `SceneHistory` and made a cycle through SceneColor. `SamplePrevious` is a read of what an imported
  texture held before the frame: its pass runs before the first writer, and a transient is an error. ADR-0056 did not
  foresee it; no ADR describes the graph's read rule, so none changes.
- **`FullscreenPasses` gains a public `Draw`**, the body `AddPass`'s passes recorded, so `ReflectionTrace` adds its own
  graph pass and notes which texture its hit target is as it records (`m_drawn`, read by `RendererProbe::ReflectionHits`).
  `RendererProbe::SceneColour` passes those over: the hit target is RGBA16F at the view's size, as SceneColor is, and
  the GL tests could not tell them apart. The null device's submitted lists could, but not the GL device's.
- **The trace reads depth from the pyramid's level 0, not SceneDepth**, so only SceneNormals needs an MSAA variant.
- **The march's details.** It starts by stepping out of the reflecting pixel, so its own depth is never a hit. On
  odd-sized levels the last texel on a side covers the rest of the view, as `builtin:hi-z` folds it. A level-0 texel the
  ray passes behind by more than the thickness is stepped over at level 0, so a wall behind a thin pillar can still be
  hit. Cells are crossed 0.01 pixels past their edge.
- **A towards-camera fade, which ADR-0056 does not list.** The ADR gives a ray pointing at the camera no confidence; a
  hard cut would draw a line where rays turn, so the confidence falls linearly from rays at view-space z -0.1 to 0 at
  z 0 (`ReflectionTowardsCameraFade`), and rays at or past 0 are not traced.
- **The fades' widths:** the edge fade spans the last tenth of the view at each edge, the distance fade the last fifth
  of `maxDistance`; facing is a step (the hit's normal against the ray), and disocclusion falls linearly over
  `thickness` of view depth.
- **Disocclusion linearises last frame's depths with this frame's projection.** The history keeps only the previous
  view-projection; a projection that changes between frames (a zoom) errs for that frame only.
- **The cone mip** takes Uludag's cone for GGX's a as a Phong power (2 / a^2 - 2, cone acos(0.244^(1 / (power + 1)))),
  spans its diameter at the ray's length, and sees it at the surface's view depth plus the ray's length, where the
  mirror image is; an orthographic camera ignores the depth. The level is log2 of that diameter in pixels.
- **The blend's weight is clamped to 1**, so an intensity above 1 cannot extrapolate past the reflection, and the
  reflection is not scaled by the sky's intensity, since it is the scene's own colour.
- **Rect lights yield by the blend's weight**, the confidence times the roughness fade times the intensity, not the bare
  confidence: past the roughness cutoff, where the reflection is gone, the light's specular comes back whole.
- **The light record's `twoSided` became `flags`**, bit 0 two-sided and bit 1 the drawn surface (`LightFlag` in C++,
  `FenrirLightTwoSided` and `FenrirLightVisibleSurface` in `Fenrir/LightRecord.glsl`), so the record stays 80 bytes.
- **`HasReflections`** (enabled, with an intensity and a max distance above 0) now also decides whether ticket 29's
  textures and passes exist, as `HasAmbientOcclusion` does for GTAO, so off at intensity 0 costs nothing.
- **`reflectionInfo` is a `vec4`** of floats (on, intensity, cutoff, the history's last mip), and is set from the camera's
  settings even when the history could not be had that frame (a second view of one camera); the stand-ins' zero
  confidence keeps that harmless.
- **After `/code-review`:** ADR-0056 now says rect lights yield by the reflection's share and that `reflectionInfo`
  holds the history's last mip; ADR-0019 states the graph's read rule and `SamplePrevious`. A hit behind last frame's
  camera (negative w) no longer passes the on-screen test. Kept as they are, for the reviewer: `m_drawn` (a probe seam
  that also makes `RendererProbe::SceneColour` skip hit targets), `ViewGroupFor`'s seven parameters, and the tests'
  copied `Labels`/`Samples`/`Close` helpers, which every renderer test file repeats.
