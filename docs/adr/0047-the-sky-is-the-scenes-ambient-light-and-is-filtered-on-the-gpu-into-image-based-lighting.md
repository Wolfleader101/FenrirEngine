---
status: proposed
---

# The sky is the scene's ambient light, and is filtered on the GPU into image-based lighting

ADR-0024 lit `builtin:lit` with one directional light and a constant `AmbientLight`, and ADR-0026 gave each camera a background drawn from the scene's `Sky`. A constant ambient beside a skybox lights the scene twice, once with the wrong colour, so the `Sky` becomes the scene's only ambient light and `AmbientLight` is removed.

## The sky

- **Three kinds**, all in the `Sky` tagged union: `ColourSky { colour }`, `CubemapSky { cubemap, intensity, rotation }` and the new `PanoramaSky { image, intensity, rotation }`. A panorama is a flat, linear HDR image (an `.hdr` loaded as RGBA32F), width twice its height. `rotation` turns the sky about world +Y; `intensity` scales both what is drawn and the light it gives.
- **The renderer converts a panorama to a cubemap on the GPU**, once per image revision. FenrirAssets does no image processing; `ImageShape` is unchanged.
- **A `ColourSky` lights with its colour**, a constant irradiance: the old `AmbientLight` behaviour, now spelled as the sky. A scene with no `Sky` keeps the dim grey default.
- **A camera's `ColourBackground` changes the backdrop only.** Every view of a scene is lit by the same sky, so one object never lights differently in two cameras and IBL is made once per scene, not per camera.
- **A `SkyLight { sky }` lights the scene in the `Sky`'s place, leaving the `Sky` drawn.** *Added 2026-10-31 (lighting ticket 04):* an optional scene resource holding a sky of any kind, used only for its light; a scene without one is lit by its `Sky`. A skybox chosen for its look can tint everything its own colours (Darcy Wilson's teal sea turned RendererDemo's grey plinth teal), so it can be lit by a neutral `ColourSky` or a softer panorama instead. It is the mirror of `ColourBackground`, which changes what is drawn and not the light. One sky still lights the scene, so this is not the rejected `AmbientLight` beside the sky: nothing is lit twice. The frame block carries the drawn sky's intensity and rotation apart from the lighting sky's.

## Image-based lighting

- **Diffuse from L2 spherical harmonics** (nine RGB coefficients) projected from the sky's cubemap, as Filament and Unity do, not LearnOpenGL's irradiance cubemap: smaller, cheaper to sample, and indistinguishable for diffuse light.
- **Specular from a prefiltered cubemap**: GGX importance sampling with Karis's PDF-based source-mip filtering, one roughness step per mip, sampled at `roughness * (mips - 1)`.
- **A BRDF lookup table** (RG16F, scale and bias of the split sum), made once at start-up, also feeding multi-scatter energy compensation (ADR-0048).
- **Made on the GPU in render graph passes, cached across frames** and remade only when the `Sky` changes kind, image or image revision. Changing `intensity` never refilters. *Amended 2026-10-31 (lighting ticket 04):* nor does changing `rotation`, which this first listed: the coefficients and the prefiltered cube are kept in the cube's own frame, and the lighting include turns each lookup by the rotation, as the Background pass turns the sky it draws, so a turning sky costs nothing. Baking offline was rejected: it needs a tool pipeline and freezes the sky.
- **Ambient occlusion and `occlusionTexture`** darken the sky's diffuse light, and its specular through a specular-occlusion term; never direct light.

## Consequences

- `AmbientLight` leaves the C++ API, FenrirRenderer's API description and every demo. Its C# mirror stays as a deprecated no-op until the C# ticket removes it, so C# keeps compiling.
- The frame set grows to carry the sky's coefficients, cubemap and lookup table (ADR-0048).

## Considered options

- **Keep `AmbientLight` beside image-based lighting**, as a tint or fallback. Rejected: two sources of the same light, and the glossary already avoids *environment* for the sky.
- **CPU conversion of panoramas in FenrirAssets.** Rejected: the GPU passes exist for filtering anyway, and the asset store stays free of image processing.
