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
cascades tinted red, green, blue and yellow from the nearest, the shadow atlas, the spot shadow atlas, or the light
clusters' heat: how many lights each pixel loops over, black none, 1 blue, 4 cyan, 16 green, 64 yellow and 256 red),
and the sky: which panorama, its intensity and its rotation. It writes them to every camera in the scene each frame, the free-fly camera among them.
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
`-DFENRIR_FETCH_DEMO_ASSETS=ON` and CMake downloads every file `fetched-assets.json` lists, about 800 MB (487 MB of it
the forest's Fir Tree 01), into `FENRIR_DEMO_ASSET_CACHE` (by default `%LOCALAPPDATA%/FenrirEngine/demo-assets` on
Windows, else `$XDG_CACHE_HOME/FenrirEngine/demo-assets` or `~/.cache/FenrirEngine/demo-assets`), outside the build
directory, and checks each one's SHA-256: a file whose hash is not the manifest's fails configure, naming it. A file
already cached with the right hash is not downloaded again, so a clean build reuses the cache. The app loads the fetched
files from the cache by absolute path; nothing is copied into the build.

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
plastic) and spheres of gold and white plastic between them. Up to 1024 coloured lights circle it, in 32 hues, each
marked by a small glowing sphere of its colour (each hue's markers one instanced draw): point lights bobbing at waist height, and every fourth a spot light up high, pointing
down and leaning out as it goes, its cone lighting a moving pool. A hundred are on at first, and the spot lights cast
shadows: the eight nearest the camera each frame, RendererConfig's default spot shadow budget. So do the point lights,
the four nearest (the default point shadow budget), each into a cube round it. The window's Scene part has the count (0
to 1024), whether they move, the point and spot intensities, the range, whether the spot lights cast, and their budget
(0 to 16, Renderer::SetSpotShadowBudget), and whether the point lights cast, and their budget (0 to 8,
Renderer::SetPointShadowBudget), and whether the lights are clustered (Renderer::SetClusteredLights). The budgets and
the clustering are the renderer's, so other scenes keep them as they were left.

| Keys | |
| --- | --- |
| T, E, B, O, G, H | The Lighting window's: tonemapper, exposure, bloom, ambient occlusion, the next debug view, the sun's shadows. |
| M | 16, 64, 100, 256 or 1024 lights. |
| P | The lights moving or still. |
| K | Spot shadows off or on. |
| J | Point shadows off or on. |
| C | The lights clustered or not. |

Hand checks, on OpenGL:

- With a hundred lights the room reads as lit by many small coloured lights: each makes a soft pool on the floor that
  fades to nothing before its range, with no hard edge where the range ends; spots light round pools with soft rims.
- The smooth floor tiles and the metal columns show sharp coloured highlights that slide as the lights move; the rough
  tiles show broad soft ones. The markers glow and bloom softly.
- The frame rate holds at a thousand lights with clustering on (ADR-0049), and C, turning it off, drops it to about a
  third. With 256 lights, or a thousand at a range of 1.5, C changes nothing on screen. G to the light clusters' heat
  shows the count following the lights as they move, cluster by cluster: cooler at 256 lights than at a thousand,
  red (the cap of 256) round the camera at a thousand and range 5, where the lights past the cap drop out of those
  clusters; lowering the range cools it to blue, black between the lights' reach.
- Range shrinks and grows every pool; 0 lights leaves the room almost black.
- The spot lights nearest the camera cast shadows of the columns and spheres across the floor, soft-edged and attached
  to their casters, with no acne on the lit floor or walls. As the lights circle, lights move in and out of the budget
  without popping badly: a light leaving it is far from the camera, its pool small on screen. K turns them off and on.
- G to the spot shadow atlas shows a three by three grid of grey tiles, eight of them each one spot light's view down
  onto the floor and the columns; the budget slider makes it a grid of the new size.
- The point lights nearest the camera, low among the columns and spheres, throw their shadows out across the floor and
  up the walls on every side of them, soft-edged and attached. No seam shows where a shadow crosses from one face of a
  light's cube to the next (look along the floor's diagonals from a light, where the faces meet). J turns them off and
  on.

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

A temperate forest clearing in late-afternoon sun: the dense, alpha-tested, animated instancing case. A clearing about
40 metres across lies in a shallow hollow, its edge ragged, ringed by a dense forest of Poly Haven's Fir Tree 01
(firs of 11 to 17 metres) with broadleaf Tree Small 02 and Fir Sapling below the crowns, out to 75 metres. A winding dirt
path crosses it from where the camera starts towards the sun, low beyond the trees ahead, whose gap in the crowns
lets it light the clearing's middle; a large mossy boulder (Rock Moss Set 01) stands in the foreground beside the path,
a fallen log (Dead Tree Trunk 02) beside it to the left. Ferns, shrubs, mossy rocks, pebbles, stumps, roots and dry
branches grow along the edge and under the trees, about 20,000 copies of thirteen species in all, half of them pebbles
(ADR-0051): each of a model's glTF nodes is a kind, with one instance list per level of detail, and each mesh one
InstancedMeshRenderer drawing that list.

The renderer has no LODs and its shadow cascades draw every caster, so the demo budgets the forest itself:

- **Trees thin with distance** (`ThinnedCards`, `src/MeshTools.cpp`): a tree's leaf and twig cards (the fir's
  4 million twig triangles are 800,000 cards) are split into their connected pieces, one in 16, 48 or 128 kept as the
  tree stands farther past the clearing's edge, each grown so the crown keeps its cover; the farther trunks are
  simplified by vertex clustering, the nearest kept whole. The deepest trees cast no shadow and are drawn darker instead, as the floor under them is.
- **The understory stays near the clearing**: ferns, pebbles and branches grow only within 12 to 20 metres of its
  edge, and only the nearest cast shadows.
- **Foliage is cut out by its alpha**: Poly Haven's JPEG glTFs leave the alpha out of the leaf and twig textures,
  so their cards draw solid whether the material is MASK, BLEND or OPAQUE, and each species' alpha map is fetched and put back into its
  base colour, which is drawn `Masked` with a cutoff of 0.3 (thin needles fade in the smaller mips): the cut-outs show
  in the depth prepass, the shadow cascades and the lit pass alike.

The ground is a generated terrain mesh drawn by the demo's ground shader (`shaders/ground.vert`, `ground.frag`): Poly
Haven's Forest Ground 04 in the clearing, Forest Leaves 04 under the trees and Stony Dirt Path on the path, blended by
weights the mesh carries per vertex and broken up by each texture's occlusion, the floor darkened under the canopy. The
grass is about 200,000 generated blades, no asset (`src/ForestGround.cpp`): thick in the open, thinning under the trees
and off the path, in 38 chunk meshes, each blade a curving strip with a face each way, drawn by the demo's wind shader
(`shaders/grass.vert`, `grass.frag`), whose vertex stage sways each blade by the square of its height in rolling gusts
and whose fragment stage lights it through `Fenrir/Lighting.glsl`. The renderer runs that vertex stage in the depth
prepass and the shadow cascades too, so the blades' depth, shadows and pixels sway together. GTAO (radius 1 metre here)
grounds the plants. The sky is Poly Haven's Qwantani Late Afternoon, whose sun the DirectionalLight comes from, or a sky
the demo makes with its sun in the same place.

The window's Scene part has the forest's density (cutting each list to its share, which uploads it again), the grass
shown or hidden, the counts, the last frame's draws, instances and shadow draws, and the frame time. In Release on an
RTX 5080 at 1280 by 720 with 4x MSAA the full scene takes about 30 ms a frame. Without the fetched assets the grass
still grows over plain earth-coloured ground and path, among box trunks and sphere boulders (two instance lists) and
stand-ins for the boulder and log, under the demo's sky, and the scene says so on screen.

| Keys | |
| --- | --- |
| T, E, B, O, G, H, S | The Lighting window's: tonemapper, exposure, bloom, ambient occlusion, the next debug view, the sun's shadows, the next panorama. |
| N | 25%, 50% or all of the forest. |
| M | The grass hidden or shown. |

Hand checks, on OpenGL (Release for a usable frame rate):

- The grass sways in rolling gusts, and its shadows on the ground and on its neighbours sway with it.
- The ferns', shrubs', saplings' and trees' leaves and twigs cut out cleanly in light and in their shadows: no
  box-shaped shadows of their cards, and no holes or halos where the prepass and the lit pass disagree.
- The sun lights the clearing's middle through the gap in the crowns; the edges and the forest lie in the trees' shade.
- G to the occlusion view shows the plants, boulder and log grounded by dark contact shading.
- Without the fetched assets: grass, trunks and boulders, and an amber message naming `FENRIR_FETCH_DEMO_ASSETS`, no
  error in the log.

Not yet here: haze and sun shafts through the trees (ticket 24), debug views of the albedo, normals, roughness,
metallic, direct and indirect light and wireframe (ticket 23); higher-quality trees and ground vegetation from Fab
(Megascans), which need a login and so cannot be fetched by the manifest; and the bark's texture transforms: the glTF
importer reads neither `KHR_texture_transform` nor a texture's `texCoord`, so Fir Tree 01's bark and one trunk and Tree
Small 02's branches sample their textures at the wrong scale.

| Key | What it does |
| --- | --- |
| F1, F3, PageDown, PageUp | The scaffold's: help, the free-fly camera, the next and previous scene. |

### Neon city

A neon city at night after rain: the many-lights scene at city scale. Twenty-five blocks of nine lots each, between
two-lane streets with sidewalks and dashed centre lines. Everything is generated except the asphalt and one of the
skies. The buildings rise towards the middle of the city:

- **Towers** stand on podiums that front the street, up to about 190 metres. More than half have a narrower crown.
- **Lower blocks** fill the other lots.
- **Roofs** carry plant rooms; some masts carry red beacons.

Every tier of every building is one copy in one instance list, drawn by the demo's facade shader
(`shaders/facade.vert`, `facade.frag`):

- **Styles:** punched windows, a glass curtain wall lit a floor at a time, or ribbon windows.
- **Storeys** are counted from the ground, so a tower's tiers keep their floors in line. Only a tier standing on the
  ground has the shop floor.
- **Copy colour:** its RGB is the windows' light (warm tungsten, neutral or cool fluorescent). Its alpha packs the share
  of windows lit, the style, and whether the tier has neon edge strips.
- **Edge strips** run up the corners and along the crowns of about half the towers. They and the odd window lit in a
  neon hue carry the city's colour from far off.
- **Distance:** as a bay shrinks below a few pixels, the windows give way to their average, so distant facades glow
  evenly instead of shimmering.
- **Lighting:** the walls light through `Fenrir/Lighting.glsl`.

1092 lights, all clustered (ADR-0049):

- **Neon signs, 594 rect lights.** On every wall that faces a street:
  - one-sided shop signs above the ground floor, about one to every six metres;
  - up to two two-sided blade signs standing out from it.

  Some low and middling roofs carry a billboard. Each sign glows on an unlit slab of its size and radiance, so the
  light and what shows it match.
- **Uplights, 154 spot lights.** Coloured washes up the towers' walls from their podiums. Each reaches three-quarters
  of the way up its tower, brighter for taller towers.
- **Street lamps, 200 point lights.** Two on every side of every block, on poles with glowing heads.
- **Headlights, 144 spot lights.** 72 boxy cars drive round the blocks, three to a lane, in four instance lists
  (bodies, cabins, headlamps, tail-lights) rebuilt each frame they move. Each car's two headlights dip a little onto
  the road ahead.

Beside them, 545 neon boards are stacked up some walls like a street of vertical signs. They glow but light nothing,
so they add colour without a light's cost.

The streets are Poly Haven's Asphalt 01, drawn by the demo's street shader (`shaders/street.vert`, `street.frag`):

- **Puddles:** two octaves of value noise over the world position, cut at the panel's puddle share. In a puddle the
  surface is smooth, level and darker. The signs mirror in it through the rect lights' LTC specular, the lamps and
  headlights through GGX.
- **Damp asphalt** between the puddles has its roughness lowered, so the lights streak along the street.
- **Edges:** at a puddle's edge, the asphalt's stones break the water first.

The sky is the demo's own city glow (a dark blue zenith over a magenta and orange light-pollution glow from the
horizon), or with S, Poly Haven's Kloppenheim 07 (Pure Sky), an overcast night lit from below by distant towns. Both
are drawn at 0.03 intensity, and there is no sun. Bloom (0.12), GTAO (1.5 m) and AgX finish it, at -1 EV: at 0 EV, AgX
lifts the dim spill on the upper walls to dusk.

The window's Scene part has:

- the frame time, the light and copy counts, and the last frame's draws and instances;
- clustering, the traffic and the puddles;
- each kind of light's intensity.

In Debug on an RTX 5080 at 1280 by 720 with 4x MSAA, a frame takes about 13 ms clustered or not: at this size the Debug
CPU work bounds it, not the light loop. The cluster heat view shows 16 to 64 lights a cluster along the streets, under
the cap of 256.

Without the fetched assets, the streets are plain dark asphalt, still wet, under the generated sky. Everything else is
generated, and the scene says so on screen.

| Keys | |
| --- | --- |
| T, E, B, O, G, H, S | The Lighting window's: tonemapper, exposure, bloom, ambient occlusion, the next debug view, the sun's shadows, the next sky. |
| N | The traffic stopped or moving. |
| M | The streets dry or wet again. |
| C | The lights clustered or not. |

Hand checks, on OpenGL:

- Neon streaks on the wet streets: the signs and headlights mirror in the puddles and smear along the damp asphalt
  between them. M dries the streets, and the streaks go.
- The signs glow, with bloom round them; their light falls on the walls and sidewalks below them in their colour.
- From high above, the skyline glows: edge-lit towers, coloured washes up their walls, beacons, and the glow behind.
- The headlights light the road ahead of each car, and move with it.
- The frame rate stays smooth flying down the streets with the free-fly camera; distant facades do not shimmer.
- G to the light clusters' heat: no red (256 lights) anywhere along the streets.
- Without the fetched assets: dark wet streets, the city and its lights, and an amber message naming
  `FENRIR_FETCH_DEMO_ASSETS`, no error in the log.

| Key | What it does |
| --- | --- |
| F1, F3, PageDown, PageUp | The scaffold's: help, the free-fly camera, the next and previous scene. |

## Smoke runs

The scaffold's `LightingDemo-Smoke-null` (everywhere) and `LightingDemo-Smoke-opengl` (label `gl`) run every scene for
60 frames with its keys pressed, and fail on any error. Neither uses the fetched assets, so Showcase models runs empty
Showroom's plinths bare, the asteroid ring of spheres and cubes, the forest clearing of grass, boulders and trunks and the neon city's streets plain under its generated sky, each with its message shown. `FENRIR_DEMO_SCENE="Material grid" FENRIR_DEMO_KEYS=1` runs one scene through its keys.
