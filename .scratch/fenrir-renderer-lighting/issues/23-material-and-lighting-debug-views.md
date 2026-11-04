# 23: Debug views of what the renderer shades with

**What to build:** Camera debug views that let a viewer inspect how FenrirEngine renders a scene, not only the finished frame. Requested by the user while reviewing ticket 16 (the forest clearing), 2026-11-01, as a debug overlay for the LightingDemo.

- **New `CameraDebugView` modes** beside the ambient occlusion, cascade and shadow-atlas views: albedo only (the surface's base colour, unlit), world normals, roughness, metallic, direct light only (the sun and punctual lights, no sky), and indirect light only (the sky's diffuse and specular, times occlusion).
- **Wireframe**: the scene's triangles drawn as lines over or instead of the lit frame, needing a line raster state in the device (GL `glPolygonMode`) or a barycentric fragment path.
- The material views must work for user material shaders too (the forest's grass and ground): through `Fenrir/Lighting.glsl`'s `FenrirSurface`, so a shader that fills one gets them without its own code.
- LightingDemo's Lighting panel lists every view, and G steps through them.

**Blocked by:** 16

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `examples/LightingDemo/`

**Status:** needs-triage

- [ ] GL pixel probes: each material view shows the probed surface's value (a known base colour, normal, roughness, metallic) unlit.
- [ ] GL pixel probes: direct plus indirect equals the lit view, within tolerance, before tonemapping.
- [ ] Null `RendererProbe`: the wireframe view records its line pipeline.

Hand check: in the forest clearing, each view reads as what it names; wireframe shows the trees' thinned levels of detail.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
