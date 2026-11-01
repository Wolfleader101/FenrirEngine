# LightingDemo

The lighting spec's demo app (`GLOSSARY.md`: demo app): physically based lighting, image-based lighting from HDR
panoramas, tonemapping and bloom on the demo scaffold (`examples/DemoScaffold/README.md`), one scene per showcase, flown
through on the scaffold's free-fly camera. Built with the examples when the renderer is on. Each scene below has a
section of its own: what it shows, its keys and its hand checks. Later lighting tickets add their scenes to the list
(`.scratch/fenrir-renderer-lighting/spec.md`, "LightingDemo").

## The Lighting window

Every scene shows the same ImGui window, Lighting (`src/LightingPanel.cpp`): the cameras' exposure (EV stops),
tonemapper (AgX, PBR Neutral, ACES, None), bloom (intensity and radius) and ambient occlusion (GTAO: on, which it is
by default here, its radius and strength), the sun's shadows (on, their distance and their normal and slope biases,
given to every DirectionalLight in the scene), a debug view (the lit scene, the raw occlusion as grey, the shadow
cascades tinted red, green, blue and yellow from the nearest, or the shadow atlas), and the sky: which panorama, its
intensity and its rotation. It writes them to every camera in the scene each frame, the free-fly camera among them.
Keys do the same, for the smoke runs and by hand: T the next tonemapper, E the next exposure (0, +1, +2, -1 EV), B bloom
off or back on at its default, O ambient occlusion off or back on, G the next debug view, H the sun's shadows off or
back on, S the next panorama where the scene has more than one.

Hand check for ambient occlusion: in Showcase models, contact shadows under the helmets where they meet the ground, and
with G, dark creases and corners on white, no dark halo round a model's silhouette against what is far behind it.

Hand check for the sun's shadows (ADR-0050): flying round Showcase models with the free-fly camera, the helmets'
shadows on the floor hold still, neither shimmering as the camera turns nor swimming as it moves; G to the cascade
colours shows four bands on the floor, their boundaries following the camera, and no seam in a shadow that crosses one.
The lit floor shows no speckled acne at any sun angle the sky's rotation leaves it; raising the biases in the window
detaches the shadows from the helmets' bases, lowering them to 0 brings acne back.

## Assets

Committed ones are in `assets/` (`assets/ATTRIBUTION.md`); the large ones are fetched (ADR-0052). Configure with
`-DFENRIR_FETCH_DEMO_ASSETS=ON` and CMake downloads every file `fetched-assets.json` lists, about 190 MB, into
`FENRIR_DEMO_ASSET_CACHE` (by default `%LOCALAPPDATA%/FenrirEngine/demo-assets` on Windows, else
`$XDG_CACHE_HOME/FenrirEngine/demo-assets` or `~/.cache/FenrirEngine/demo-assets`), outside the build directory, and
checks each one's SHA-256: a file whose hash is not the manifest's fails configure, naming it. A file already cached with
the right hash is not downloaded again, so a clean build reuses the cache. The app loads the fetched files from the cache
by absolute path; nothing is copied into the build.

`fetched-assets.json` holds `{"assets": [{"path", "url", "sha256", "licence"}]}`: the file's place in the cache, its
pinned upstream URL (Khronos glTF-Sample-Assets at a commit, Poly Haven, or Solar System Scope), its SHA-256 and its
SPDX licence. A file is
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
| T, E, B, O, G, H | The Lighting window's: tonemapper, exposure, bloom, ambient occlusion, the next debug view, the sun's shadows. |
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

Khronos's DamagedHelmet, FlightHelmet and SciFiHelmet in a row, each about two metres high, on a grey floor under one
of three Poly Haven panoramas, Venice Sunset, Studio Small 09 and Symmetrical Garden 02, all fetched, with a low warm sun
behind them on the left casting their shadows towards the camera. The window's Scene part has a
turntable that turns the helmets slowly. Without the fetched assets (or in a smoke run) the scene is empty, and says on
screen to configure with `-DFENRIR_FETCH_DEMO_ASSETS=ON`. Entering it loads all three 2k panoramas and the helmets'
large textures at once: about 15 seconds in a Debug build on an RTX 5080 machine, the window not responding meanwhile.

| Keys | |
| --- | --- |
| T, E, B, O, G, H | The Lighting window's: tonemapper, exposure, bloom, ambient occlusion, the next debug view, the sun's shadows. |
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
| T, E, B, O, G, H | The Lighting window's: tonemapper, exposure, bloom, ambient occlusion, the next debug view, the sun's shadows. |
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

### Showroom

A dark room, twelve metres wide and ten deep, lit by rect lights (ADR-0048) alone: a magenta neon strip low along the
back wall, a warm white panel high on it to the left, and a cyan two-sided sign hung side on from the ceiling to the
right, lighting both halves of the room. Each shows as a thin glowing slab of its size and radiance. The floor is one
dark polished slab whose roughness rises smoothly from a near mirror (0.02) at its left edge to satin (0.5) at its
right, slowly at first: 0.05 a quarter of the way across, 0.14 in the middle.
Khronos's DamagedHelmet, FlightHelmet and SciFiHelmet stand on white plinths across the middle, fetched (ADR-0052);
without them the plinths are bare and the scene says so on screen. The window's Scene part has each light's width,
height, intensity and sidedness.

| Keys | |
| --- | --- |
| T, E, B, O, G, H | The Lighting window's: tonemapper, exposure, bloom, ambient occlusion, the next debug view, the sun's shadows. |
| Z | Every light at half, as set, or twice its size. |
| V | Every light's sidedness flipped. |

Hand checks, on OpenGL:

- The polished floor reflects each light as its own shape: the neon strip as a thin line and the panel as a rectangle,
  sharp-edged on the left of the floor and blurring, stretching towards the camera and dimming across it to the right.
- Widening, heightening or dimming a light in the window changes its slab, its reflection and the light it casts in the
  same frame; Z does it to all three at once.
- The sign lights the floor and walls on both sides of it; turned one-sided (its checkbox, or V), only the side its front
  faces, towards -X, the room's left half, is lit. V also makes the neon and the panel two-sided: each then lights the
  strip of back wall just behind it too.
- The helmets' metal shows the lights' shapes in its highlights; the room stays dark beyond the lights' ranges.
- Without the fetched assets: bare plinths and an amber message naming `FENRIR_FETCH_DEMO_ASSETS`, no error in the log.

### Asteroid ring

A planet forty metres across, in Solar System Scope's Jupiter texture, ringed from 30 to 55 metres out by 100,000 rocks
of three Poly Haven moon-rock kinds (ADR-0051): one InstancedMeshRenderer each, on children of one ring entity, so the
ring is three draws a pass however many rocks it holds, its copies each turned, scaled, squashed and tinted at random.
Poly Haven's rocks have 3,300 to 6,800 triangles each and the renderer has no LODs, so the demo simplifies each mesh at
spawn by vertex clustering: on 5 cells a side for the ring, whose rocks are a few pixels across, and 16 for the field.
At full detail the ring took 136 ms a frame in Release on an RTX 5080, shading micro-triangles; simplified, 9 ms.
The ring entity turns slowly about the planet; its lists were uploaded once and are not uploaded again as it turns.
Beside the ring's outer edge floats a field of 5,000 rock entities, each its own MeshRenderer in one of nine tinted
materials, which automatic instancing merges into a handful of draws, and beside the field a station: a hull and six
modules on spokes, glowing windows (emissive, strength 6) and on each module a coloured beacon with a point light that
lights the field's nearest rocks. The sky is a starfield panorama the demo makes at spawn: faint blue-black, a soft milky
band round a tilted great circle and stars, a few bright enough to bloom. A sun lights everything from one side, and the
planet's shadow falls across the ring behind it (its shadow distance 200, so the whole ring is in the cascades from the
start; the ring's rocks take shadows and cast none, which would cost four times the ring again). The
rocks and the planet's texture are fetched; without them the ring is spheres and cubes and the planet plain, and the
scene says so on screen.

The window's Scene part has the ring's count (cutting each list to its share, which bumps its revision and uploads it
again), the merging switch (Renderer::SetAutomaticInstancing: the renderer's, so the other scenes keep it as it was left), the
ring turning, and the last frame's draws, instances
and uploads.

| Keys | |
| --- | --- |
| T, E, B, O, G, H | The Lighting window's: tonemapper, exposure, bloom, ambient occlusion, the next debug view, the sun's shadows. |
| N | 10k, 50k or 100k ring rocks. |
| I | Automatic instancing off or back on: the field's 5,000 rocks become 5,000 draws, and back to a handful. |
| R | The ring turning or still. |

Hand checks, on OpenGL:

- The ring holds a smooth frame rate at 100k rocks, and the panel shows a few dozen draws and over 100,000 instances.
- The station's beacons light the nearest rocks of the field in their colours; the rocks far from it are lit by the sun
  and the sky alone.
- Switching merging off multiplies the draws by thousands and costs frame time; the ring stays three draws either way.
- Uploads stay 0 while the ring turns; moving the count slider shows 3 uploads in that frame.
- Without the fetched assets: spheres and cubes in the ring and field, a plain planet, and an amber message naming
  `FENRIR_FETCH_DEMO_ASSETS`, no error in the log.
- The planet's shadow darkens the ring's far side behind it, its edge sliding over the rocks as the ring turns, and the
  field's rocks shadow one another.

### Forest clearing

The dense, alpha-tested, animated instancing case: a clearing at golden hour, its grass swaying in the wind under a low
warm sun's cascaded shadows, the forest round it of about 20,000 copies of ten Poly Haven species (ADR-0051): fir and pine
saplings grown to trees, ferns, two shrubs, two rocks, a fallen trunk, stumps and dry branches. Each of a model's glTF
nodes is a variant with one instance list of its copies, turned, scaled and tinted at random, and each mesh of a variant
one InstancedMeshRenderer drawing that list (the saplings' bark and twigs share one): 22 lists, 30 draws a pass with
the ground and the grass.
The pine twigs, ferns and shrubs are `Masked` glTF materials, cut out by their alpha in the depth prepass, the shadow
cascades and the lit pass alike. The rocks are simplified at spawn by vertex clustering as the asteroid ring's are (the
pebbles cast no shadow); the leaves and twigs cannot be, so the forest is triangle-heavy (the saplings' twigs have
120,000 to 160,000 triangles each) and there are few trees, with the panorama's autumn forest standing beyond them. The
trees leave a gap towards the sun, so it reaches the clearing; the forest floor is Poly Haven's Forest Ground 04, the sky
its Autumn Forest 04 panorama or a golden-hour sky the demo makes, whose sun sits where the DirectionalLight comes from.

The grass is about 200,000 blades, no asset: one patch mesh of 1,800 generated blades, each a tapering, curving strip of
five triangles with its own height, lean and tint, drawn by one InstancedMeshRenderer at every four-metre cell of the
clearing, each copy turned a quarter-turn at random. Its material is the demo's own shader (`shaders/grass.vert`,
`shaders/grass.frag`, built by `fenrir_add_shaders` and embedded): the vertex stage sways each vertex by the square of its
height in a gust rolling along the wind with a flutter across it, driven by the frame's time, and the fragment stage
lights the blade through `Fenrir/Lighting.glsl`. The renderer runs that vertex stage in the depth prepass and the shadow
cascades too, so the blades' depth, shadows and pixels sway together. GTAO darkens the ground among the plants.

The window's Scene part has the forest's density (cutting each list to its share, which uploads it again), the grass
shown or hidden, the counts, and the last frame's draws, instances and shadow draws. Without the fetched assets the grass
still grows, on a plain earth ground among sphere boulders and box trunks (two instance lists), under the golden-hour
sky, and the scene says so on screen.

| Keys | |
| --- | --- |
| T, E, B, O, G, H, S | The Lighting window's: tonemapper, exposure, bloom, ambient occlusion, the next debug view, the sun's shadows, the next panorama. |
| N | 25%, 50% or all of the forest. |
| M | The grass hidden or shown. |

Hand checks, on OpenGL:

- The grass sways in rolling gusts, and its shadows on the ground and on its neighbours sway with it.
- The ferns', shrubs' and pine twigs' leaves cut out cleanly in light and in their shadows: no box-shaped shadows of
  their cards, and no holes or halos in the lit leaves where the prepass and the lit pass disagree.
- G to the occlusion view shows the plants grounded by dark contact shading at their feet.
- Without the fetched assets: grass, boulders and trunks, and an amber message naming `FENRIR_FETCH_DEMO_ASSETS`, no
  error in the log.

| Key | What it does |
| --- | --- |
| F1, F3, PageDown, PageUp | The scaffold's: help, the free-fly camera, the next and previous scene. |

## Smoke runs

The scaffold's `LightingDemo-Smoke-null` (everywhere) and `LightingDemo-Smoke-opengl` (label `gl`) run every scene for
60 frames with its keys pressed, and fail on any error. Neither uses the fetched assets, so Showcase models runs empty
Showroom's plinths bare, the asteroid ring of spheres and cubes and the forest clearing of grass, boulders and trunks, each with its message shown. `FENRIR_DEMO_SCENE="Material grid" FENRIR_DEMO_KEYS=1` runs one scene through its keys.
