# 04: Materials light through `Fenrir/Lighting.glsl`, the sky lights the scene, and `AmbientLight` is gone

**What to build:** The lighting moves out of `lit.frag` into a shared include, set 0 grows for it, and the sky is filtered into image-based lighting that replaces `AmbientLight` (spec: "Lights and the sky", "Materials"; ADR-0047, ADR-0048).

- **`Fenrir/Lighting.glsl`** with `FenrirSurface` and one lighting function; `builtin:lit` becomes its first user, its output unchanged for the directional light.
- **Sky filtering passes**, cached by the sky's kind, image, revision and rotation (never by `intensity`): L2 spherical harmonics projected from the sky's cubemap; a prefiltered GGX cubemap by importance sampling with PDF-based source-mip filtering, one roughness step per mip; a BRDF lookup table (RG16F) made at start-up. A `ColourSky` gives a constant irradiance and a flat specular colour.
- **Set 0 grows** for the coefficients, the prefiltered cubemap and the lookup table; the C++ layout and the debug reflection check follow.
- **`AmbientLight` is removed** from C++, the API description and every demo, which move to their `Sky`. Its C# mirror stays as a deprecated no-op until ticket 22.
- **A camera's `ColourBackground`** changes only what is drawn behind the scene.

**Blocked by:** 03

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `examples/RendererDemo/`, `examples/AnimationDemo/`, `examples/PhysicsDemo/`, `examples/ScriptingDemo/`

**Status:** ready-for-agent

- [ ] CPU unit tests: SH projection of a constant and of a one-sided sky against analytic irradiance.
- [ ] GL pixel probes: a white rough sphere under a constant white sky gives about 1; a mirror sphere reflects the sky's faces.
- [ ] `RendererProbe` on null: filtering runs once per sky change, not when `intensity` changes; two cameras of one scene share it.
- [ ] Golden images: the sphere grid's rows under one panorama.

Hand check: metals reflect the panorama, rough dielectrics take its colour, and RendererDemo's scenes look as before under their `ColourSky`.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
