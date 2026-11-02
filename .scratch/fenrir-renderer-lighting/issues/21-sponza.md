# 21: Sponza, the flagship scene

**What to build:** Everything at once in a real scene (spec: "LightingDemo").

- **Scene 8:** Khronos Sponza (fetched) under a Poly Haven sky, with the sun's cascades, shadowed point lights (torches), GTAO, bloom and the tonemapper, flown around on the free-fly camera.
- The README collects every scene's hand checks.

**Blocked by:** 11, 18, 19

**Touches:** `examples/LightingDemo/`

**Status:** resolved

- [x] LightingDemo's smoke runs pass on null with no fetched assets.

Hand check: Sponza looks right from the floor and the galleries, with stable shadows and no light leaks.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

Built 2026-11-02, stacked on ticket 20's branch at `7571fa6` (#265 to #280 not yet merged). Decisions to review:

- **A renderer fix, in `Fenrir/Shadows.glsl`.** A face turned from a shadowed light skipped its shadow lookup and
  relied on N.L to darken it, but a normal map can turn the shading normal back towards the light: Sponza's arch soffits
  in shadow took speckled, unshadowed sunlight. Now such a face takes `FenrirTurnedFromLight`'s share, all of the light
  at the terminator (so faceted spheres still shade smoothly and every golden image still matches), fading to none a
  quarter turned away. Within that quarter a face still takes a little light unlooked-up, less than the whole of it
  before. It applies to the sun's, spot and point shadows alike; lights without shadows are unchanged. New
  GL tests in `Tests/OpenGL/Shadows.cpp`; the shadowed one fails without the fix (0.72 lit against under 0.02).
- **Sponza is Khronos's, fetched:** 71 files, 53 MB, pinned to glTF-Sample-Assets commit `8f49239`, as the helmets
  are, each file's git blob hash checked against the commit's tree. **Its licence is the CRYENGINE Limited License
  Agreement** (`LicenseRef-CRYENGINE-Agreement`), Crytek's engine licence, not an open one: the files are fetched for
  viewing and never committed or shipped, as DamagedHelmet's non-commercial ones are. Worth a look before anything ships
  with the demo's assets.
- **The sky is Poly Haven's Kloofendal 48d Partly Cloudy (Pure Sky)**, CC0, and the sun is its own: the brightest
  texels' centroid, 48 degrees up, turned with the sky's rotation each frame (the panel's rotation moves both). The sky
  starts at -0.834 rad so the sun shines down the courtyard's length; the courtyard is only 4 metres wide and 10 high, so
  a sun from across it would leave the whole floor in shade.
- **Sky at 0.35.** With no light probes the sky lights the galleries' shade as brightly as the open courtyard, GTAO
  aside; drawn dim, the shade stays darker than the sunlit stone and the torches carry the galleries.
- **Four torches, the default point shadow budget,** each on the gallery side of one of the courtyard's middle columns
  (Crytek's Sponza has no torches): an iron bracket and a glowing flame, both casting no shadow, and a point light that
  flickers. The scene sets the budget to 4 and clustering on when entered, as other scenes leave them.
- **Without fetched assets** a floor, two colonnades of plain columns and the galleries' outer walls stand in, with the
  sun and the torches, so the smoke runs still draw cascades and point shadows.
- **README:** every scene's hand checks, 1 to 8, are now in one "Hand checks" section after the scenes; each scene's
  section keeps what it shows and its keys.
- **Not asked for:** keys J (torch shadows), K (torches out) and N (flicker), and the panel's sun and torch sliders and
  vsync checkbox.
- **Timing (RTX 5080, 1280 x 720, 4x MSAA):** Debug about 11 ms a frame (vsync on, under 240 Hz); Release about 2.2 ms
  (2.11 to 2.23 over four runs from the floor and the lower gallery, vsync off; the panel now has the neon city's
  Vsync checkbox for this), 105 draws and 824 shadow draws.
- **Screenshots:** `.scratch/fenrir-renderer-lighting/screenshots/21-sponza/`.
