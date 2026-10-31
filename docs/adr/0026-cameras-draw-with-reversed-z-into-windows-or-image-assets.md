---
status: accepted
---

# Cameras draw with reversed-Z into windows or image assets

Game code makes a view with a `Camera` component on an entity, oriented by the entity's world transform (looking along local −Z, +Y up). Every enabled camera in the active scene draws one view each frame (ADR-0019), in its `order`. There is no single active camera. Two parts of this are hard to change later and would surprise a reader without the reasons, so they are recorded here.

## Depth is reversed-Z, and perspective has no far plane

- **Reversed-Z in [0,1].** Every projection maps the near plane to depth 1 and the far plane to 0, with a [0,1] clip range (GL 4.5 clip control, already required by ADR-0016). The depth test is greater-or-equal, depth targets clear to 0, and the `Background` pass draws at depth 0.
- **Perspective uses an infinite far plane.** Its only depth setting is `near` (default 0.1 m). Orthographic projections keep a finite `near` and `far`.
- **Why:** with a floating-point depth target, reversed-Z spreads precision evenly over distance, so large outdoor scenes don't z-fight far away and need no tuned far plane. Vulkan, D3D12 and Metal all take a [0,1] range natively. Every shader, depth test, depth read-back in tests and the screen-to-world helpers assume this convention, so switching later would touch all of them.

## Render targets are ordinary image assets

- **A camera's `target` is a window or an `AssetHandle<Image>`.** The main window is the default. An image target is a FenrirAssets `ImageData` with a size and format but no pixels, made through the store like any image (ADR-0021). Its format is RGBA8 sRGB, which is what `Output` writes to a window.
- **Sampled like any texture.** A material's texture parameter, or a UI library's overlay (such as an editor's scene view), names the same handle. The render graph orders the view that writes it before any pass that reads it (ADR-0019). A view whose own draws sample its target gets the default texture for them instead, and the renderer logs it once.
- **Resized through the store.** `Replace` with a new size bumps the revision, and the renderer recreates the GPU texture, as for any changed asset.
- **Why:** this is Bevy's model. One handle type serves every texture use, so materials, the overlay and C# need nothing new. A GPU-only resource in an asset store that otherwise holds CPU data is the cost. A view never writes into a texture that the store doesn't know about.

## Considered options

- **Standard Z with a finite far plane.** Rejected: it is simpler to read, but it wastes precision far from the camera and makes every scene pick a far plane.
- **Reversed-Z with a finite far plane for perspective.** Rejected: nothing in the foundation needs a far cut-off, and an infinite projection keeps the setting count down. Culling at a distance, if it comes, is a separate setting.
- **A renderer-owned `RenderTexture` handle** (Unity's `RenderTexture`, Godot's `SubViewport`). Rejected: materials, the overlay and C# would each need a second texture type, or a bridge from it to `AssetHandle<Image>`.
- **One active camera** (the old `FenrirCamera` and the DemoApp's `DemoCamera`). Rejected: split screen, picture in picture and cameras drawing into textures all need several views a frame.

## When to revisit

If a backend or a target platform lacks [0,1] clip control or floating-point depth, or if keeping GPU-only images in FenrirAssets starts to complicate its loading and ownership rules.

*Amended by ADR-0047:* a camera's `ColourBackground` replaces only the backdrop; every view is lit by the scene's `Sky`. `Camera` gains `exposure`, `tonemapper`, ambient occlusion and bloom settings (ADR-0049).
