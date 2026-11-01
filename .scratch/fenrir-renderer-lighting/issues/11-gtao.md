# 11: Ground-truth ambient occlusion from the prepass

**What to build:** GTAO darkens indirect light where geometry occludes it (spec: "The frame"; ADR-0049).

- **GTAO** after Intel's MIT-licensed XeGTAO (licence recorded): horizon search on the prepass's depth and normals (sample 0 under MSAA), a spatial denoise, and a per-view occlusion texture.
- **`Fenrir/Lighting.glsl` multiplies indirect diffuse by it**, and indirect specular by a specular-occlusion term; direct light never.
- **`Camera`'s `ambientOcclusion { enabled, radius, intensity }`**; disabled culls the passes.
- The API description gains the settings (generated output only); the demo panel gets a toggle and a view of the raw occlusion.

**Blocked by:** 10

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `examples/LightingDemo/`

**Status:** resolved

- [x] `RendererProbe` on null: GTAO passes run only for cameras that enable it.
- [x] GL pixel probes: an inside corner is darker than a flat floor; a flat floor alone is unoccluded.
- [x] Golden image: the material grid with GTAO.

Hand check: contact shadows under the helmets, no haloing at silhouettes.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-11-01, stacked on ticket 10's branch (#265 to #269 not yet merged). Decisions to review:

- **The port.** `builtin:gtao` follows XeGTAO's `XeGTAO.hlsli` (commit `a5b1686c`, MIT, recorded in
  `Resources/XeGTAO`): its main pass (4 slices of 3 steps, its default radius multiplier 1.457, falloff, sample
  distribution, Hilbert-curve R2 noise at frame 0) and its denoise, run **twice** (XeGTAO's "medium" setting, as nothing
  filters it over time; one pass left visible grain). Left out: the depth MIP chain, temporal noise, bent normals.
  Depth and normals are read with texelFetch, sample 0 under MSAA. Orthographic views look along -Z.
- **Where the occlusion binds:** set 1 (the view) binding 1, `fenrirAmbientOcclusion` in `Fenrir/View.glsl`. Opaque binds
  the view's occlusion; every other pass, and every view without GTAO, the 1 x 1 white texture, so transparent items are
  never darkened by what is behind them. View groups are now made by SceneRenderer (block + texture), not the ring.
- **`min`, not a product.** Lighting.glsl darkens the sky's diffuse by `min(material occlusion, GTAO)` and its specular by
  the Lagarde specular occlusion of that, as Filament does, so occlusion a texture bakes and GTAO also finds does not
  darken twice. Direct lights are untouched.
- **Settings:** `Camera::ambientOcclusion { enabled = false, radius = 0.5, intensity = 1 }`. Off by default, so no
  existing golden or pass-list test changes. Intensity scales XeGTAO's final power 2.2.
- **The raw view needed an API addition:** `Camera::debugView` (`CameraDebugView::None`, `AmbientOcclusion`), which has
  Output draw the occlusion as grey (Output's new OCCLUSION variant) and so culls the lit passes. Ticket 15's shadow debug
  views can add values to it. Beyond the spec's Camera list; the user chose to keep it (2026-11-01).
- **The golden** is `sphere-grid-gtao.png`: the ticket-04 sphere grid resting on a floor against a wall, built in the GL
  test, since tests never use the demo's assets and the demo's floating material grid shows almost no occlusion.
- **Hand check** (helmets' contact shadows, no haloing) is not done in the demo: Showcase models needs the fetched
  assets and a person at the screen. The golden scene was checked by eye at 960 x 540 (lit and raw): contact shadows
  under the spheres and along the wall's foot, no halo at the spheres' silhouettes, some noise grain.
