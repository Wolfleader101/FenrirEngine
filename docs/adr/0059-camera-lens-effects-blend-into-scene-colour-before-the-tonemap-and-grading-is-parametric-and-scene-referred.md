---
status: accepted
---

# Camera lens effects blend into SceneColor before the tonemap, and grading is parametric and scene-referred

ADR-0049 ends the frame with bloom in `PostProcess` and exposure and a tonemapper in `Output`. Tuning the neon city (lighting ticket 20) against cyberpunk reference renders asked for what a camera adds on top: anamorphic streaks, lens flare, chromatic aberration, vignette, film grain and a colour grade (lighting tickets 25 to 28). Each is small; what needs deciding once is where each goes relative to the tonemap, whether the lens effects keep bloom's promise to move light and never add it, and how a grade is described. This amends ADR-0049.

## The order

`PostProcess`, on linear HDR SceneColor, after GTAO and before `Output`:

1. **Bloom** (ADR-0049), unchanged.
2. **Anamorphic streaks**, a horizontal-only chain from bloom's first level.
3. **Lens flare**, ghosts and a halo from a small bloom level, and lens dirt scaling bloom's and the flare's share.

`Output`, per pixel:

1. **Chromatic aberration**, while reading (and under MSAA resolving) SceneColor, since it moves where each channel is read from.
2. **Exposure** (ADR-0049).
3. **Vignette**, a multiply in linear light, as a lens's falloff is.
4. **Colour grade**, scene-referred.
5. **Tonemap** (ADR-0049).
6. **Film grain**, display-referred, on the tonemapped colour before the target's sRGB encoding.

Optical effects (streaks, flare, aberration, vignette) happen to light before it reaches the film, so they come before the tonemap and the tonemapper rolls off whatever they brighten. Grain is the film's own, so it comes after, in the range the viewer sees, where its strength can be set once for any exposure.

## Lens effects blend, as bloom does

**Streaks and the flare are mixed into SceneColor as bloom is:** SceneColor becomes (1 - intensity) of itself and intensity of the effect, through the same alpha-blended composite. A tint is normalised to unit luminance. So, like bloom, they move light and never add it, there is no threshold (everything streaks and ghosts a little, only what is bright shows), and turning one on does not change the frame's exposure. Lens dirt scales the share per pixel, held at or below 1, so the mix stays a blend.

## Grading is parametric and scene-referred

**The grade is a handful of parameters on `Camera`, applied in linear Rec. 709 after exposure and before the tonemapper, in this order:** white balance (a von Kries adaptation in LMS), contrast in log2 around mid grey (0.18), split toning (a shadow and a highlight tint, each normalised to unit luminance, split by a balance in log2 around mid grey), saturation around luminance, and the ASC CDL's slope, offset and power per channel as the final trim. Split toning is the one display-era control kept, because it is what a two-tone look such as the neon city's teal and magenta is, and it works on scene values as contrast does. Grading before the tonemap is what Unreal and Unity's HDR pipelines do: contrast and saturation act on scene values, mid grey stays put, and the tonemapper still compresses the highlights, so a strong grade does not clip. Every control is a closed-form function, so it is mirrored on the CPU beside the tonemappers (`src/Core/Tonemapping.cpp`) and the GL tests hold the shader to the mirror, as they do the tonemappers. Identity is the default and costs nothing.

## Not yet

- **3D LUTs.** A look baked into a 3D texture, applied after the tonemap as a display-referred creative step, is how artists bring grades from DaVinci Resolve or Photoshop. It needs a 3D `TextureKind`, a LUT asset and a way to author one; it waits for an artist with a LUT to bring. When it comes, it sits after the tonemap and before grain, and the parametric grade stays.
- **Lift, gamma and gain**, and shadows, midtones and highlights wheels. They are display-referred controls with more to set and test; they come with the LUT stage, or as a log-space grade if split toning and the CDL prove short.
- **Post-process volumes**, regions of the world with their own settings blended as the camera moves, as Unity and Unreal have. The settings live on `Camera`; a game blends them between cameras or by area in its own code until a scene needs volumes.
- **Per-light starbursts and sprite flares**, placed by each light's screen position and visibility.
- **Auto-exposure** (ADR-0049's "left for later") and **depth of field** and **motion blur**, which need more than a full-screen pass.

## Considered

- **Additive streaks and flare**, as many engines add them. Without a threshold, adding a blur of the whole frame brightens the whole frame, so every change to the intensity is also an exposure change; with a threshold, the engine gains the threshold bloom gave up (ADR-0049). Blending keeps one rule for every blur. At the small intensities these effects use the two look much alike; if the neon city wants more punch from its streaks, a threshold on the streaks alone is the amendment to make.
- **Grading after the tonemap.** Simpler to reason about on a monitor, but contrast and saturation then act on compressed values and clip the highlights the tonemapper just saved. The display-referred step is the LUT's, later.
- **A LUT only**, with every parametric control baked into it each time a setting changes (Unity's approach). It makes the shader one lookup, but needs the 3D textures first, quantises the grade, and hides it from CPU tests behind a bake.
