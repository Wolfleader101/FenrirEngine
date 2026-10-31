---
status: proposed
---

# Shadows are stable cascades and pooled atlases with a budget, and skinned meshes skin once per frame in compute

ADR-0019 reserved the `Shadow` stage. This fills it, choosing current techniques over LearnOpenGL's single fitted map, 3×3 PCF and geometry-shader cubemaps, and it amends ADR-0035, because shadows draw a skinned mesh many times a frame.

## What casts

- **Lights opt in** through `castShadows`: on by default for the `DirectionalLight`, off for point and spot lights. Rect lights cast none (ADR-0048).
- **Meshes opt out** through `MeshRenderer`'s and `InstancedMeshRenderer`'s `castShadows`, on by default. `Masked` materials cast with their alpha cutoff; `AlphaBlend` and `Additive` ones never cast.
- **A budget, not a cap on lights.** `RendererConfig` sets how many point and spot lights may be shadowed (4 and 8 by default). The rest draw unshadowed, warned about once; which ones keep their shadows is chosen by distance to the view each frame.

## How

- **The sun uses cascaded shadow maps**, up to four per view (a cascade depends on the camera). Splits follow the practical scheme, a log and linear blend set by a lambda, and neighbouring cascades are blended across their boundary. Each cascade is a bounding sphere snapped to whole shadow-map texels, so shadows do not shimmer as the camera moves or turns.
- **Spot shadows share a 2D atlas; point shadows share a cubemap array.** Both are pools sized by the budget, not one texture per light.
- **No geometry shader for cubemaps.** Where the vertex stage can write `gl_Layer`, a point light's six faces draw as instances of one draw; otherwise as one draw per face.
- **Filtering is hardware depth comparison with Vogel-disk PCF**, rotated per pixel by interleaved gradient noise, for softer edges than a grid at the same tap count. Each light has a slope-scaled depth bias and a normal-offset bias. PCSS and VSM were set aside: PCSS can come later without any change users see.
- **Shadow passes instance** like every other pass (ADR-0051).

## Compute skinning

With the prepass, cascades and cubemap faces, one skinned mesh can be drawn ten times a frame. A compute pass at the start of the frame skins each skinned mesh once into a per-frame vertex buffer from ADR-0035's palette buffer, and every pass then draws that buffer rigidly. The `SKINNED` vertex variant stays for user shaders and as the fallback with compute skinning off. Palette upload, deduplication and the skip cases are unchanged.
