# 07: LightingDemo opens with the material grid and the showcase models, and demo assets are fetched at configure time

**What to build:** The new demo app, its first two scenes, and the `FENRIR_FETCH_DEMO_ASSETS` download path (spec: "LightingDemo"; ADR-0052).

- **`FENRIR_FETCH_DEMO_ASSETS`** (off by default): a manifest of URL, SHA-256 and licence per file; downloads at configure time into a cache outside the build directory; a hash mismatch fails configure with the file named.
- **LightingDemo** at `examples/LightingDemo/` through `fenrir_add_demo_app`, on the free-fly camera, with the ImGui panel for exposure, tonemapper, bloom and the sky, an `ATTRIBUTION.md` and a README per scene.
- **Scene 1, the material grid:** spheres varying roughness and metallic under one committed ~2 MB Poly Haven panorama (CC0).
- **Scene 2, showcase models:** Khronos DamagedHelmet, FlightHelmet and SciFiHelmet, fetched, with a panorama switch among two or three fetched Poly Haven skies.
- **A missing fetched asset** shows an on-screen message naming the option, and the scene is otherwise empty; the smoke runs pass without them.

**Blocked by:** 05, 06

**Touches:** `examples/LightingDemo/`, `examples/CMakeLists.txt`, `cmake/`, `examples/DemoScaffold/`

**Status:** ready-for-agent

- [ ] Configure tests: a manifest entry with a wrong hash fails; a cached file is not downloaded again.
- [ ] LightingDemo's smoke runs pass on null with no fetched assets.

Hand check: with assets fetched, the helmets look right under each sky; the grid matches LearnOpenGL's IBL result in spirit.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
