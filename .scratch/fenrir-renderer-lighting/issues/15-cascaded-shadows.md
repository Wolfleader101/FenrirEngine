# 15: The sun casts stable cascaded shadows

**What to build:** The `Shadow` stage's first user (spec: "Shadows"; ADR-0050).

- **`DirectionalLight`'s `castShadows`** (on by default) and **`MeshRenderer`'s `castShadows`** (on by default); masked materials cast with their cutoff, blended ones never.
- **Built in two steps within the ticket:** one fitted map first, then up to four cascades per view with the practical split scheme (lambda in `RendererConfig`), boundary blending, and texel-snapped bounding spheres.
- **Filtering:** hardware comparison with Vogel-disk PCF rotated by interleaved gradient noise; slope-scaled and normal-offset biases per light.
- **Shadow passes instance**; skinned meshes cast through compute skinning; `InstancedMeshRenderer` casts by its own flag.
- **Debug views** in LightingDemo: cascade colours and the shadow map. The asteroid ring gains the planet's shadow.
- The API description gains the flags (generated output only).

**Blocked by:** 12, 14

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `examples/LightingDemo/`

**Status:** ready-for-agent

- [ ] CPU unit tests: split distances; a cascade's snapped sphere does not move its texel grid as the camera moves by less than a texel.
- [ ] GL pixel probes: a box's shadow on a plane is where the light says; no acne on a lit plane at grazing angles within the bias limits.
- [ ] Golden images: shadows across each cascade boundary.

Hand check: flying around the showcase models, shadows do not shimmer or swim.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
