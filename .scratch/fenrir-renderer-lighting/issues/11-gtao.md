# 11: Ground-truth ambient occlusion from the prepass

**What to build:** GTAO darkens indirect light where geometry occludes it (spec: "The frame"; ADR-0049).

- **GTAO** after Intel's MIT-licensed XeGTAO (licence recorded): horizon search on the prepass's depth and normals (sample 0 under MSAA), a spatial denoise, and a per-view occlusion texture.
- **`Fenrir/Lighting.glsl` multiplies indirect diffuse by it**, and indirect specular by a specular-occlusion term; direct light never.
- **`Camera`'s `ambientOcclusion { enabled, radius, intensity }`**; disabled culls the passes.
- The API description gains the settings (generated output only); the demo panel gets a toggle and a view of the raw occlusion.

**Blocked by:** 10

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `examples/LightingDemo/`

**Status:** ready-for-agent

- [ ] `RendererProbe` on null: GTAO passes run only for cameras that enable it.
- [ ] GL pixel probes: an inside corner is darker than a flat floor; a flat floor alone is unoccluded.
- [ ] Golden image: the material grid with GTAO.

Hand check: contact shadows under the helmets, no haloing at silhouettes.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
