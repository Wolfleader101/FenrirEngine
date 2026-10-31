# 03: `PanoramaSky` draws an HDR panorama as the sky

**What to build:** A third `Sky` kind takes a flat equirectangular HDR image, converted on the GPU to a cubemap once per image revision; `CubemapSky` and `PanoramaSky` gain `intensity` and `rotation` (spec: "Lights and the sky"; ADR-0047).

- **`PanoramaSky { image, intensity, rotation }`** in the `Sky` union; an image that is not flat or not twice as wide as high is warned about once and the sky falls back to the default colour.
- **A render graph pass converts the panorama** into a cached cube texture (face size from the image's height, with mips), redone only when the image or its revision changes. `Background` draws it like a `CubemapSky`.
- **`intensity` and `rotation`** (about world +Y) apply to both cubemap and panorama skies.
- The API description gains the new kind (generated output only).

**Blocked by:** 01

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`

**Status:** resolved

- [x] `RendererProbe` on null: the conversion runs once per revision and again after the image changes.
- [x] GL pixel probes: a synthetic panorama with a distinct colour per direction lands on the right cube faces; `rotation` turns it.
- [x] Golden image: a panorama sky behind a lit sphere.

Hand check: a Poly Haven panorama looks seamless when flying around under it.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-31. Decisions to review:

- **Conversion is a fragment pass per face and mip level, not compute.** The GL backend has no compute pipelines or storage images yet, so the `Panorama` passes draw `fullscreen.vert` + `panorama.frag` into one level of one face each (6 x mips passes, once per revision). For that, `ColourAttachment` gains `mipLevel` and `layer`, the graph gains `PassBuilder::ColourAttachmentLevel`, and GL attaches a cube face with `glNamedFramebufferTextureLayer` (its framebuffer cache now keys on name, level and layer).
- **Every level is filtered from the panorama, not from the level above.** Level k samples the panorama's mip whose texels are as tall as its own (`log2(H / (2 * side))`, so k), with four points across each texel. This needs no mip generation on the GPU and no reading of one level while writing the next (a feedback loop under GL). Ticket 04's prefilter can read the cube's mips directly. Near the poles the panorama's texels are narrower than tall, so coarse levels can alias a little there.
- **The panorama's own mips are made on the CPU**, by the same `WriteWithMips` every image uses. ADR-0047 rejects CPU *conversion*; this is only the upload's mip chain, but for a 4k `.hdr` it is a noticeable one-off cost per revision. A GPU downsample would remove it once the device can generate mips.
- **The cube texture:** RGBA16F (half the memory of RGBA32F; texels over 65504, a bright sun's core, are clamped rather than made infinite), faces half as wide as the panorama is high (as many texels round the horizon as the panorama has), every mip level. The panorama's own texture, the passes' uniform blocks and groups are freed the frame after the passes ran.
- **Panorama orientation:** the image's centre looks along -Z, its right half turns towards +X (seen from inside, not mirrored), its top row looks up. `rotation` is in radians, counter-clockwise seen from above (right-handed about +Y), so a quarter turn moves what was ahead to the left.
- **`intensity` and `rotation` live in the frame block**, a new `vec4 sky` (intensity, cos, sin) after `time`, with `FenrirSkyDirection()` in `Fenrir/Frame.glsl`. The sky is per scene, and ticket 04's lighting needs the same two values, so the Background pass's CUBEMAP variant now binds set 0. `FrameData` grows from 64 to 80 bytes.
- **A new `RenderStage::Prepare`**, first, for work kept across frames (the panorama passes; ticket 04's filtering). ADR-0049's stage list now names it. The Background pass does not declare a graph read of the cube texture and relies on stage order; ticket 04's filtering passes should import it and declare their reads.
- **Validation:** any flat image with pixels and width exactly twice its height is accepted, LDR included; anything else (square, cubemap, empty, stale) draws dark grey with one warning per handle, sharing the cubemap's log (`skyCubemaps` became `skyImages`). If `builtin:panorama` fails to load, a panorama sky draws dark grey rather than an empty cube.
- **Cube textures live until their image goes stale**, as a `CubemapSky`'s always did, even after the Sky names another image. For panoramas that is a full RGBA16F cube each; a sweep of images no Sky names would be a follow-up.
- **Tests:** null probes for the passes' shape and order and for once-per-revision conversion; GL probes for the six directions, rotation (panorama and cubemap) and intensity; golden `panorama-sky.png` (3 KB, a panorama made in code: blue sky, warm horizon, brown ground, a sun at 40). `RendererTest::Sphere`, `RendererTest::Panorama` and `GlScene::ReadTarget` are now shared test helpers (the tonemapping goldens use them, unchanged).
- **C#:** only the generated output (`PanoramaSky.cs`, `Sky.cs`, `SkyType.cs`, `CubemapSky.cs`, mappings, defaults test). Hand-written C# for the new kind is ticket 22.
- **Review** (`/code-review`, standards and spec): no wrong maths found. Fixed from it: the GL rotation and intensity checks split into two tests with an `INFO` on every check, the null conversion test split in two, one shared panorama builder, `SceneSky::image` (it holds either kind), one read of intensity and rotation in `ReadSky`, and the empty-pipeline fallback.

Hand check still to do: a Poly Haven panorama flown around in a demo (none uses `PanoramaSky` yet; LightingDemo arrives with ticket 07).
