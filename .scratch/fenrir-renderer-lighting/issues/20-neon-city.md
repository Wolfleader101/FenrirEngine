# 20: The neon city at night

**What to build:** The lighting showcase (spec: "LightingDemo").

- **Scene 7:** procedural instanced buildings drawn by a user **facade shader** that lights windows by per-instance colour and lights through `Fenrir/Lighting.glsl`; Poly Haven asphalt (fetched) with a puddle mask lowering roughness; rect-light neon signs with emissive meshes to match, reflected in the wet streets through LTC specular; point-light street lamps; spot-light car headlights on instanced cars; a Poly Haven night panorama at low intensity; bloom, GTAO and AgX.
- Hundreds of lights, clustered.

**Blocked by:** 06, 11, 13, 19

**Touches:** `examples/LightingDemo/`

**Status:** resolved

- [x] LightingDemo's smoke runs pass on null with no fetched assets.

Hand check: neon streaks on wet streets, glowing signs, and a smooth frame rate flying down the streets.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

Built 2026-11-02, stacked on ticket 19's branch (#265 to #279 not yet merged). Decisions to review:

- **No renderer change.** A user shader reached `Fenrir/Lighting.glsl`, the instance lists and their copy colours as
  the forest's did; nothing was missing.
- **The city, reworked after a first look** (the user found it flat from afar: too few lights, plain boxes). 5 x 5
  blocks of 3 x 3 lots; towers on street-fronting podiums, up to about 190 m in the middle, half with a crown tier;
  lower blocks between; plant rooms on the roofs, masts with red beacons. Every tier is one copy in one instance list,
  all generated from the cube and sphere, no fetched model. `src/NeonCity.cpp`.
- **1092 lights at the demo's seed:** 594 rect-light signs (shop signs every six metres or so, up to two blade signs a
  street wall, billboards on some roofs, each on a board of its size in the sign shader), 154 coloured spot uplights
  washing the towers, 200 point-light lamps, 144 spot headlights on 72 cars whose four instance lists are replaced each
  frame they move. 545 neon boards stacked up some walls glow but light nothing: colour without a light's cost.
- **The facade shader** (`shaders/facade.*`), reworked a second time (the user found the flat-coloured walls fake
  beside the textured street): fetched Poly Haven walls (Concrete Tile Facade, lifted x2.5 from charcoal; Dark Brick
  Wall; Exterior Wall Cladding; eight textures, the set's limit) by world position at their real sizes, normal mapped,
  streaked with grime; windows by bay and storey counted from the ground, in three styles (punched with frame, reveal
  and sill; curtain wall with metal mullions, lit a floor at a time; ribbon); **rooms behind the windows by interior
  mapping** (a ray-box into a room the bay wide, the storey high, 5 m deep or 8 for shops: back wall with desks, side
  walls, floor, a ceiling light panel), some with blinds; the copy colour's RGB is the rooms' light and its alpha packs
  the share lit, the style and the edge-strip flag; neon edge strips on about half the towers; distant windows fade to
  their average, so far facades do not shimmer.
- **The sign shader** (`shaders/sign.*`): every sign board and wall board is neon tubes on a dark board, an edge tube
  and hashed glyph strokes, in a row on wide boards and a column on tall ones, fading to their average far off. Flat
  glowing slabs looked fake once the walls were real.
- **A Vsync checkbox** in the panel sets the main window's swap interval (`Window::SetSwapInterval`; LightingDemo now
  links FenrirWindow): with vsync on, Release sat at the display's 240 Hz and its frame time said nothing.
- **Uplights reach three-quarters of the way up**, brighter by the square of their reach. Reaching the top doubled
  the cluster load (yellow to orange in the heat view) and cost 4 ms in Debug.
- **The puddle mask is procedural**, in the street shader (`shaders/street.*`): value noise over the world position, cut
  at a share the panel sets. Puddles are smooth, level and darker, the asphalt between them damp (roughness x 0.3).
  No mask texture was fetched.
- **The sky is the demo's own city glow by default**, a magenta and orange light-pollution glow from the horizon,
  with Poly Haven's Kloppenheim 07 (Pure Sky) on S. Modern Buildings Night was tried first: its bright plaza floor,
  below the horizon, made the city read as a model on a table.
- **PBR Neutral, not AgX, by the user's choice after the second rework** (against cyberpunk reference renders): AgX
  turned the neon pastel and lifted the lamps' dim spill on the upper walls towards dusk. Four looks were compared
  with the existing post effects only; the city now starts with PBR Neutral at -0.5 EV, bloom 0.3 at radius 0.85, signs
  at 8, puddles over 0.75 of the street and the damp asphalt at 0.15 of its roughness, so the signs' colours streak
  down the road. Wetter (0.9, damp 0.08) turned the road to a mirror. T still steps to AgX. Sky 0.03, windows 0.12.
- **Not asked for:** `src/DemoMaterials.*` (`EmbeddedMaterial` and `AsLinear` moved out of the forest for both scenes),
  keys N (traffic), M (wet or dry) and C (clustering), and the panel's intensity sliders.
- **Timing (RTX 5080, 1280 x 720, 4x MSAA, 1092 lights, street level; the panel's frame time, wall clock smoothed
  over about a second, alternating runs):** Release with vsync off, 1.37 and 1.28 ms
  clustered against 10.20 and 10.31 ms unclustered; above a street 1.41 against 11.00 ms. Debug about 13 ms, CPU
  bound. The cluster heat view shows 16 to 64 lights a cluster, under the cap of 256.
- **Release build** (`msvc-x64-release`, LightingDemo only, tests and scripting off), asked for by the user for this
  scene. Twice in about twenty launches, a Release start straight after a forced kill of the previous one exited at
  once with 0xC0000142 (a DLL failed to initialise, before `main`, no log line); the next launch ran. Not seen in
  Debug.
- **Screenshots:** `.scratch/fenrir-renderer-lighting/screenshots/20-neon-city/`.
- **msvc-x64-debug:** the whole CTest tree passed (GL included, RTX 5080): 163 of 170 at once, and the seven C#
  hosting tests run again after their `Iris.Bridge` runtime configs, zeroed by a session crash mid-build, were copied
  back from `iris/bridge`. LightingDemo's smoke runs passed again after the review fixes and each rework.
