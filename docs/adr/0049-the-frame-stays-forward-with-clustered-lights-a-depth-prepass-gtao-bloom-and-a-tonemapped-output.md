---
status: proposed
---

# The frame stays forward, with clustered lights, a depth prepass, GTAO, bloom and a tonemapped output

The lighting family could have moved opaque drawing to deferred shading, as LearnOpenGL does next. It stays forward: a G-buffer cannot run ADR-0024's user fragment programs, and it breaks ADR-0019's MSAA. What deferred shading buys, many lights and screen-space effects, comes from clustered forward lighting and a depth prepass instead, as Doom 2016, Godot 4's Forward+ and Bevy do. This amends ADR-0019's stages and passes.

## Stages and passes

- **The stage list becomes** `Shadow, DepthPrepass, Opaque, Background, Transparent, Debug, PostProcess, Output, Overlay`.
- **`DepthPrepass`** draws every `Opaque` and `Masked` item (alpha-tested) into `SceneDepth` and a view-space normal target, at the view's MSAA count. `Opaque` then tests depth for equality and writes none, so no pixel is shaded twice. This is the depth prepass ADR-0019 deferred.
- **Ambient occlusion is GTAO** (Jimenez et al. 2016, after Intel's MIT-licensed XeGTAO), not LearnOpenGL's hemisphere-kernel SSAO, which is noisier and over-darkens. It reads the prepass's depth and normals (sample 0 under MSAA), denoises, and writes a per-view occlusion texture that `Opaque` samples for indirect light only. `Camera` turns it on and sets its radius and intensity.
- **Light clustering** is a compute pass per view: point, spot and rect lights are assigned to 16×9×24 view-space clusters with exponentially spaced depth slices, and `Fenrir/Lighting.glsl` loops over its pixel's cluster. Until it lands, the loop runs over the whole light buffer, which is laid out the same way, so materials never change.
- **Bloom** is in `PostProcess`: Jimenez's 13-tap downsample and tent upsample (CoD: Advanced Warfare), no threshold, mixed into `SceneColor` by `Camera`'s `bloomIntensity` (0.04 by default) and `bloomRadius`, before the tonemap.
- **`Output` tonemaps.** `Camera` gains `exposure`, in EV stops (0 by default, each +1 doubling), and a `tonemapper`: `None` (the old clamp), `AgX` (the default), `PbrNeutral` (Khronos PBR Neutral) or `Aces` (fitted). Reinhard is not offered. Auto-exposure is left for later. ADR-0019's placeholder clamp is gone.

## Testing

Shadows and post-processing cannot be pinned by analytic pixel probes, so this family adds golden-image tests on the real GL device: whole frames compared with committed small PNGs under a perceptual tolerance, regenerated only by an explicit script flag. Pixel probes stay wherever a result can be pinned exactly.

## Considered options

- **Deferred shading for opaque items**, with transparent items forward. Rejected: user shaders would become surface functions writing a G-buffer, MSAA would need per-sample lighting, and a forward path stays for transparency anyway.
- **Both paths, chosen per `RendererConfig`.** Rejected: every lighting feature written twice.
- **A plain light loop with no clustering.** Kept only as the first step: fine under about a hundred lights, too slow for the city scene's hundreds.
