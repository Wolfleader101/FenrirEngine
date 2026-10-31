# 07: LightingDemo opens with the material grid and the showcase models, and demo assets are fetched at configure time

**What to build:** The new demo app, its first two scenes, and the `FENRIR_FETCH_DEMO_ASSETS` download path (spec: "LightingDemo"; ADR-0052).

- **`FENRIR_FETCH_DEMO_ASSETS`** (off by default): a manifest of URL, SHA-256 and licence per file; downloads at configure time into a cache outside the build directory; a hash mismatch fails configure with the file named.
- **LightingDemo** at `examples/LightingDemo/` through `fenrir_add_demo_app`, on the free-fly camera, with the ImGui panel for exposure, tonemapper, bloom and the sky, an `ATTRIBUTION.md` and a README per scene.
- **Scene 1, the material grid:** spheres varying roughness and metallic under one committed ~2 MB Poly Haven panorama (CC0).
- **Scene 2, showcase models:** Khronos DamagedHelmet, FlightHelmet and SciFiHelmet, fetched, with a panorama switch among two or three fetched Poly Haven skies.
- **A missing fetched asset** shows an on-screen message naming the option, and the scene is otherwise empty; the smoke runs pass without them.

**Blocked by:** 05, 06

**Touches:** `examples/LightingDemo/`, `examples/CMakeLists.txt`, `cmake/`, `examples/DemoScaffold/`

**Status:** resolved

- [x] Configure tests: a manifest entry with a wrong hash fails; a cached file is not downloaded again.
- [x] LightingDemo's smoke runs pass on null with no fetched assets.

Hand check: with assets fetched, the helmets look right under each sky; the grid matches LearnOpenGL's IBL result in spirit.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-31, stacked on ticket 06's branch (#265 not yet merged). Decisions to review:

- **`cmake/FenrirDemoAssets.cmake`**, `fenrir_fetch_demo_assets(MANIFEST <json> CACHE_DIR <dir>)`, included at the root; the option and `FENRIR_DEMO_ASSET_CACHE` (a cache `PATH`, default `%LOCALAPPDATA%/FenrirEngine/demo-assets`, else `$XDG_CACHE_HOME` or `~/.cache`) sit beside the other options. A cached file whose SHA-256 matches is kept; any other is downloaded into `<file>.part`, hashed, and renamed into place only if it matches, so a wrong or interrupted download never stands in the cache. A mismatch is `FATAL_ERROR` naming the manifest path, the URL, both hashes and the manifest. The manifest is a `CMAKE_CONFIGURE_DEPENDS`.
- **The manifest is JSON** (`examples/LightingDemo/fetched-assets.json`, read with `string(JSON)`): `{"assets": [{"path", "url", "sha256", "licence"}]}`, licence as an SPDX expression. 33 files, about 100 MB: the three helmets' glTF folders from glTF-Sample-Assets at `8f492390`, BoxTextured's pin, and three 2k Poly Haven panoramas. One manifest per ADR-0052, kept beside its only user; a later demo with fetched assets can move it to `examples/`.
- **No copying:** the app gets the cache folder as `LIGHTING_DEMO_FETCHED_ASSETS` and loads fetched files by absolute path (the asset store takes absolute paths as given), so 100 MB is never copied into each build's asset root by the POST_BUILD copy.
- **Smoke runs never load fetched assets**, even in a build that has them (`FetchedAsset` checks `FENRIR_DEMO_SMOKE`): ADR-0052's "tests never use them" holds on a developer's machine too, and the missing-assets path runs in every smoke run.
- **Missing assets:** the scene spawns nothing, logs one info line, and each frame shows a keyed DebugDraw message naming `FENRIR_FETCH_DEMO_ASSETS` (amber, 22 px). All or nothing, since configure fetches every file or fails.
- **Committed panorama:** Poly Haven's Brown Photostudio 02 at 1k (1.65 MB, CC0), a studio with softboxes, for crisp reflections on the grid. `.gitattributes` now marks `*.hdr` binary (its header is text).
- **DamagedHelmet is CC-BY 4.0 and CC-BY-NC 4.0** (the earlier model by theblueturtle_ is non-commercial): fine to fetch for viewing, never to commit or ship; recorded in the manifest and `ATTRIBUTION.md`. FlightHelmet and SciFiHelmet are CC0.
- **The Lighting window** (`src/LightingPanel.cpp`) is shared by every scene: exposure, tonemapper, bloom intensity and radius, and the sky (panorama, intensity, rotation), written to every camera in the scene and to the scene's `Sky` each frame (neither intensity nor rotation refilters, ADR-0047). Keys T, E, B and S do the same for the smoke runs; each scene adds its own controls under it (the grid's sun and base colour, the showcase's turntable).
- **The grid** is LearnOpenGL's 7 x 7 with its albedo (linear 0.5, 0, 0), roughness 0 to 1 (not from 0.05: lit's specular anti-aliasing handles 0) and metallic 0 to 1, lit by the sky alone until L adds a sun. Point lights come with ticket 08.
- **README:** one `README.md` with a section per scene (what it shows, keys, hand checks), as the other demo apps have, rather than a file per scene.
- **Tests:** `cmake/Tests/` (new, added under `FENRIR_BUILD_TESTS`): `FetchDemoAssets-WrongHashFailsConfigure` and `FetchDemoAssets-CachedFileIsNotDownloadedAgain` configure a small project against a `file://` URL, so no test downloads. The second removes the upstream file before the second configure, so only the cache can satisfy it. The SHA-256s are literals from `sha256sum`. Plus the scaffold's `LightingDemo-Smoke-null` and `-opengl`.
- **Checked by eye** (screenshots, not committed): the grid under the studio, and the helmets under Studio Small 09 with and without bloom. A real `FENRIR_FETCH_DEMO_ASSETS=ON` configure downloaded all 33 files from the pinned URLs with matching hashes, and a second configure downloaded none. In Debug, entering Showcase models takes about 15 s: three 2k panoramas get CPU mips (`WriteWithMips`), and the helmets' PNGs are large.
- **msvc-x64-debug:** the whole CTest tree passed (170 tests: the two new configure tests and LightingDemo's two smoke runs among them, GL included; RTX 5080), with the build configured `FENRIR_FETCH_DEMO_ASSETS=ON`, so the smoke runs passed with the assets present and unused. Screenshots in `screenshots/07-lighting-demo/`.

Hand checks still to do, in LightingDemo: this ticket's (the helmets under each sky; the grid against LearnOpenGL's IBL), and those of 03 (panoramas), 04 (IBL), 05 (BRDF) and 06 (bloom).
