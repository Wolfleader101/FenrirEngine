# 30: Screen-space reflections: the trace and the lighting include's blend

**What to build:** Reflections of whatever is on screen, glowing geometry included, in every material that lights through `Fenrir/Lighting.glsl` (ADR-0056). Requested by the user while reviewing ticket 20 (the neon city), 2026-11-02.

- **`ReflectionTrace`**, a fullscreen fragment pass after `HiZ` (ticket 29): per pixel with depth, the mirror ray about the prepass normal, marched through the pyramid (Uludag's Hi-Z tracing, GPU Pro 5) for at most 64 iterations, a hit within the camera's `thickness`. It writes an RGBA16F hit target: the hit reprojected into the previous frame's UV, its confidence, and the ray's length. Confidence as ADR-0056 lists it: hit, edge and distance fades, facing, history validity, and disocclusion against last frame's pyramid.
- **`Fenrir/Lighting.glsl`** samples `fenrirSceneHistory` at the hit's UV, at the mip that spreads the hit over GGX's lobe for the surface's roughness and the ray's length, and blends it over the sky's prefiltered specular by the confidence times the roughness fade and the camera's intensity, through the same single-scattering share and specular occlusion. Rect lights' specular is scaled by 1 minus the confidence unless their `visibleSurface` is false; point and spot lights are untouched.
- **The view set gains** `fenrirReflectionHits` and `fenrirSceneHistory`, with 1 × 1 stand-ins (zero confidence) in every pass but `Opaque` and for views with reflections off; `ViewData` gains `reflectionInfo`. The C++ layout and its debug-build reflection check follow.
- **`Camera::reflections`** gains `intensity`, `maxDistance`, `thickness` and `roughnessCutoff` (defaults 1, 50, 0.5, 0.6); **`RectLight`** gains `visibleSurface` (true). FenrirRenderer's C# binding is regenerated for them, and the scripting round trip covers them.
- `packages/FenrirRenderer/README.md` says what the reflections catch and what they cannot: off-screen objects, one frame of lag, moving objects trailing.

**Blocked by:** 19, 29

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/README.md`

**Status:** ready-for-agent

- [ ] CPU unit tests of the trace's maths, mirrored in C++ as Core/LightClusters mirrors its shader: the reflected ray's screen-space end points, a Hi-Z march over a known pyramid finding a known hit and missing past `maxDistance`, the edge fade, the reprojection into the previous frame's UV, and the roughness-to-mip choice.
- [ ] GL pixel probes: a mirror floor in front of an emissive quad shows the quad's colour at the reflected pixel; with the quad behind the camera the floor shows only the sky; a rough floor past the cutoff matches it with reflections off; a rect light with a drawn board does not reflect twice, and one with `visibleSurface` false keeps its specular.
- [ ] A golden image: a mirror floor under emissive strips and a rect light, reflections on.
- [ ] Null `RendererProbe`: the trace pass follows HiZ, and Opaque binds the hit target and history.

Hand check: in the showroom, the polished floor shows the neon strip's and the helmets' reflections, sharp where the floor is smooth and blurred where rough, and they fade at the screen's edge rather than cutting off.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
