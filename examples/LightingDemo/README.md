# LightingDemo

The lighting spec's demo app (`GLOSSARY.md`: demo app): physically based lighting, image-based lighting from HDR
panoramas, tonemapping and bloom on the demo scaffold (`examples/DemoScaffold/README.md`), one scene per showcase, flown
through on the scaffold's free-fly camera. Built with the examples when the renderer is on. Each scene below has a
section of its own: what it shows, its keys and its hand checks. Later lighting tickets add their scenes to the list
(`.scratch/fenrir-renderer-lighting/spec.md`, "LightingDemo").

## The Lighting window

Every scene shows the same ImGui window, Lighting (`src/LightingPanel.cpp`): the cameras' exposure (EV stops),
tonemapper (AgX, PBR Neutral, ACES, None) and bloom (intensity and radius), and the sky: which panorama, its intensity
and its rotation. It writes them to every camera in the scene each frame, the free-fly camera among them. Keys do the
same, for the smoke runs and by hand: T the next tonemapper, E the next exposure (0, +1, +2, -1 EV), B bloom off or back
on at its default, S the next panorama where the scene has more than one.

## Assets

Committed ones are in `assets/` (`assets/ATTRIBUTION.md`); the large ones are fetched (ADR-0052). Configure with
`-DFENRIR_FETCH_DEMO_ASSETS=ON` and CMake downloads every file `fetched-assets.json` lists, about 100 MB, into
`FENRIR_DEMO_ASSET_CACHE` (by default `%LOCALAPPDATA%/FenrirEngine/demo-assets` on Windows, else
`$XDG_CACHE_HOME/FenrirEngine/demo-assets` or `~/.cache/FenrirEngine/demo-assets`), outside the build directory, and
checks each one's SHA-256: a file whose hash is not the manifest's fails configure, naming it. A file already cached with
the right hash is not downloaded again, so a clean build reuses the cache. The app loads the fetched files from the cache
by absolute path; nothing is copied into the build.

`fetched-assets.json` holds `{"assets": [{"path", "url", "sha256", "licence"}]}`: the file's place in the cache, its
pinned upstream URL (Khronos glTF-Sample-Assets at a commit, or Poly Haven), its SHA-256 and its SPDX licence. A file is
added only after its licence has been checked and written into `assets/ATTRIBUTION.md`.

A scene whose fetched assets are missing is empty, with an on-screen message naming `FENRIR_FETCH_DEMO_ASSETS`. Smoke runs
(`FENRIR_DEMO_SMOKE`) never load fetched assets, even when the build has them, so CI and the tests use committed assets
only.

## Scenes

### Material grid

Seven by seven spheres in `builtin:lit`, roughness 0 to 1 from left to right and metallic 0 to 1 from bottom to top,
labelled in world text (metallic on the right), all of LearnOpenGL's IBL red (linear 0.5, 0, 0), lit by Poly Haven's Brown Photostudio 02 (the
committed 1k panorama) alone; a sun can be added. The window's Scene part has the sun and the spheres' base colour.

| Keys | |
| --- | --- |
| T, E, B | The Lighting window's: tonemapper, exposure, bloom. |
| L | The sun (a DirectionalLight of intensity 3) on or off. |

Hand checks, on OpenGL:

- The grid reads as LearnOpenGL's IBL result does, in spirit: bottom left a glossy red plastic with a sharp reflection of
  the studio's softboxes, the reflection blurring and widening to the right until the rightmost column is matte.
  Upwards the rows turn metallic: the white reflections take on the red, and the top row is a red metal with no
  diffuse light. No row or column is much brighter or darker overall than its neighbours: a rough metal
  keeps its energy (ADR-0048's multi-scatter compensation), and no sphere glows at its rim.
- Turning the sky (Rotation) turns the reflections with it, without a hitch; Intensity scales the whole grid's light.
- L adds a sun from above and to the right, behind the camera: a highlight on each sphere, small and sharp at the left, broad at the right.
- T steps the tonemapper: AgX (the default) softens the brightest highlights towards white, PBR Neutral keeps the red
  truer, ACES is more contrasty, None clips the softbox reflections flat. E brightens a stop at a time.
- Bloom (B, and the window's sliders): the softbox reflections in the glossy spheres glow softly into their
  surroundings, with no hard threshold edge; intensity 0 removes it, and radius widens or narrows the glow.

### Showcase models

Khronos's DamagedHelmet, FlightHelmet and SciFiHelmet in a row, each about two metres high, under one of three Poly
Haven panoramas, Venice Sunset, Studio Small 09 and Symmetrical Garden 02, all fetched. The window's Scene part has a
turntable that turns the helmets slowly. Without the fetched assets (or in a smoke run) the scene is empty, and says on
screen to configure with `-DFENRIR_FETCH_DEMO_ASSETS=ON`. Entering it loads all three 2k panoramas and the helmets'
large textures at once: about 15 seconds in a Debug build on an RTX 5080 machine, the window not responding meanwhile.

| Keys | |
| --- | --- |
| T, E, B | The Lighting window's: tonemapper, exposure, bloom. |
| S | The next panorama. |
| R | The turntable on or off. |

Hand checks, on OpenGL with the assets fetched:

- Under each sky the helmets look as they do in Khronos's sample viewer: DamagedHelmet's worn metal and its glowing
  emissive panels (which bloom softly), its scratches in the normal map; FlightHelmet's leather, brass, glass lenses and
  wooden stand reading as their own materials; SciFiHelmet's painted and bare metal panels. Each is lit by the sky
  around it: warm and directional at the Venice sunset, soft and neutral in the studio, cool and even in the garden.
- S changes the sky and the helmets' light and reflections with it in the same frame, with no flash.
- Rotation turns the light round the helmets; the turntable (R) shows reflections sliding over the curved surfaces
  without swimming or sparkling.
- Without the fetched assets: an empty scene with an amber message naming `FENRIR_FETCH_DEMO_ASSETS`, and no error in
  the log.

### Many lights

A dark room, twenty metres square, of PBR surfaces: a floor of five by five grey tiles, rough to smooth from left to
right and dielectric to metal from back to front, four pale walls, a ring of eight columns (polished metal and blue
plastic) and spheres of gold and white plastic between them. Up to 256 coloured lights circle it, each marked by a small
glowing sphere of its colour: point lights bobbing at waist height, and every fourth a spot light up high, pointing
down and leaning out as it goes, its cone lighting a moving pool. A hundred are on at first. The window's Scene part has
the count (0 to 256), whether they move, the point and spot intensities and the range.

| Keys | |
| --- | --- |
| T, E, B | The Lighting window's: tonemapper, exposure, bloom. |
| M | 16, 64, 100 or 256 lights. |
| P | The lights moving or still. |

Hand checks, on OpenGL:

- With a hundred lights the room reads as lit by many small coloured lights: each makes a soft pool on the floor that
  fades to nothing before its range, with no hard edge where the range ends; spots light round pools with soft rims.
- The smooth floor tiles and the metal columns show sharp coloured highlights that slide as the lights move; the rough
  tiles show broad soft ones. The markers glow and bloom softly.
- The frame rate stays usable with a hundred lights, and with 256 (the loop runs over every light until clustering,
  ADR-0049).
- Range shrinks and grows every pool; 0 lights leaves the room almost black.

| Key | What it does |
| --- | --- |
| F1, F3, PageDown, PageUp | The scaffold's: help, the free-fly camera, the next and previous scene. |

## Smoke runs

The scaffold's `LightingDemo-Smoke-null` (everywhere) and `LightingDemo-Smoke-opengl` (label `gl`) run every scene for
60 frames with its keys pressed, and fail on any error. Neither uses the fetched assets, so Showcase models runs empty,
its message shown. `FENRIR_DEMO_SCENE="Material grid" FENRIR_DEMO_KEYS=1` runs one scene through its keys.
