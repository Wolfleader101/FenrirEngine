# What does the game look like?

Type: grilling
Status: resolved
Blocked by: 01, 11

## Question

The intent is a stylised, toon-shaded look, as the design document wanted (settled while charting). What is it made of, and can the renderer do it today?

- The shading: banded light, outlines or none, how shadows read.
- The palette, sky, fog and time of day for the one level.
- Which renderer features the game leans on and which it switches off through its configuration.
- Effects: what coins, cages opening, power-ups and stomps show (particles, if the engine has them).

If the renderer cannot take a game's own shading, that is an engine gap; decide what the game looks like until it is closed. If the answer hangs on seeing it, split off a prototype ticket.

## Answer

Grilled 2026-11-19, in two rounds.

**The rule** (the user): the game does not fake a renderer feature the engine lacks. Each one is an engine issue that must be resolved before the game is complete. None blocks the first playable slice, which goes without until then.

### What the game does itself, today

- **The shading** is one toon material shader of the game's own, used on everything:
  - light in three hard bands to start with;
  - the sun's shadow as the darkest band, with a hard edge;
  - shadows tinted cool, not black;
  - a thin rim of light;
  - the sky as flat fill light.

  Sun and sky only; no local lights for the first level. The band count and tints are shader parameters. The engine supports a game's own material shader, so this is the game's work and not a gap.
- **Sky and time of day:** a bright midday, with saturated bush greens, warm rock and a blue sky. The sky is Darcy Wilson's CC0 skybox already in this repo (`examples/assets/skybox-darcy/`), copied into the game's own assets and credited there. It is lit with a neutral fill light, as RendererDemo does for it, because its teal sea would otherwise tint everything.
- **Renderer settings:**

  | On | Off |
  | --- | --- |
  | cascaded sun shadows | ambient occlusion |
  | 4x MSAA | reflections |
  | PBR Neutral tonemapping | lens flare and streaks |
  | a little bloom, for coins and power-ups | chromatic aberration, film grain, vignette |

  PBR Neutral keeps colours close to what the shader wrote.
- **Motion that needs no engine feature** is in from the start: coins and power-ups spin and bob, a cage's door swings open, an enemy squashes flat when stomped.
- **The UI** follows the same look: rounded, chunky panels in the level's greens and sky blue with a warm yellow accent, set in Fredoka.

### What waits on the engine

Until each is resolved the game goes without it; it builds no stand-in.

| The look wants | Engine issue |
| --- | --- |
| Dark outlines on Kiki, friends, enemies and pickups; none on the level | [A game cannot draw outlines or add a render pass](../../engine-gaps/issues/07-outlines-and-game-render-passes.md) |
| Haze softening the level's far edges | [No fog](../../engine-gaps/issues/14-fog.md) |
| Bursts on pickups, cages opening, stomps and landings; later, weather | [No particles or billboards](../../engine-gaps/issues/06-particles-and-billboards.md) |
| Friends in their own body colours; Kiki's rim light taking an active power-up's colour | [One entity cannot have its own colour without its own material](../../engine-gaps/issues/15-per-entity-material-colour.md) (cloned materials serve until then) |

Rejected as stand-ins: outline meshes turned inside out in Blender, a distance fade inside the game's shader, and bursts of instanced meshes the game moves itself.

### Left to a prototype

The band count, the shadow tint, and whether the 2022 textures (64 and 128 pixels) hold up cannot be chosen on paper: ["What does the toon look need?"](16-what-the-toon-look-needs.md).
