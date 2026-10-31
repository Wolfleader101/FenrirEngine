# 04: Materials light through `Fenrir/Lighting.glsl`, the sky lights the scene, and `AmbientLight` is gone

**What to build:** The lighting moves out of `lit.frag` into a shared include, set 0 grows for it, and the sky is filtered into image-based lighting that replaces `AmbientLight` (spec: "Lights and the sky", "Materials"; ADR-0047, ADR-0048).

- **`Fenrir/Lighting.glsl`** with `FenrirSurface` and one lighting function; `builtin:lit` becomes its first user, its output unchanged for the directional light.
- **Sky filtering passes**, cached by the sky's kind, image, revision and rotation (never by `intensity`): L2 spherical harmonics projected from the sky's cubemap; a prefiltered GGX cubemap by importance sampling with PDF-based source-mip filtering, one roughness step per mip; a BRDF lookup table (RG16F) made at start-up. A `ColourSky` gives a constant irradiance and a flat specular colour.
- **Set 0 grows** for the coefficients, the prefiltered cubemap and the lookup table; the C++ layout and the debug reflection check follow.
- **`AmbientLight` is removed** from C++, the API description and every demo, which move to their `Sky`. Its C# mirror stays as a deprecated no-op until ticket 22.
- **A camera's `ColourBackground`** changes only what is drawn behind the scene.

**Blocked by:** 03

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `examples/RendererDemo/`, `examples/AnimationDemo/`, `examples/PhysicsDemo/`, `examples/ScriptingDemo/`

**Status:** resolved

- [x] CPU unit tests: SH projection of a constant and of a one-sided sky against analytic irradiance.
- [x] GL pixel probes: a white rough sphere under a constant white sky gives about 1; a mirror sphere reflects the sky's faces.
- [x] `RendererProbe` on null: filtering runs once per sky change, not when `intensity` changes; two cameras of one scene share it.
- [x] Golden images: the sphere grid's rows under one panorama.

Hand check: metals reflect the panorama, rough dielectrics take its colour, and RendererDemo's scenes look as before under their `ColourSky`.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-31. Decisions to review:

- **Two new core modules.** `Core/SkyLighting` owns the BRDF lookup table, the sky's filtering passes and what set 0 binds; `SceneRenderer` hands it a `SkySource` each frame (the sky's cube texture, a Sky cubemap's or a panorama's, else a colour) and binds its textures. `Core/SphericalHarmonics` is the CPU mirror of `builtin:sky-irradiance`, which the CPU tests exercise, and gives a ColourSky its coefficients.
- **Rotation never refilters**, unlike the ticket's and ADR-0047's first wording ("cached by ... rotation"). The coefficients and the prefiltered cube stay in the cube's own frame and `Fenrir/Lighting.glsl` turns each lookup with `FenrirSkyDirection`, as the Background pass does, so a turning sky costs nothing. ADR-0047 is amended to say so. The cache key is the cube texture handle (a new revision is a new texture), or the colour.
- **Fragment passes, not compute**, as ticket 03's conversion: `SkyIrradiance` draws a 9 x 1 RGBA16F target, one coefficient a texel, each reading every texel of the cube's first mip no wider than 32 (with exact texel solid angles); `SkySpecular` draws each face of each mip of an RGBA16F cube no wider than 128 (or the sky's faces), roughness k / (levels - 1), 128 GGX samples a texel with Karis's PDF-based source mip (+1 bias), mip 0 a copy at its resolution; `BrdfLut` draws 128 x 128 RG16F once, in the first frame (512 samples a texel). They run in `Prepare` after the `Panorama` passes and declare their graph reads of the cube. ADR-0049's stage list already names `Prepare`.
- **The coefficients are a texture, not part of the frame block**: they come off the GPU, and a 9 x 1 texture avoids a texture-to-buffer copy the device lacks. The lighting include `texelFetch`es nine texels a pixel.
- **The BRDF table already uses height-correlated Smith** and stores Filament's (1 - Fc) V and Fc V, so ticket 05 can read multi-scatter compensation from it unchanged. Direct light keeps the separable Schlick-GGX term, so `builtin:lit`'s directional output is unchanged (the lit-face GL probe still passes); ticket 05 changes both together.
- **`FenrirSurface`** has base colour, alpha, normal, **world position** (the view direction needs it; ADR-0048's list omits it), metallic, roughness, occlusion and emissive; `FenrirLitColour(surface)` returns the lit colour with its alpha. `builtin:lit` passes occlusion 1 until ticket 05's `occlusionTexture`. Specular occlusion is Lagarde's from that occlusion. The sky's light is `(diffuse x occlusion + specular x f0-scale-and-bias x specular occlusion) x intensity`, diffuse `(1 - (f0 A + B)) (1 - metallic)`.
- **Set 0** is the frame block (64 bytes now: `ambient` is gone, the sky vec4's w holds the specular cube's highest mip), then `fenrirSkyIrradiance`, `fenrirSkySpecular` and `fenrirBrdfLut` at bindings 1 to 3, declared in C++ and checked in debug builds. **GL's flattening formula allows four textures a set, so set 0 has one left**: shadows, cluster lists and ambient occlusion (tickets 06 on) will need the formula widened or storage buffers.
- **A scene with no Sky now lights with the default ColourSky's dark grey (0.015)**, not `AmbientLight`'s 0.1: "keeps the dim grey default" read as keeping the default sky. Scenes that relied on the old default (none of the demos, which now set a ColourSky) look darker.
- **`AmbientLight` is gone from C++ and the API description.** C# keeps a hand-written `[Obsolete]` `AmbientLight` of the old layout, registered by FenrirRendererScripting against a private stand-in (`Scripting/src/DeprecatedAmbientLight.cpp`, outside the natives source, which the natives describer cannot parse with mappings in it), so scripts keep compiling and running; nothing reads it. Ticket 22 deletes both.
- **Demos:** AnimationDemo's stage and RendererDemo's Meshes and materials, Cameras and targets and Overlay scenes have a ColourSky of their old ambient colour, which also brightens their backdrop; Meshes and materials' M key (C++ and C#) now cycles the sky's grey (0.1, 0.4, black). PhysicsDemo (C++ and C#) and RendererDemo's Sky scene lose their AmbientLight and are lit by their skybox or dusk colour. RendererDemo's stripes shader takes the sky's diffuse light through `FenrirSkyIrradiance`.
- **Every first frame has a `BrdfLut` pass**, so five older pass-list expectations gained it.
- **Tests:** CPU SH projection of a constant sky, a sky lit only above the horizon (L2 is exact for it: pi (1 + cos t) / 2) and a ColourSky's coefficients; null probes for set 0's four bindings, filtering once into every face and mip before any view, no refiltering on intensity or rotation, refiltering on a new revision, two cameras (one with a ColourBackground) sharing it after the panorama conversion, the lookup table made once, and a ColourSky's CPU-written textures; GL probes for a white rough sphere under a constant white panorama and a white ColourSky (about 1) and a mirror sphere reflecting the +Z, +Y and +X faces; golden `sphere-grid-panorama.png` (red dielectric and gold rows, roughness 0 to 1).
- **msvc-x64-debug:** the whole CTest tree passes (166 tests, GL and demo smoke runs included; RTX 5080). The tonemapping goldens and `panorama-sky.png` were regenerated with `FENRIR_UPDATE_GOLDENS=1`: their spheres now take the sky's light (the panorama sphere's shadowed side fills in; the tonemap scene's gold no longer takes a diffuse ambient). FenrirShaderCompiler's flattening fixture includes `Fenrir/Frame.glsl`, so it counts the three sky textures and checks they flatten to set 0's texture units 0 to 2.

Hand check still to do: metals reflecting a real panorama, rough dielectrics taking its colour, and RendererDemo's scenes under their ColourSky.
