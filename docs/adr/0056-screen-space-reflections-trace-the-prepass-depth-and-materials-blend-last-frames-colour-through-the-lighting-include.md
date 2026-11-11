---
status: accepted
---

# Screen-space reflections trace the prepass's depth, and materials blend last frame's colour through the lighting include

The neon city (lighting ticket 20) showed that wet streets mirror only the analytic lights (rect, point and spot, through their specular lobes) and the sky's prefiltered cube. Glowing geometry is not reflected: the neon edge strips, the emissive sign boards and the lit windows. The user compared it with cyberpunk reference renders and asked for it to be fixed (2026-11-02). Only reflecting what is on screen can do that at this engine's scale, so the lighting family takes screen-space reflections (SSR), which its spec had left out. This amends ADR-0049's frame and ADR-0048's include.

## Why not a post pass

Deferred engines run SSR after lighting, as a pass that reads a G-buffer's normal, roughness and F0 and adds the reflection on top. ADR-0049 keeps the frame forward: user fragment programs work out their own normal, roughness and metallic in `Opaque`, and nothing before `Opaque` knows them. The `DepthPrepass` runs `builtin:depth-prepass` behind each item's vertex stage and writes depth and an octahedral view-space normal, from the vertex normal, not the material's normal map. A post pass would have to guess roughness and F0, or add a G-buffer-like target to every user shader.

So the work splits as Unity's forward SSR and Godot 4's Forward+ split it: the ray is traced before `Opaque` from what the prepass knows (depth and normal), and each material, in `Opaque`, decides how much of the hit to use from what only it knows (roughness, F0, Fresnel).

## The passes

Per view, when its camera has reflections on:

- **`HiZ`**, in the `DepthPrepass` stage after the prepass: a hierarchical-Z pyramid of `SceneDepth`, each texel the nearest depth of the texels it covers (the largest, depth being reversed-Z), with the odd row and column folded in when a level's size is odd. It is a colour texture with a full mip chain, drawn level by level by fragment passes into each mip, as the bloom chain draws its levels. (ADR-0057 shares it with occlusion culling: it is RG32F, the green channel the farthest depth, under MSAA the nearest and the farthest of every sample, every camera keeps one, and a reflecting camera builds it twice a frame, the trace reading the second, complete build.) **Not compute:** the device has compute dispatch over storage buffers (light clusters, skinning) but no storage images, and fragment passes into mips need little new device work: a binding that samples one mip, so a pass can read the level above the one it draws without a feedback loop (lighting ticket 29).
- **`ReflectionTrace`**, after `HiZ`: a fullscreen fragment pass at the view's size. For each pixel with depth it rebuilds the view-space position, reflects the view ray about the prepass normal, and marches the mirror ray through the pyramid (Uludag's Hi-Z tracing, GPU Pro 5), stepping down a level on a hit and up on a miss, at most 64 iterations. A hit is a pyramid texel the ray passes behind by less than the camera's `thickness`. It writes an RGBA16F hit target: the hit's UV in the **previous** frame's view, its confidence, and the ray's length. The pass reprojects the hit itself, with the previous frame's view-projection in its own block, so `Opaque` never reads depth, which it is testing against at the time.
- **`SceneHistory`**, at the start of `PostProcess`, before bloom: `SceneColor` (every opaque, background and transparent item, before bloom glows over it and before exposure) is copied into the view's history texture, RGBA16F at the view's size, a plain average of the samples under MSAA, then box-filtered down its mip chain. Not bloom's first level: it is at half size and weighted by the Karis average, which darkens exactly the bright neon the reflections are for.
- **In `Opaque`, `Fenrir/Lighting.glsl`** reads the hit target at its pixel. With confidence above zero, it samples the history at the hit's UV, at the mip that spreads the hit over GGX's lobe for the material's roughness at that ray length (a cone of the lobe's width, as Unity HDRP and Godot do), and blends it over the sky's prefiltered specular by the confidence, times the same single-scattering share and specular occlusion the sky's specular takes. Every material that calls `FenrirLitColour`, user shaders included, gets reflections with no code of its own.

The history and the pyramid live across frames, so the renderer keeps them per camera, imported into each frame's graph by `ImportTexture` as the shadow atlases are, two pyramids ping-ponging (see disocclusion); a camera without reflections keeps one pyramid, for occlusion culling (ADR-0057), and no history. They are freed when the camera goes, and remade, with history marked invalid, when its size or MSAA changes.

## Confidence

The hit's confidence is the product of:

- **Hit:** 0 when the march finds nothing within the camera's `maxDistance` or its iterations, or the ray leaves the screen or points at the camera.
- **Edge fade:** falling to 0 as the hit nears the screen's edge, and as the ray's length nears `maxDistance`, so reflections do not end in a line.
- **Facing:** 0 where the hit surface faces away from the ray (its prepass normal), which would be the back of something.
- **History:** 0 on the frame the history is invalid (the camera's first frame, a resize, a cut the camera reports), and where the hit's reprojected UV is off the previous frame's screen.
- **Disocclusion:** the hit's reprojected depth compared with the previous frame's pyramid at level 0. A hit that was hidden last frame reads whatever hid it, so the confidence falls by the difference.

In `Opaque` the material multiplies in a **roughness fade**, 1 below the camera's `roughnessCutoff` less 0.1 and 0 at it, so rough surfaces keep the sky's prefiltered specular, which is smoother than a blurred screen.

## The decisions

- **One frame of lag.** Reflections show the previous frame's colour, as Unity HDRP's and Godot's do: the frame being drawn has no colour yet when `Opaque` runs. The camera's motion is undone by the reprojection. Moving objects are not, since the engine has no motion vectors: a car's reflection trails it by a frame, and the disocclusion test hides most of the ghost. Motion vectors are left for when temporal anti-aliasing needs them.
- **Double counting with rect lights.** A neon sign is a `RectLight` and an emissive board drawn in its place. With SSR both would reflect: the light through its LTC specular and the board through the screen. On screen, the screen is the better of the two (it is occluded, and it shows the board's own texture), so a rect light's specular yields to the reflection: it is multiplied by 1 minus the reflection's share (its confidence times the roughness fade and the camera's intensity, the weight the sky's specular gives way by), its diffuse untouched. `RectLight` gains `visibleSurface`, true by default, which says the scene draws the light's surface; a rect light with no drawn surface sets it false and keeps its specular everywhere. Point and spot lights keep theirs: their glints are small and their bulbs, when drawn, are smaller still.
- **Off screen** the reflection falls back to what the engine has today: the sky's prefiltered specular and the analytic lights' specular, by 1 minus the confidence. A sign behind the camera still lights the street through its LTC specular; its board is not seen in it.
- **Full resolution.** The trace runs at the view's size. A half-size trace needs a filter that upsamples hit UVs across depth edges without smearing thin strips, and without temporal accumulation it shimmers on exactly the one-pixel neon edges this is for. Half resolution, with temporal accumulation, is left for later.
- **Cost:** the target is 1.5 ms for the three passes at 1920 × 1080 on the developer's GPU in the neon city, measured once with an external profiler (RenderDoc's or Nsight's per-draw timings) and noted in the ticket; over it is called out, not a gate. The renderer has no GPU timer queries, and per-pass GPU timing is left for a design of its own. The pyramid is about a third more than the depth; the trace is bounded by its iterations; the history copy is one fullscreen pass and its mips. Memory per view at 1080p: about 22 MB for the history with its mips, 22 MB for the two pyramids and 17 MB for the hit target, about 60 MB in all.
- **Build order:** the three tickets stack straight after the neon city (lighting ticket 20), and the camera post-effects tickets (lighting 25 to 28) stack on top of them.

## The interface

- **`Camera` gains `reflections { enabled, intensity, maxDistance, thickness, roughnessCutoff }`**, off by default as ambient occlusion is, with off costing nothing: no passes, and the include's binding is a 1 × 1 target of zero confidence. Defaults: intensity 1, max distance 50 world units, thickness 0.5, roughness cutoff 0.6.
- **`CameraDebugView` gains `Reflections`**: the reflected colour times its confidence, exposed and tonemapped, black where nothing is reflected; and **`ReflectionConfidence`** as grey.
- **The view set (set 1) gains two bindings:** `fenrirReflectionHits`, the hit target, and `fenrirSceneHistory`, the history with its mips. `ViewData` gains `reflectionInfo`: whether the view has them, the intensity, the roughness cutoff, and the history's last mip. Both bindings get 1 × 1 stand-ins in every pass but `Opaque`, as the occlusion texture does, so transparent items reflect nothing they are in front of.
- **C#** reaches the camera's settings, the debug views and `RectLight::visibleSurface` through FenrirRenderer's generated binding, which each ticket regenerates when it changes a header, as every lighting ticket did; each adds its fields to the scripting round trip that lighting ticket 22 wrote (`Scripting/Tests/Lighting.cpp`).

## Considered options

- **A post pass over a G-buffer-like target**: the prepass, or `Opaque` through a second output, writes roughness and F0, and SSR runs after `Opaque` as deferred engines do. Rejected: every user shader would have to write the target, ADR-0048's include is no longer enough, and `Opaque`'s output grows for every pixel to serve a few wet ones.
- **Tracing inside `Opaque`**, in `Fenrir/Lighting.glsl`, with the material's own normal and roughness. It is the most correct, the normal map's ripples included, and skips rough pixels. Rejected for now: a 64-step march inside the include raises the register count of every material's shader, reflective or not, and cannot run at any other resolution. The neon city's puddles take the vertex normal anyway (the street shader gives the normal map way to the water's level surface).
- **Sampling bloom's first level as the history.** Rejected: half size and Karis weighted, so the reflection of a bright strip comes out dim and soft.
- **A linear march over the depth buffer**, as LearnOpenGL's readers and older engines do. Rejected: a fixed step either misses thin strips or costs many more samples for long rays; Hi-Z skips empty space.
- **Planar reflections** for the street's plane, as some racing games draw. Rejected: a second view of the scene for one plane, which gives nothing on other surfaces, and the city's streets are not one plane.
- **Reflection probes** (baked or real-time cubemaps per area). Left for later: they would catch what is off screen, and are the natural fallback between SSR and the sky, but need placement, blending and baking that this fix does not.
